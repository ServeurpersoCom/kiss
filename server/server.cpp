// server.cpp: the KiSS server: one binary per machine that serves the page,
// its site configuration and the MCP of the machine
//
// This file is the host: the command line, the startup-config, the HTTP
// routes and the checks every request passes. The configuration lives in
// engine.cpp and its modules, the MCP in mcp.cpp, the tools in tools.cpp.

#include "server.h"

#include "defaults.conf.hpp"
#include "engine.h"
#include "httplib.h"
#include "index.html.gz.hpp"
#include "kiss.conf.hpp"
#include "mcp.h"
#include "utf8.h"
#include "version.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#    include <ws2tcpip.h>
#else
#    include <netdb.h>
#    include <sys/socket.h>
#    include <unistd.h>
#    if defined(__APPLE__)
#        include <mach-o/dyld.h>
#    endif
#endif

static const char * const STARTUP_FILE = "server.conf";

// the listener, the address it holds, and whether the server stops for good
static std::mutex        g_listener_mutex;
static httplib::Server * g_listener = nullptr;
static std::string       g_bound_host;
static int               g_bound_port = 0;
static std::atomic<bool> g_quit{ false };

bool server_loopback(const std::string & host) {
    return host == "localhost" || host == "::1" || host.compare(0, 4, "127.") == 0;
}

bool server_bindable(const std::string & host, int port, std::string & why) {
    {
        std::lock_guard<std::mutex> lock(g_listener_mutex);
        if (g_listener && host == g_bound_host && port == g_bound_port) {
            return true;
        }
    }
    addrinfo hints            = {};
    hints.ai_family           = AF_UNSPEC;
    hints.ai_socktype         = SOCK_STREAM;
    hints.ai_flags            = AI_PASSIVE;
    addrinfo *        found   = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || !found) {
        why = "no such address";
        return false;
    }
#if defined(_WIN32)
    SOCKET     s  = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    const bool ok = s != INVALID_SOCKET && bind(s, found->ai_addr, (int) found->ai_addrlen) == 0;
    if (s != INVALID_SOCKET) {
        closesocket(s);
    }
#else
    const int s   = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    const int one = 1;
    if (s >= 0) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    const bool ok = s >= 0 && bind(s, found->ai_addr, found->ai_addrlen) == 0;
    if (s >= 0) {
        close(s);
    }
#endif
    freeaddrinfo(found);
    if (!ok) {
        why = "the address is taken or not of this machine";
    }
    return ok;
}

void server_listen(const std::string & host, int port) {
    std::lock_guard<std::mutex> lock(g_listener_mutex);
    if (g_listener && (host != g_bound_host || port != g_bound_port)) {
        g_listener->stop();
    }
}

static void on_signal(int) {
    g_quit = true;
    std::lock_guard<std::mutex> lock(g_listener_mutex);
    if (g_listener) {
        g_listener->stop();
    }
}

// the directory of the binary, where server.conf lives by default
static std::filesystem::path binary_directory() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD) buf.size());
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    _NSGetExecutablePath(&buf[0], &size);
    return std::filesystem::canonical(buf.c_str()).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}

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
    if (!server_loopback(g_bound_host)) {
        return true;
    }
    std::string name, port;
    host_header(req.get_header_value("Host"), name, port);
    const std::string bound = std::to_string(g_bound_port);
    return (server_loopback(name) || name == g_bound_host) && (port == bound || (port.empty() && bound == "80"));
}

static bool same_key(const std::string & a, const std::string & b) {
    unsigned char diff = (unsigned char) (a.size() != b.size());
    for (size_t i = 0; i < a.size() || i < b.size(); i++) {
        diff |= (unsigned char) ((i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0));
    }
    return diff == 0;
}

