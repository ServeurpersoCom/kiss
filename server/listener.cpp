// listener.cpp: the socket the server listens on

#include "listener.h"

#include "engine/engine.h"
#include "http.h"
#include "log.h"

#include <atomic>
#include <mutex>

#if defined(_WIN32)
#    include <ws2tcpip.h>
#else
#    include <netdb.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

static std::mutex        g_mutex;
static httplib::Server * g_server = nullptr;
static std::string       g_host;
static int               g_port = 0;
static std::atomic<bool> g_quit{ false };

bool listener_loopback(const std::string & host) {
    return host == "localhost" || host == "::1" || host.compare(0, 4, "127.") == 0;
}

bool listener_bindable(const std::string & host, int port, std::string & why) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_server && host == g_host && port == g_port) {
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

void listener_move(const std::string & host, int port) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_server && (host != g_host || port != g_port)) {
        g_server->stop();
    }
}

void listener_address(std::string & host, int & port) {
    std::lock_guard<std::mutex> lock(g_mutex);
    host = g_host;
    port = g_port;
}

void listener_stop() {
    g_quit = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_server) {
        g_server->stop();
    }
}

bool listener_run() {
    while (!g_quit) {
        const std::string host = *engine_get("server host");
        const int         port = std::stoi(*engine_get("server port"));
        httplib::Server   server;
        http_routes(server);
        if (!server.bind_to_port(host, port)) {
            log_info("Server", "Cannot listen on %s port %d", host.c_str(), port);
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_server = &server;
            g_host   = host;
            g_port   = port;
        }
        log_info("Server", "Listening on http://%s:%d", host.c_str(), port);
        server.listen_after_bind();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_server = nullptr;
    }
    return true;
}
