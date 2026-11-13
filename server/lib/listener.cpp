// listener.cpp: the socket the server listens on

#include "lib/listener.h"

#include "engine/engine.h"
#include "lib/http.h"
#include "lib/log.h"
#include "lib/quit.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

// a server listening on a thread of its own, and the address it listens on
struct served {
    std::shared_ptr<httplib::Server> server;
    std::string                      host;
    int                              port = 0;
};

// the server listening now
static std::mutex            g_mutex;
static served                g_served;
static std::atomic<bool>     g_quit{ false };
static std::atomic<unsigned> g_epoch{ 0 };

// the milliseconds between two looks at whether the listener stops
static const int TICK_MS = 100;

// localhost, or a loopback address as written: IPv4 in 127.0.0.0/8, or ::1
bool listener_loopback(const std::string & host) {
    in_addr  v4;
    in6_addr v6;
    if (host == "localhost") {
        return true;
    }
    if (inet_pton(AF_INET, host.c_str(), &v4) == 1) {
        return (ntohl(v4.s_addr) >> 24) == 127;
    }
    return inet_pton(AF_INET6, host.c_str(), &v6) == 1 && memcmp(&v6, &in6addr_loopback, sizeof(v6)) == 0;
}

// a server bound to host and port, its routes set, listening on a thread the
// process joins as it ends; none when the address does not bind
static served serve(const std::string & host, int port) {
    auto server = std::make_shared<httplib::Server>();
    http_routes(*server, host, port);
    if (!server->bind_to_port(host, port)) {
        return {};
    }
    log_info("Server", "Listening on http://%s:%d", host.c_str(), port);
    quit_thread([server] { server->listen_after_bind(); });
    return { server, host, port };
}

// stops a server once its thread listens, its socket closed on return; the
// thread ends with the requests it serves, among them maybe the batch that
// stops it
static void halt(served & s) {
    if (s.server) {
        s.server->wait_until_ready();
        s.server->stop();
        s = {};
    }
}

std::string listener_prepare(const std::string & host, int port) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_served.server || (host == g_served.host && port == g_served.port)) {
        return "";
    }
    // the new address binds beside the one served, which stops once it does;
    // one that overlaps it, a wildcard on either side, binds once the one
    // served let the port go, and the address it left binds again when the
    // new one does not, as the port was the server's an instant before
    served next = serve(host, port);
    if (!next.server) {
        const std::string from_host = g_served.host;
        const int         from_port = g_served.port;
        ++g_epoch;
        halt(g_served);
        next = serve(host, port);
        if (!next.server) {
            g_served = serve(from_host, from_port);
            if (!g_served.server) {
                log_error("Server", "Cannot listen on %s port %d again", from_host.c_str(), from_port);
            }
            return "cannot listen on " + host + " port " + std::to_string(port) +
                   ": the address is taken or not of this machine";
        }
    }
    ++g_epoch;
    halt(g_served);
    g_served = std::move(next);
    return "";
}

void listener_address(std::string & host, int & port) {
    std::lock_guard<std::mutex> lock(g_mutex);
    host = g_served.host;
    port = g_served.port;
}

unsigned listener_epoch() {
    return g_epoch;
}

void listener_stop() {
    g_quit = true;
    ++g_epoch;
}

bool listener_run() {
    const std::string host = *engine_get("server listen-address");
    const int         port = std::stoi(*engine_get("server listen-port"));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_served = serve(host, port);
        if (!g_served.server) {
            log_error("Server", "Cannot listen on %s port %d", host.c_str(), port);
            return false;
        }
    }
    // a signal may stop the listener, so the stop is a flag looked at here
    while (!g_quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    halt(g_served);
    return true;
}
