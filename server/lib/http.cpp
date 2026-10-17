// http.cpp: the routes of the server: the page, its site configuration, the
// MCP, the servers it proxies and the conversations, behind the checks of
// Host, Origin and the key

#include "lib/http.h"

#include "client.conf.hpp"
#include "engine/engine.h"
#include "engine/keychain.h"
#include "index.html.gz.hpp"
#include "lib/listener.h"
#include "lib/log.h"
#include "lib/proxy.h"
#include "lib/sync.h"
#include "mcp/mcp.h"
#include "version.h"

#include <fstream>
#include <sstream>

// the site configuration a directory gives the page it serves
static const char * const SITE_FILE      = "client.conf";
static const char * const RENDERING_FILE = "rendering.conf";

// the path of a proxied server, its route name then the rest of the path
// captured
static const char * const ROUTE_PATH = R"(/proxy/([a-z0-9_-]+)(/.*)?)";

// the stream of the conversations, and the path of an action, captured
static const char * const EVENTS_PATH = "/conversations/events";
static const std::string  ACTION_PATH = std::string("/conversations/(") + SYNC_ACTIONS + ")";

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

// on a loopback address, Host must name loopback and the port: a page of
// another site that resolves its own name to 127.0.0.1 never gets through
static bool host_allowed(const httplib::Request & req) {
    std::string bound_host;
    int         bound_port = 0;
    listener_address(bound_host, bound_port);
    if (!listener_loopback(bound_host)) {
        return true;
    }
    std::string name, port;
    host_header(req.get_header_value("Host"), name, port);
    const std::string bound = std::to_string(bound_port);
    return (listener_loopback(name) || name == bound_host) && (port == bound || (port.empty() && bound == "80"));
}

// who may call /mcp: on loopback, a client without Origin, a command line, or
// the page this server serves; anyone else, and everyone off loopback, with
// the key as a bearer token; why not, otherwise
static bool authorized(const httplib::Request & req, std::string & why) {
    std::string bound_host;
    int         bound_port = 0;
    listener_address(bound_host, bound_port);
    const std::string origin = req.get_header_value("Origin");
    const std::string self   = "http://" + req.get_header_value("Host");
    if (listener_loopback(bound_host) && (origin.empty() || origin == self)) {
        return true;
    }
    if (!engine_get("server key")) {
        why = "another origin calls this server with the key, and server key is not set";
        return false;
    }
    const std::string bearer = req.get_header_value("Authorization");
    if (bearer.compare(0, 7, "Bearer ") != 0 || !keychain_admits(bearer.substr(7))) {
        why = "the key is missing or wrong";
        return false;
    }
    return true;
}

// whether a request passes, else refused with why, the page told to send the
// key
static bool admitted(const httplib::Request & req, httplib::Response & res) {
    std::string why;
    if (authorized(req, why)) {
        return true;
    }
    log_error("HTTP", "Refused %s: %s", req.remote_addr.c_str(), why.c_str());
    res.status = 401;
    res.set_header("WWW-Authenticate", "Bearer");
    res.set_content(why + "\n", "text/plain");
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

void http_routes(httplib::Server & server) {
    server.set_socket_options([](socket_t sock) {
        int one = 1;
#if defined(_WIN32)
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    });

    server.set_pre_routing_handler([](const httplib::Request & req, httplib::Response & res) {
        if (!host_allowed(req)) {
            log_error("HTTP", "Refused %s: Host %s names no loopback address of this server", req.remote_addr.c_str(),
                      req.get_header_value("Host").c_str());
            res.status = 403;
            res.set_content("Host names no loopback address of this server\n", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // the page, gzipped at build time; the browser inflates it
    server.Get("/", [](const httplib::Request & req, httplib::Response & res) {
        if (req.get_header_value("Accept-Encoding").find("gzip") == std::string::npos) {
            res.status = 406;
            res.set_content("the page needs a browser that reads gzip\n", "text/plain");
            return;
        }
        res.set_header("Content-Encoding", "gzip");
        res.set_content(reinterpret_cast<const char *>(index_html_gz), index_html_gz_len, "text/html; charset=utf-8");
    });

    // the site configuration of the page: client.conf of the directory the
    // server runs in, else the one it embeds, its backend at a relative URL
    server.Get("/client.conf", [](const httplib::Request &, httplib::Response & res) {
        std::ifstream site(SITE_FILE, std::ios::binary);
        if (site) {
            std::ostringstream text;
            text << site.rdbuf();
            res.set_content(text.str(), "text/plain; charset=utf-8");
            return;
        }
        res.set_content(reinterpret_cast<const char *>(client_conf), client_conf_len, "text/plain; charset=utf-8");
    });

    // the site rendering of the page: rendering.conf of the directory the
    // server runs in, no line without it, the page keeping its own
    server.Get("/rendering.conf", [](const httplib::Request &, httplib::Response & res) {
        std::ifstream      site(RENDERING_FILE, std::ios::binary);
        std::ostringstream text;
        if (site) {
            text << site.rdbuf();
        }
        res.set_content(text.str(), "text/plain; charset=utf-8");
    });

    server.Options("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = 204;
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                       "Authorization, Content-Type, Accept, MCP-Protocol-Version, Mcp-Method, Mcp-Name");
    });

    server.Post("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        if (admitted(req, res)) {
            mcp_post(req, res, KISS_VERSION);
        }
    });

    // the server keeps no session: no stream to open, none to close
    const auto refuse = [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = 405;
        res.set_header("Allow", "POST");
    };
    server.Get("/mcp", refuse);
    server.Delete("/mcp", refuse);

    // the proxy knows no protocol, so a page sends a proxied server the headers
    // it asks to, and reads every header the server answers, a session too
    server.Options(ROUTE_PATH, [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = 204;
        res.set_header("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", req.get_header_value("Access-Control-Request-Headers"));
    });
    const auto forward = [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.set_header("Access-Control-Expose-Headers", "*");
        if (admitted(req, res)) {
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
        res.status = 204;
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    };
    server.Options(EVENTS_PATH, preflight);
    server.Options(ACTION_PATH, preflight);
    server.Get(EVENTS_PATH, [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        if (admitted(req, res)) {
            sync_events(req, res);
        }
    });
    server.Post(ACTION_PATH, [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        if (admitted(req, res)) {
            sync_action(req, res, req.matches[1]);
        }
    });
}
