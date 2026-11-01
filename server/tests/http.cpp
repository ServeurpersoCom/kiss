// http.cpp: the laws of the admission of a request, run as the build ends,
// against the server listening on loopback: with no key, a client without
// Origin and the page of the server pass, another origin is refused; once a
// key is set, every request needs it as a bearer token, whatever its Origin,
// on every route behind the key

#include "engine/engine.h"
#include "factory.h"
#include "httplib.h"
#include "lib/listener.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] http_test broken: %s\n", what.c_str());
    }
}

// a socket listening on [::1]:port that no other socket binds, as another
// process holds an address: exclusive on Windows, without SO_REUSEPORT on
// POSIX; -1 when the machine has no IPv6 loopback
static long long hold(int port) {
    const auto s = socket(AF_INET6, SOCK_STREAM, 0);
#if defined(_WIN32)
    const int one = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&one), sizeof(one));
#endif
    sockaddr_in6 at{};
    at.sin6_family = AF_INET6;
    at.sin6_port   = htons((unsigned short) port);
    at.sin6_addr   = in6addr_loopback;
    if (bind(s, reinterpret_cast<sockaddr *>(&at), sizeof(at)) != 0 || listen(s, 1) != 0) {
#if defined(_WIN32)
        closesocket(s);
#else
        close(s);
#endif
        return -1;
    }
    return (long long) s;
}

static void release(long long s) {
#if defined(_WIN32)
    closesocket((SOCKET) s);
#else
    close((int) s);
#endif
}

static const char * const LIST = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";

int main() {
    const fs::path root = fs::temp_directory_path() / ("http_test." + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const int port = laws_factory();
    engine_start((root / "server.conf").u8string(), "test");
    if (engine_get("server listen-port") != std::to_string(port)) {
        fs::remove_all(root);
        printf("[Laws] http_test broken: the server takes the free port %d\n", port);
        return 1;
    }

    std::thread listening([] { listener_run(); });
    for (int bound = 0; bound != port;) {
        std::string host;
        listener_address(host, bound);
        std::this_thread::yield();
    }

    const std::string self = "http://127.0.0.1:" + std::to_string(port);
    httplib::Client   client("127.0.0.1", port);

    // the status of a POST of path with these headers
    const auto status = [&](const std::string & path, const httplib::Headers & headers) {
        const auto res = client.Post(path, headers, LIST, "application/json");
        return res ? res->status : -1;
    };

    law(status("/mcp", {}) == 200, "with no key, a client without Origin passes");
    law(status("/mcp",
               {
                   { "Origin", self }
    }) == 200,
        "with no key, the page of the server passes");
    law(status("/mcp",
               {
                   { "Origin", "https://evil.example" }
    }) == 401,
        "with no key, another origin is refused");

    law(engine_run("set server key sesame", ENGINE_ADMIN).ok, "the administrator sets the key");

    law(status("/mcp", {}) == 401, "with a key, a client without Origin on loopback needs it");
    law(status("/mcp",
               {
                   { "Origin", self }
    }) == 401,
        "with a key, the page of the server needs it");
    law(status("/mcp",
               {
                   { "Authorization", "Bearer wrong" }
    }) == 401,
        "with a key, a wrong key is refused");
    law(status("/mcp",
               {
                   { "Authorization", "sesame" }
    }) == 401,
        "with a key, a key outside Bearer is refused");
    law(status("/mcp",
               {
                   { "Authorization", "Bearer sesame" }
    }) == 200,
        "with a key, the key passes");
    law(status("/mcp",
               {
                   { "Origin",        "https://elsewhere.example" },
                   { "Authorization", "Bearer sesame"             }
    }) == 200,
        "with a key, the key passes from any origin");
    law(status("/conversations/pin", {}) == 401, "with a key, the conversations need it");
    law(status("/proxy/any", {}) == 401, "with a key, the proxied servers need it");

    // the startup-config reads whole while the server listens on its port,
    // another address on that port being the server's own
    std::ofstream(root / "server.conf") << "set endpoints url e http://127.0.0.1:9/v1\nset server key sesame\n"
                                        << "set server listen-address 0.0.0.0\n";
    law(engine_run("show startup-config", ENGINE_ADMIN).text ==
            "set endpoints url e http://127.0.0.1:9/v1\n! server key is set\nset server listen-address 0.0.0.0",
        "the startup-config reads whole while the server listens on its port");

    // a move to an address another process holds is refused whole, the
    // server listening and keeping its key, a machine without IPv6 loopback
    // leaving the law out
    engine_run("set server key sesame", ENGINE_ADMIN);
    const long long held = hold(port);
    if (held >= 0) {
        const engine_result moved = engine_run("set server listen-address ::1\nno server key", ENGINE_ADMIN);
        law(!moved.ok && moved.text.find("cannot listen on ::1 port") != std::string::npos,
            "a move to an address another process holds is refused: " + moved.text);
        law(engine_get("server key") && status("/mcp",
                                               {
                                                   { "Authorization", "Bearer sesame" }
        }) == 200,
            "the server listens on with its key");
        release(held);
    }

    listener_stop();
    listening.join();
    fs::remove_all(root);

    printf("[Laws] http_test: %zu laws, %zu broken\n", g_laws, g_fails);
    engine_exit(g_fails ? 1 : 0);
}
