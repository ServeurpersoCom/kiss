// proxy.cpp: the proxy of the servers the proxy module names
//
// Each request is built anew from the one it proxies, never spliced: its method,
// the rest of its path and its query after the url of the route, its body and
// every header but those that stay on its side, then the headers of the route,
// which replace those of the same name, and its key, placed as its auth says.
// The key of this server never leaves it. The answer keeps its status and every
// header but those that stay on its side, its body streamed as it arrives. A
// server that refuses the key of the route answers 502 here, so the client
// never takes it for a refusal of the key of this server; one that redirects
// answers 502 too, its key going to no address the route does not name. The
// proxy knows no protocol: MCP and LLM endpoints pass alike.

#include "proxy.h"

#include "engine/engine.h"
#include "engine/text.h"
#include "log.h"

#include <memory>
#include <optional>

// the headers that stay on their side of the proxy, lowercase: those of the
// connection, those that tell who each side is, the keys, and those the proxy
// writes itself, a body passing uncompressed so it streams as it arrives
static const char * const SIDE_HEADERS[] = {
    "connection",
    "keep-alive",
    "te",
    "trailer",
    "transfer-encoding",
    "upgrade",
    "host",
    "origin",
    "referer",
    "cookie",
    "set-cookie",
    "authorization",
    "x-api-key",
    "content-type",
    "content-length",
    "content-encoding",
    "accept-encoding",
};

// the bytes of the body read and passed on at a time
static const size_t PROXY_CHUNK = 16384;

// how a route sends its key: as a bearer token, or as the x-api-key of the
// Anthropic API
static const char * const AUTH_X_API_KEY = "x-api-key";

// a header that stays on its side: one of the list, or one of a proxy or of
// CORS, which each side writes for itself
static bool stays(const std::string & header) {
    const std::string h = lower(header);
    if (starts_with(h, "proxy-") || starts_with(h, "access-control-")) {
        return true;
    }
    for (const char * x : SIDE_HEADERS) {
        if (h == x) {
            return true;
        }
    }
    return false;
}

// a route as one same configuration holds it
struct route_conf {
    std::string url;
    std::string key;
    std::string auth;
    std::string headers;
    int         timeout;
};

static std::optional<route_conf> route_of(const std::string & name) {
    std::optional<route_conf> r;
    engine_with([&](const engine_reader & config) {
        const auto url = config.get("proxy url", name);
        if (url) {
            r = route_conf{ *url, config.get("proxy key", name).value_or(""),
                            config.get("proxy auth", name).value_or(""), config.get("proxy headers", name).value_or(""),
                            std::stoi(config.get("proxy timeout", name).value_or("0")) };
        }
    });
    return r;
}

// https://host:port/path as httplib takes it: the part it connects to, then
// the path of every request
static void split_url(const std::string & url, std::string & host, std::string & path) {
    const size_t slash = url.find('/', url.find("://") + 3);
    host               = url.substr(0, slash);
    path               = slash == std::string::npos ? "/" : url.substr(slash);
}

static void refuse(httplib::Response & res, int status, const std::string & why) {
    res.status = status;
    res.set_content(why + "\n", "text/plain");
}

void proxy(const httplib::Request & req, httplib::Response & res, const std::string & route, const std::string & rest) {
    const auto r = route_of(route);
    if (!r) {
        refuse(res, 404, "no proxy route \"" + route + "\"");
        return;
    }
    std::string host, base;
    split_url(r->url, host, base);
    // the rest joins the url with one slash, whether the url ends with one or not
    const std::string head   = rest.empty() ? base : base.substr(0, base.find_last_not_of('/') + 1);
    const size_t      query  = req.target.find('?');
    const std::string path   = head + rest + (query == std::string::npos ? "" : req.target.substr(query));
    const auto        client = std::make_shared<httplib::Client>(host);
    if (!client->is_valid()) {
        refuse(res, 502, r->url + " cannot be reached: a bad port, or https on a build without TLS");
        return;
    }
    client->set_connection_timeout(r->timeout, 0);
    client->set_read_timeout(r->timeout, 0);

    httplib::Headers headers;
    for (const auto & h : req.headers) {
        if (!stays(h.first)) {
            headers.emplace(h.first, h.second);
        }
    }
    // a header of the route replaces the one of the same name the client sent
    for (const std::string & pair : split(r->headers, ';')) {
        const size_t colon = pair.find(':');
        if (colon != std::string::npos) {
            const std::string name = trim(pair.substr(0, colon));
            headers.erase(name);
            headers.emplace(name, trim(pair.substr(colon + 1)));
        }
    }
    if (!r->key.empty()) {
        if (r->auth == AUTH_X_API_KEY) {
            headers.emplace("x-api-key", r->key);
        } else {
            headers.emplace("Authorization", "Bearer " + r->key);
        }
    }

    const auto stream = std::make_shared<httplib::ClientImpl::StreamHandle>(
        client->open_stream(req.method, path, {}, headers, req.body, req.get_header_value("Content-Type")));
    if (!stream->is_valid()) {
        const std::string why = r->url + " cannot be reached: " + httplib::to_string(stream->error);
        log_error("Proxy", "Route proxy/%s: %s", route.c_str(), why.c_str());
        refuse(res, 502, why);
        return;
    }
    const int status = stream->response->status;
    if (status == httplib::StatusCode::Unauthorized_401 || status == httplib::StatusCode::Forbidden_403) {
        const std::string why = r->url + " answered " + std::to_string(status) + " -> set proxy key " + route;
        log_error("Proxy", "Route proxy/%s: %s", route.c_str(), why.c_str());
        refuse(res, 502, why);
        return;
    }
    if (status >= httplib::StatusCode::MultipleChoices_300 && status < httplib::StatusCode::BadRequest_400) {
        const std::string why = r->url + " answered " + std::to_string(status) + " to " +
                                stream->response->get_header_value("Location") + " -> set proxy url " + route;
        log_error("Proxy", "Route proxy/%s: %s", route.c_str(), why.c_str());
        refuse(res, 502, why);
        return;
    }
    log_debug("Proxy", "Route proxy/%s: %s %s %d", route.c_str(), req.method.c_str(),
              (rest.empty() ? "/" : rest).c_str(), status);
    res.status = status;
    for (const auto & h : stream->response->headers) {
        if (!stays(h.first)) {
            res.set_header(h.first, h.second);
        }
    }
    const std::string type = stream->response->get_header_value("Content-Type");
    if (type.empty()) {
        return;
    }
    res.set_chunked_content_provider(type, [client, stream](size_t, httplib::DataSink & sink) {
        char          buf[PROXY_CHUNK];
        const ssize_t n = stream->read(buf, sizeof(buf));
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            sink.done();
            return true;
        }
        return sink.write(buf, (size_t) n);
    });
}
