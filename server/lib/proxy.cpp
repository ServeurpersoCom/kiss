// proxy.cpp: the proxy of the servers the proxies module names
//
// Each request is built anew from the one it proxies, never spliced: its method,
// the rest of its path and its query after the url of the route, its body and
// every header but those that stay on its side, then the headers of the route,
// which replace those of the same name, and its key, in the header its auth-header
// names. The key of this server never leaves it. The answer keeps its status and
// every header but those that stay on its side, each through the keychain of
// every secret of this server: a stream of events event by event as it arrives,
// any other body whole, JSON filtered as decoded and written again, any other
// text as it is written. A route the configuration makes unusable answers 424
// here with the fix: a server that refuses the key of the route, so the client
// never takes it for a refusal of the key of this server nor for a server down
// for a while; one that redirects, its key going to no address the route does
// not name; an url no client of this build reaches. A server that cannot be
// reached answers 502. The proxy knows no protocol: MCP and LLM endpoints pass
// alike. The request out is sent on a thread of its own, which hands the head
// of its answer, then its body piece by piece as they come, to the answer of
// this server; it ends once its client leaves or the process ends.

#include "lib/proxy.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "engine/text.h"
#include "lib/api.h"
#include "lib/log.h"
#include "lib/quit.h"
#include "lib/remote.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

// a request out, which its thread fills and the answer of this server takes:
// the head of its answer once it came, the body that came and is not taken
// yet, whether it ended and why
struct relay_t {
    api_stop                stop;
    std::mutex              mutex;
    std::condition_variable moved;
    bool                    headed = false;
    httplib::Response       head;
    std::string             body;
    bool                    ended = false;
    httplib::Error          error = httplib::Error::Success;
};

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

// a route as one same configuration holds it: the server it names, and
// every secret of that configuration, the key the request carries among them
struct route_conf {
    remote  server;
    words_t secrets;
};

static std::optional<route_conf> route_of(const std::string & name) {
    std::optional<route_conf> r;
    engine_with([&](const engine_reader & config) {
        if (config.get("proxies url", name)) {
            r = route_conf{ remote_of(config, "proxies", name, BEARER), keychain_secrets(config) };
        }
    });
    return r;
}

// a refusal of a route, logged and answered with its status, its text through
// the keychain
static void refuse(httplib::Response & res,
                   const route_conf &  r,
                   const std::string & route,
                   httplib::StatusCode status,
                   const std::string & why) {
    const std::string text = seal_text(why, r.secrets);
    log_error("Proxy", "Route proxy/%s: %s", route.c_str(), text.c_str());
    res.status = status;
    res.set_content(text + "\n", "text/plain");
}

// sends out on the thread of the relay, through a client of its stop
static void relayed(const std::shared_ptr<relay_t> &         relay,
                    const std::shared_ptr<httplib::Client> & client,
                    httplib::Request                         out) {
    quit_thread([relay, client, out]() mutable {
        out.response_handler = [&relay](const httplib::Response & head) {
            std::lock_guard<std::mutex> lock(relay->mutex);
            relay->head   = head;
            relay->headed = true;
            relay->moved.notify_all();
            return true;
        };
        out.content_receiver = [&relay](const char * piece, size_t n, size_t, size_t) {
            std::lock_guard<std::mutex> lock(relay->mutex);
            relay->body.append(piece, n);
            relay->moved.notify_all();
            return true;
        };
        const httplib::Result       res = api_send(*client, out, relay->stop);
        std::lock_guard<std::mutex> lock(relay->mutex);
        relay->ended = true;
        relay->error = res.error();
        relay->moved.notify_all();
    });
}

