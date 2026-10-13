// proxy.cpp: the proxy of the servers the proxy module names
//
// Each request is built anew from the one it proxies, never spliced: its method,
// the rest of its path and its query after the url of the route, its body and
// every header but those that stay on its side, then the headers of the route,
// which replace those of the same name, and its key, in the header its auth
// names. The key of this server never leaves it. The answer keeps its status and
// every header but those that stay on its side, each through the keychain of
// every secret of this server: a stream of events event by event as it arrives,
// any other body whole, JSON filtered as decoded and written again, any other
// text as it is written. A server that refuses the key of the route answers 502
// here, so the client never takes it for a refusal of the key of this server;
// one that redirects answers 502 too, its key going to no address the route
// does not name. The proxy knows no protocol: MCP and LLM endpoints pass alike.

#include "proxy.h"

#include "engine/engine.h"
#include "engine/keychain.h"
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
    "forwarded",
    "via",
    "x-real-ip",
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

// a header that stays on its side: one of the list, or one of a proxy, of a
// hop a reverse proxy tells, or of CORS, which each side writes for itself
static bool stays(const std::string & header) {
    const std::string h = lower(header);
    if (starts_with(h, "proxy-") || starts_with(h, "x-forwarded-") || starts_with(h, "access-control-")) {
        return true;
    }
    for (const char * x : SIDE_HEADERS) {
        if (h == x) {
            return true;
        }
    }
    return false;
}

// a route as one same configuration holds it: its url, the header that
// carries its key, its headers, its seconds, and every secret of that
// configuration, the key the request carries among them
struct route_conf {
    std::string url;
    bool        keyed = false;
    std::string header;
    std::string value;
    std::string headers;
    int         timeout = 0;
    words_t     secrets;
};

static std::optional<route_conf> route_of(const std::string & name) {
    std::optional<route_conf> r;
    engine_with([&](const engine_reader & config) {
        const auto url = config.get("proxy url", name);
        if (url) {
            r      = route_conf{};
            r->url = *url;
            r->keyed =
                credential(config, config.get("proxy auth", name).value_or(BEARER), "proxy", name, r->header, r->value);
            r->headers = config.get("proxy headers", name).value_or("");
            r->timeout = std::stoi(config.get("proxy timeout", name).value_or("0"));
            r->secrets = keychain_secrets(config);
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

// a refusal of a route, logged and answered 502, its text through the keychain
static void refuse(httplib::Response & res, const route_conf & r, const std::string & route, const std::string & why) {
    const std::string text = seal_text(why, r.secrets);
    log_error("Proxy", "Route proxy/%s: %s", route.c_str(), text.c_str());
    res.status = httplib::StatusCode::BadGateway_502;
    res.set_content(text + "\n", "text/plain");
}

void proxy(const httplib::Request & req, httplib::Response & res, const std::string & route, const std::string & rest) {
    const auto r = route_of(route);
    if (!r) {
        res.status = httplib::StatusCode::NotFound_404;
        res.set_content("no proxy route \"" + route + "\"\n", "text/plain");
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
        refuse(res, *r, route, r->url + " cannot be reached: a bad port, or https on a build without TLS");
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
    if (r->keyed) {
        headers.erase(r->header);
        headers.emplace(r->header, r->value);
    }

    const auto stream = std::make_shared<httplib::ClientImpl::StreamHandle>(
        client->open_stream(req.method, path, {}, headers, req.body, req.get_header_value("Content-Type")));
    if (!stream->is_valid()) {
        refuse(res, *r, route, r->url + " cannot be reached: " + httplib::to_string(stream->error));
        return;
    }
    const int status = stream->response->status;
    if (status == httplib::StatusCode::Unauthorized_401 || status == httplib::StatusCode::Forbidden_403) {
        refuse(res, *r, route, r->url + " answered " + std::to_string(status) + " -> set proxy key " + route);
        return;
    }
    if (status >= httplib::StatusCode::MultipleChoices_300 && status < httplib::StatusCode::BadRequest_400) {
        refuse(res, *r, route,
               r->url + " answered " + std::to_string(status) + " to " +
                   stream->response->get_header_value("Location") + " -> set proxy url " + route);
        return;
    }
    log_debug("Proxy", "Route proxy/%s: %s %s %d", route.c_str(), req.method.c_str(),
              (rest.empty() ? "/" : rest).c_str(), status);
    res.status            = status;
    const words_t secrets = r->secrets;
    for (const auto & h : stream->response->headers) {
        if (!stays(h.first)) {
            res.set_header(h.first, seal_text(h.second, secrets));
        }
    }
    const std::string type = stream->response->get_header_value("Content-Type");
    if (type.empty()) {
        return;
    }
    if (!starts_with(lower(type), "text/event-stream")) {
        std::string body;
        char        buf[PROXY_CHUNK];
        for (ssize_t n; (n = stream->read(buf, sizeof(buf))) > 0;) {
            body.append(buf, (size_t) n);
        }
        res.set_content(seal_body(body, secrets), type);
        return;
    }
    const auto events = std::make_shared<seal_events>(seal_events{ secrets, "" });
    res.set_chunked_content_provider(type, [client, stream, events](size_t, httplib::DataSink & sink) {
        char              buf[PROXY_CHUNK];
        const ssize_t     n   = stream->read(buf, sizeof(buf));
        const std::string out = n > 0 ? events->feed(buf, (size_t) n) : events->end();
        if (n < 0 || (!out.empty() && !sink.write(out.data(), out.size()))) {
            return false;
        }
        if (n == 0) {
            sink.done();
        }
        return true;
    });
}
