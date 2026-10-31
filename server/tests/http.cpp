// http.cpp: the laws of the admission of a request, run as the build ends,
// against the server listening on loopback: with no key, a client without
// Origin and the page of the server pass, another origin is refused; once a
// key is set, every request needs it as a bearer token, whatever its Origin,
// on every route behind the key

#include "engine/engine.h"
#include "httplib.h"
#include "lib/listener.h"
#include "server/server.conf.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] http_test broken: %s\n", what.c_str());
    }
}

static const char * const LIST = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";

int main() {
    // a free port: httplib closes a bound socket only once it listens
    int port = 0;
    {
        httplib::Server probe;
        port = probe.bind_to_any_port("127.0.0.1");
        std::thread probing([&] { probe.listen_after_bind(); });
        probe.wait_until_ready();
        probe.stop();
        probing.join();
    }

    const fs::path root = fs::temp_directory_path() / ("http_test." + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    std::ofstream(root / "server.conf") << "set server listen-port " << port << "\n";
    engine_factory(std::string(reinterpret_cast<const char *>(server_server_conf), server_server_conf_len));
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

    listener_stop();
    listening.join();
    fs::remove_all(root);

    printf("[Laws] http_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
