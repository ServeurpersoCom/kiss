// proxy.cpp: the relay of the MCP servers the mcp module names
//
// Each request is built anew from the one it relays, never spliced: its
// method, its body, the headers of the first list below and every Mcp-Param-
// header, then the headers and the key of the route. The key of this server
// never leaves it. The answer keeps its status and the headers of the second
// list, its body streamed as it arrives. A server that refuses the key of the
// route answers 502 here, so the client never takes it for a refusal of the
// key of this server; one that redirects answers 502 too, its key going to no
// address the route does not name.

#include "proxy.h"

#include "engine/engine.h"
#include "engine/text.h"
#include "log.h"

#include <memory>
#include <optional>

static const char * const REQUEST_HEADERS[]  = { "Accept",     "Mcp-Session-Id", "MCP-Protocol-Version",
                                                 "Mcp-Method", "Mcp-Name",       "Last-Event-ID" };
static const char * const RESPONSE_HEADERS[] = { "Mcp-Session-Id", "MCP-Protocol-Version" };

// the headers a client mirrors from the arguments of a call, whatever follows
static const char * const PARAM_HEADER = "mcp-param-";

// the bytes of the body read and passed on at a time
static const size_t PROXY_CHUNK = 16384;

// a route as one same configuration holds it
struct route_conf {
    std::string url;
    std::string key;
    std::string headers;
    int         timeout;
};

static std::optional<route_conf> route_of(const std::string & name) {
    std::optional<route_conf> r;
    engine_with([&](const engine_reader & config) {
        const auto url = config.get("mcp url", name);
        if (url) {
            r = route_conf{ *url, config.get("mcp key", name).value_or(""),
                            config.get("mcp headers", name).value_or(""),
                            std::stoi(config.get("mcp timeout", name).value_or("0")) };
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

void proxy(const httplib::Request & req, httplib::Response & res, const std::string & route) {
    const auto r = route_of(route);
    if (!r) {
        refuse(res, 404, "no mcp route \"" + route + "\"");
        return;
    }
    std::string host, path;
    split_url(r->url, host, path);
    const auto client = std::make_shared<httplib::Client>(host);
    if (!client->is_valid()) {
        refuse(res, 502, r->url + " cannot be reached: a bad port, or https on a build without TLS");
        return;
    }
    client->set_connection_timeout(r->timeout, 0);
    client->set_read_timeout(r->timeout, 0);

    httplib::Headers headers;
    for (const char * h : REQUEST_HEADERS) {
        if (req.has_header(h)) {
            headers.emplace(h, req.get_header_value(h));
        }
    }
    for (const auto & h : req.headers) {
        if (starts_with(lower(h.first), PARAM_HEADER)) {
            headers.emplace(h.first, h.second);
        }
    }
    for (const std::string & pair : split(r->headers, ';')) {
        const size_t colon = pair.find(':');
        if (colon != std::string::npos) {
            headers.emplace(trim(pair.substr(0, colon)), trim(pair.substr(colon + 1)));
        }
    }
    if (!r->key.empty()) {
        headers.emplace("Authorization", "Bearer " + r->key);
    }

    const auto stream = std::make_shared<httplib::ClientImpl::StreamHandle>(
        client->open_stream(req.method, path, {}, headers, req.body, req.get_header_value("Content-Type")));
    if (!stream->is_valid()) {
        const std::string why = r->url + " cannot be reached: " + httplib::to_string(stream->error);
        log_error("MCP", "Route mcp/%s: %s", route.c_str(), why.c_str());
        refuse(res, 502, why);
        return;
    }
    const int status = stream->response->status;
    if (status == httplib::StatusCode::Unauthorized_401 || status == httplib::StatusCode::Forbidden_403) {
        const std::string why = r->url + " answered " + std::to_string(status) + " -> set mcp key " + route;
        log_error("MCP", "Route mcp/%s: %s", route.c_str(), why.c_str());
        refuse(res, 502, why);
        return;
    }
    if (status >= httplib::StatusCode::MultipleChoices_300 && status < httplib::StatusCode::BadRequest_400) {
        const std::string why = r->url + " answered " + std::to_string(status) + " to " +
                                stream->response->get_header_value("Location") + " -> set mcp url " + route;
        log_error("MCP", "Route mcp/%s: %s", route.c_str(), why.c_str());
        refuse(res, 502, why);
        return;
    }
    log_debug("MCP", "Route mcp/%s: %s %d", route.c_str(), req.method.c_str(), status);
    res.status = status;
    for (const char * h : RESPONSE_HEADERS) {
        if (stream->response->has_header(h)) {
            res.set_header(h, stream->response->get_header_value(h));
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