// who may call /mcp: on loopback, a client without Origin, a command line, or
// the page this server serves; anyone else, and everyone off loopback, with
// the key as a bearer token; why not, otherwise
static bool authorized(const httplib::Request & req, std::string & why) {
    const std::string origin = req.get_header_value("Origin");
    const std::string self   = "http://" + req.get_header_value("Host");
    if (server_loopback(g_bound_host) && (origin.empty() || origin == self)) {
        return true;
    }
    const auto key = engine_get("server key");
    if (!key) {
        why = "another origin calls this server with the key, and server key is not set";
        return false;
    }
    const std::string bearer = req.get_header_value("Authorization");
    if (bearer.compare(0, 7, "Bearer ") != 0 || !same_key(bearer.substr(7), *key)) {
        why = "the key is missing or wrong";
        return false;
    }
    return true;
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

static void routes(httplib::Server & svr) {
    svr.set_socket_options([](socket_t sock) {
        int one = 1;
#if defined(_WIN32)
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    });

    svr.set_pre_routing_handler([](const httplib::Request & req, httplib::Response & res) {
        if (!host_allowed(req)) {
            res.status = 403;
            res.set_content("Host names no loopback address of this server\n", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // the page, gzipped at build time; the browser inflates it
    svr.Get("/", [](const httplib::Request & req, httplib::Response & res) {
        if (req.get_header_value("Accept-Encoding").find("gzip") == std::string::npos) {
            res.status = 406;
            res.set_content("the page needs a browser that reads gzip\n", "text/plain");
            return;
        }
        res.set_header("Content-Encoding", "gzip");
        res.set_content(reinterpret_cast<const char *>(index_html_gz), index_html_gz_len, "text/html; charset=utf-8");
    });

    // the site configuration of the page: its backend at a relative URL
    svr.Get("/kiss.conf", [](const httplib::Request &, httplib::Response & res) {
        res.set_content(reinterpret_cast<const char *>(kiss_conf), kiss_conf_len, "text/plain; charset=utf-8");
    });

    svr.Options("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = 204;
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                       "Authorization, Content-Type, Accept, MCP-Protocol-Version, Mcp-Method, Mcp-Name");
    });

    svr.Post("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        std::string why;
        if (!authorized(req, why)) {
            res.status = 401;
            res.set_header("WWW-Authenticate", "Bearer");
            res.set_content(why + "\n", "text/plain");
            return;
        }
        mcp_post(req, res, KISS_VERSION);
    });

    // the server keeps no session: no stream to open, none to close
    const auto refuse = [](const httplib::Request & req, httplib::Response & res) {
        cors(req, res);
        res.status = 405;
        res.set_header("Allow", "POST");
    };
    svr.Get("/mcp", refuse);
    svr.Delete("/mcp", refuse);
}

static void print_usage(const char * prog) {
    fprintf(stderr, "KiSS server %s\n\n", KISS_VERSION);
    fprintf(stderr,
            "usage: %s [-c server.conf] [-s line]... [--show]\n\n"
            "  -c path   the startup-config, server.conf next to the binary by default\n"
            "  -s line   a CLI line run at start, over the startup-config\n"
            "  --show    prints every key with its value on this machine, then exits\n",
            prog);
}

int main(int argc, char ** argv) {
    utf8_init(&argc, &argv);

    std::string              startup = (binary_directory() / STARTUP_FILE).u8string();
    std::vector<std::string> lines;
    bool                     show = false;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            startup = argv[++i];
        } else if (arg == "-s" && i + 1 < argc) {
            lines.push_back(argv[++i]);
        } else if (arg == "--show") {
            show = true;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    const std::string refused =
        engine_defaults(std::string(reinterpret_cast<const char *>(defaults_conf), defaults_conf_len));
    if (!refused.empty()) {
        fprintf(stderr, "[server] FATAL: the embedded defaults:\n%s\n", refused.c_str());
        return 1;
    }
    const engine_result started = engine_start(startup, KISS_VERSION);
    if (!started.ok) {
        fprintf(stderr, "[server] FATAL: %s\n", started.text.c_str());
        return 1;
    }
    if (!lines.empty()) {
        std::string text;
        for (const std::string & l : lines) {
            text += l + "\n";
        }
        const engine_result r = engine_run(text, ENGINE_ADMIN);
        fprintf(stderr, "%s\n", r.text.c_str());
        if (!r.ok) {
            return 1;
        }
    }
    if (show) {
        printf("%s\n", engine_run("show running-config all", ENGINE_ADMIN).text.c_str());
        return 0;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "[server] KiSS server %s on %s, startup-config %s\n", KISS_VERSION, engine_platform(),
            startup.c_str());

    while (!g_quit) {
        const std::string host = *engine_get("server host");
        const int         port = std::stoi(*engine_get("server port"));
        httplib::Server   svr;
        routes(svr);
        if (!svr.bind_to_port(host, port)) {
            fprintf(stderr, "[server] FATAL: cannot listen on %s port %d\n", host.c_str(), port);
            return 1;
        }
        {
            std::lock_guard<std::mutex> lock(g_listener_mutex);
            g_listener   = &svr;
            g_bound_host = host;
            g_bound_port = port;
        }
        fprintf(stderr, "[server] listening on http://%s:%d\n", host.c_str(), port);
        svr.listen_after_bind();
        std::lock_guard<std::mutex> lock(g_listener_mutex);
        g_listener = nullptr;
    }
    return 0;
}