void proxy(const httplib::Request & req, httplib::Response & res, const std::string & route, const std::string & rest) {
    const auto r = route_of(route);
    if (!r) {
        res.status = httplib::StatusCode::NotFound_404;
        res.set_content("no proxy route \"" + route + "\"\n", "text/plain");
        return;
    }
    const auto                       relay = std::make_shared<relay_t>();
    std::string                      base;
    std::shared_ptr<httplib::Client> client;
    try {
        client = api_client(r->server, relay->stop, base);
    } catch (const api_error & e) {
        if (relay->stop.stopped) {
            refuse(res, *r, route, httplib::StatusCode::ServiceUnavailable_503, "the server stops");
        } else {
            refuse(res, *r, route, httplib::StatusCode::FailedDependency_424, e.what());
        }
        return;
    }

    // the rest joins the url with one slash, whether the url ends with one or not
    const std::string head  = rest.empty() ? base : base.substr(0, base.find_last_not_of('/') + 1);
    const size_t      query = req.target.find('?');
    httplib::Request  out;
    out.method = req.method;
    out.path   = head + rest + (query == std::string::npos ? "" : req.target.substr(query));
    out.body   = req.body;
    for (const auto & h : req.headers) {
        if (!stays(h.first)) {
            out.headers.emplace(h.first, h.second);
        }
    }
    if (req.has_header("Content-Type")) {
        out.set_header("Content-Type", req.get_header_value("Content-Type"));
    }
    // a header of the route, its key among them, replaces those of the same
    // name the client sent
    const httplib::Headers own = remote_headers(r->server);
    for (const auto & h : own) {
        out.headers.erase(h.first);
    }
    for (const auto & h : own) {
        out.headers.emplace(h.first, h.second);
    }
    relayed(relay, client, out);
    std::unique_lock<std::mutex> lock(relay->mutex);
    relay->moved.wait(lock, [&] { return relay->headed || relay->ended; });
    if (!relay->headed) {
        refuse(res, *r, route, httplib::StatusCode::BadGateway_502,
               r->server.url + " cannot be reached: " + httplib::to_string(relay->error));
        return;
    }
    // an answer this server does not pass on lets the request out go
    const httplib::Response answer = relay->head;
    const int               status = answer.status;
    if (status == httplib::StatusCode::Unauthorized_401 || status == httplib::StatusCode::Forbidden_403) {
        relay->stop.stop();
        refuse(res, *r, route, httplib::StatusCode::FailedDependency_424,
               r->server.url + " answers " + std::to_string(status) + " -> set proxies key " + route);
        return;
    }
    if (status >= httplib::StatusCode::MultipleChoices_300 && status < httplib::StatusCode::BadRequest_400) {
        relay->stop.stop();
        refuse(res, *r, route, httplib::StatusCode::FailedDependency_424,
               r->server.url + " answers " + std::to_string(status) + " to " + answer.get_header_value("Location") +
                   " -> set proxies url " + route);
        return;
    }
    log_debug("Proxy", "Route proxy/%s: %s %s %d", route.c_str(), req.method.c_str(),
              (rest.empty() ? "/" : rest).c_str(), status);
    res.status            = status;
    const words_t secrets = r->secrets;
    for (const auto & h : answer.headers) {
        if (!stays(h.first)) {
            res.set_header(h.first, seal_text(h.second, secrets));
        }
    }
    const std::string type = answer.get_header_value("Content-Type");
    if (type.empty()) {
        relay->stop.stop();
        return;
    }
    if (!starts_with(lower(type), "text/event-stream")) {
        relay->moved.wait(lock, [&] { return relay->ended; });
        res.set_content(seal_body(relay->body, secrets), type);
        return;
    }
    lock.unlock();
    const auto events = std::make_shared<seal_events>(seal_events{ secrets, "" });
    res.set_chunked_content_provider(type, [relay, events](size_t, httplib::DataSink & sink) {
        std::string    piece;
        bool           ended = false;
        httplib::Error error = httplib::Error::Success;
        {
            std::unique_lock<std::mutex> held(relay->mutex);
            relay->moved.wait(held, [&] { return !relay->body.empty() || relay->ended; });
            piece.swap(relay->body);
            ended = relay->ended;
            error = relay->error;
        }
        // the events as they come, the client cut off as the server was when
        // the stream breaks, and the request out let go once the client left
        const std::string sent = piece.empty() ? "" : events->feed(piece.data(), piece.size());
        if (!sent.empty() && !sink.write(sent.data(), sent.size())) {
            relay->stop.stop();
            return false;
        }
        if (!ended) {
            return true;
        }
        if (error != httplib::Error::Success) {
            return false;
        }
        const std::string last = events->end();
        if (!last.empty() && !sink.write(last.data(), last.size())) {
            return false;
        }
        sink.done();
        return true;
    });
}
