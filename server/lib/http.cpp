// http.cpp: the routes of the server: the page, its site configuration, the
// MCP, the servers it proxies and the conversations, behind the checks of
// Host, Origin and the key

#include "lib/http.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "lib/json.h"
#include "lib/listener.h"
#include "lib/log.h"
#include "lib/proxy.h"
#include "lib/sync.h"
#include "mcp/mcp.h"
#include "server/client.conf.hpp"
#include "tools/public/index.html.gz.hpp"
#include "version.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

// the file of the site configuration the server gives the page it serves
static std::string g_site;

// the path of a proxied server, its route name then the rest of the path
// captured
static const char * const ROUTE_PATH = R"(/proxy/([a-z0-9_-]+)(/.*)?)";

// the stream of the conversations, a WebSocket pinged every PING_SECONDS, and
// the path of an action, captured
// what opens the header that carries the key of the server
static const char * const BEARER_PREFIX = "Bearer ";

static const char * const EVENTS_PATH  = "/conversations/events";
static const time_t       PING_SECONDS = 15;
static const std::string  ACTION_PATH  = "/conversations/(" + sync_actions() + ")";

// the name and the port of a Host header: example.com:8089, [::1]:8089
static void host_header(const std::string & value, std::string & name, std::string & port) {
    size_t colon = std::string::npos;
    if (!value.empty() && value[0] == '[') {
        const size_t close = value.find(']');
        name               = value.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        if (close != std::string::npos && close + 1 < value.size() && value[close + 1] == ':') {
            colon = close + 1;
        }
    } else {
        colon = value.rfind(':');
        name  = value.substr(0, colon);
    }
    port = colon == std::string::npos ? "" : value.substr(colon + 1);
}

// the address a server listens on, which the checks of its requests read, a
// request it took before a move checked as the listener that took it
struct bound_t {
    std::string host;
    int         port = 0;
};

// on a loopback address, Host must name loopback and the port: a page of
// another site that resolves its own name to 127.0.0.1 never gets through
static bool host_allowed(const httplib::Request & req, const bound_t & on) {
    if (!listener_loopback(on.host)) {
        return true;
    }
    std::string name, port;
    host_header(req.get_header_value("Host"), name, port);
    const std::string bound = std::to_string(on.port);
    return (listener_loopback(name) || name == on.host) && (port == bound || (port.empty() && bound == "80"));
}

// who may call the server: once a key is set, whoever sends it as a bearer
// token, whatever the address and the origin; with no key, on loopback, a
// client without Origin, a command line, or the page this server serves, so
// no page of another site runs a tool through fetch; why not, otherwise
static bool authorized(const httplib::Request & req, const bound_t & on, std::string & why) {
    if (engine_get("server key")) {
        const std::string bearer = req.get_header_value("Authorization");
        if (bearer.compare(0, strlen(BEARER_PREFIX), BEARER_PREFIX) != 0 ||
            !keychain_admits(bearer.substr(strlen(BEARER_PREFIX)))) {
            why = "the key is missing or wrong";
            return false;
        }
        return true;
    }
    const std::string origin = req.get_header_value("Origin");
    const std::string self   = "http://" + req.get_header_value("Host");
    if (listener_loopback(on.host) && (origin.empty() || origin == self)) {
        return true;
    }
    why = "server key is not set, so another origin is refused";
    return false;
}

// whether a request passes, else refused with why, the page told to send the
// key
static bool admitted(const httplib::Request & req, const bound_t & on, httplib::Response & res) {
    std::string why;
    if (authorized(req, on, why)) {
        return true;
    }
    log_error("HTTP", "Refused %s: %s", req.remote_addr.c_str(), why.c_str());
    res.status = httplib::StatusCode::Unauthorized_401;
    res.set_header("WWW-Authenticate", "Bearer");
    res.set_content(why + "\n", TEXT_TYPE);
    return false;
}

// a page served elsewhere reads the answers of /mcp, the key keeping out who
// has none
static void cors(const httplib::Request & req, httplib::Response & res) {
    const std::string origin = req.get_header_value("Origin");
    if (!origin.empty()) {
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Vary", "Origin");
    }
}

void http_open(const std::string & site) {
    g_site = site;
}

void http_routes(httplib::Server & server, const std::string & host, int port) {
    const bound_t on = { host, port };
    server.set_socket_options([](socket_t sock) {
        int one = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
    });

    server.set_pre_routing_handler([on](const httplib::Request & req, httplib::Response & res) {
        if (!host_allowed(req, on)) {
            log_error("HTTP", "Refused %s: Host %s names no loopback address of this server", req.remote_addr.c_str(),
                      req.get_header_value("Host").c_str());
            res.status = httplib::StatusCode::Forbidden_403;
            res.set_content("Host names no loopback address of this server\n", TEXT_TYPE);
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // the page, gzipped at build time; the browser inflates it
    server.Get("/", [](const httplib::Request & req, httplib::Response & res) {
        if (req.get_header_value("Accept-Encoding").find("gzip") == std::string::npos) {
            res.status = httplib::StatusCode::NotAcceptable_406;
            res.set_content("the page needs a browser that reads gzip\n", TEXT_TYPE);
            return;
        }
        res.set_header("Content-Encoding", "gzip");
        res.set_content(reinterpret_cast<const char *>(tools_public_index_html_gz), tools_public_index_html_gz_len,
                        "text/html; charset=utf-8");
    });

    // the site configuration of the page: its file, else the one the server
    // embeds, its backend at a relative URL
    server.Get("/client.conf", [](const httplib::Request &, httplib::Response & res) {
        std::ifstream site(std::filesystem::u8path(g_site), std::ios::binary);
        if (site) {
            std::ostringstream text;
            text << site.rdbuf();
            res.set_content(text.str(), "text/plain; charset=utf-8");
            return;
        }
        res.set_content(reinterpret_cast<const char *>(server_client_conf), server_client_conf_len,
                        "text/plain; charset=utf-8");
    });

    server.Options("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = httplib::StatusCode::NoContent_204;
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                       "Authorization, Content-Type, Accept, MCP-Protocol-Version, Mcp-Method, Mcp-Name");
    });

    server.Post("/mcp", [on](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        if (admitted(req, on, res)) {
            mcp_post(req, res, KISS_VERSION);
        }
    });

    // the server keeps no session: no stream to open, none to close
    const auto refuse = [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = httplib::StatusCode::MethodNotAllowed_405;
        res.set_header("Allow", "POST");
    };
    server.Get("/mcp", refuse);
    server.Delete("/mcp", refuse);

    // the proxy knows no protocol, so a page sends a proxied server the headers
    // it asks to, and reads every header the server answers, a session too
    server.Options(ROUTE_PATH, [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = httplib::StatusCode::NoContent_204;
        res.set_header("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", req.get_header_value("Access-Control-Request-Headers"));
    });
    const auto forward = [on](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.set_header("Access-Control-Expose-Headers", "*");
        if (admitted(req, on, res)) {
            proxy(req, res, req.matches[1], req.matches[2]);
        }
    };
    server.Post(ROUTE_PATH, forward);
    server.Get(ROUTE_PATH, forward);
    server.Delete(ROUTE_PATH, forward);

    // the conversations, beside the MCP: the stream a page follows them by,
    // and the actions of its user
    const auto preflight = [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = httplib::StatusCode::NoContent_204;
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    };
    server.Options(ACTION_PATH, preflight);
    // a browser sets no header on a WebSocket: its first message holds the
    // headers a request would carry, the key among them, as JSON; a page that
    // answers neither of two pings in a row is gone
    server.set_websocket_ping_interval(PING_SECONDS);
    server.set_websocket_max_missed_pongs(2);
    server.WebSocket(EVENTS_PATH, [on](const httplib::Request & req, httplib::ws::WebSocket & ws) {
        sync_events(req, ws, [&req, &on](const std::string & headers) {
            const json_doc   doc   = json_read(headers);
            yyjson_val *     key   = yyjson_obj_get(yyjson_doc_get_root(doc.get()), "Authorization");
            httplib::Request keyed = req;
            keyed.headers.erase("Authorization");
            keyed.set_header("Authorization", yyjson_is_str(key) ? yyjson_get_str(key) : "");
            std::string why;
            return authorized(keyed, on, why) ? "" : why;
        });
    });
    server.Post(ACTION_PATH, [on](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        if (admitted(req, on, res)) {
            sync_action(req, res, req.matches[1]);
        }
    });
}
