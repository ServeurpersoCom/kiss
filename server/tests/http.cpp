// http.cpp: the laws of the admission of a request, run as the build ends,
// against the server listening on loopback: with no key, a client without
// Origin and the page of the server pass, another origin is refused; once a
// key is set, every request needs it as a bearer token, whatever its Origin,
// on every route behind the key

#include "engine/engine.h"
#include "factory.h"
#include "httplib.h"
#include "lib/listener.h"
#include "lib/quit.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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

// a socket listening on 127.0.0.1 that accepts no connection, its port one
// the system picks
static long long muted() {
    const auto  s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in at{};
    at.sin_family      = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, reinterpret_cast<sockaddr *>(&at), sizeof(at));
    listen(s, 8);
    return (long long) s;
}

static int bound_port(long long s) {
    sockaddr_in at{};
    socklen_t   size = sizeof(at);
#if defined(_WIN32)
    getsockname((SOCKET) s, reinterpret_cast<sockaddr *>(&at), &size);
#else
    getsockname((int) s, reinterpret_cast<sockaddr *>(&at), &size);
#endif
    return ntohs(at.sin_port);
}

// the connection s accepts next
static long long accepted(long long s) {
#if defined(_WIN32)
    return (long long) accept((SOCKET) s, nullptr, nullptr);
#else
    return (long long) accept((int) s, nullptr, nullptr);
#endif
}

// whether the peer of s ends the connection within three seconds, whatever
// it sends first
static bool ended(long long s) {
#if defined(_WIN32)
    const DWORD wait = 3000;
    setsockopt((SOCKET) s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&wait), sizeof(wait));
#else
    const timeval wait{ 3, 0 };
    setsockopt((int) s, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
#endif
    for (;;) {
        char buf[4096];
#if defined(_WIN32)
        const int n = recv((SOCKET) s, buf, sizeof(buf), 0);
#else
        const ssize_t n = recv((int) s, buf, sizeof(buf), 0);
#endif
        if (n <= 0) {
            return n == 0;
        }
    }
}

// a connection to 127.0.0.1:port, -1 when it does not open
static long long connected(int port) {
    const auto  s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in at{};
    at.sin_family      = AF_INET;
    at.sin_port        = htons((unsigned short) port);
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr *>(&at), sizeof(at)) != 0) {
        release((long long) s);
        return -1;
    }
    return (long long) s;
}

static void sent(long long s, const std::string & text) {
#if defined(_WIN32)
    send((SOCKET) s, text.data(), (int) text.size(), 0);
#else
    send((int) s, text.data(), text.size(), 0);
#endif
}

// the status line of the answer read on s, empty when none comes
static std::string answered(long long s) {
    char buf[256] = {};
#if defined(_WIN32)
    recv((SOCKET) s, buf, sizeof(buf) - 1, 0);
#else
    recv((int) s, buf, sizeof(buf) - 1, 0);
#endif
    const std::string text = buf;
    return text.substr(0, text.find("\r\n"));
}

// what s receives within three seconds, until it holds the head of an answer
// and the first two bytes of a frame after it
static std::string heard(long long s) {
#if defined(_WIN32)
    const DWORD wait = 3000;
    setsockopt((SOCKET) s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&wait), sizeof(wait));
#else
    const timeval wait{ 3, 0 };
    setsockopt((int) s, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
#endif
    std::string text;
    for (;;) {
        const size_t head = text.find("\r\n\r\n");
        if (head != std::string::npos && text.size() >= head + 6) {
            return text;
        }
        char buf[4096];
#if defined(_WIN32)
        const int n = recv((SOCKET) s, buf, sizeof(buf), 0);
#else
        const ssize_t n = recv((int) s, buf, sizeof(buf), 0);
#endif
        if (n <= 0) {
            return text;
        }
        text.append(buf, (size_t) n);
    }
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
    // server listening where it did with its key, from an address of its own
    // and from a wildcard; a machine without IPv6 loopback leaves the laws out
    engine_run("set server key sesame", ENGINE_ADMIN);
    const httplib::Headers key = {
        { "Authorization", "Bearer sesame" }
    };
    const long long held = hold(port);
    if (held >= 0) {
        for (const std::string from : { "127.0.0.1", "0.0.0.0" }) {
            law(engine_run("set server listen-address " + from, ENGINE_ADMIN).ok && status("/mcp", key) == 200,
                "the server moves to " + from);
            const engine_result moved = engine_run("set server listen-address ::1\nno server key", ENGINE_ADMIN);
            law(!moved.ok && moved.text.find("cannot listen on ::1 port") != std::string::npos,
                "a move from " + from + " to an address another process holds is refused: " + moved.text);
            for (int i = 0; i < 100 && status("/mcp", key) != 200; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            law(engine_get("server listen-address") == std::optional<std::string>(from) && engine_get("server key") &&
                    status("/mcp", key) == 200,
                "the server listens on " + from + " with its key");
        }
        engine_run("set server listen-address 127.0.0.1", ENGINE_ADMIN);
        release(held);
    }

    // a request the wildcard took before a move to loopback that drops the
    // key is checked as one of the wildcard, which needs the key, while one
    // the loopback takes passes without it
    law(engine_run("set server listen-address 0.0.0.0", ENGINE_ADMIN).ok && status("/mcp", key) == 200,
        "the server moves to 0.0.0.0");
    const long long pending = connected(port);
    sent(pending, "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(strlen(LIST)) +
                      "\r\n\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    law(engine_run("set server listen-address 127.0.0.1\nno server key", ENGINE_ADMIN).ok,
        "the server moves to 127.0.0.1 and drops the key");
    sent(pending, LIST);
    const std::string late = answered(pending);
    release(pending);
    law(late == "HTTP/1.1 401 Unauthorized", "a request the wildcard took needs the key it had: " + late);
    for (int i = 0; i < 100 && status("/mcp", {}) != 200; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(status("/mcp", {}) == 200, "a request the loopback takes passes without the key");

    // a proxied request whose client leaves while the server answers
    // nothing lets the request out go: the server sees its connection end
    const long long quiet = muted();
    engine_run("set proxies url quiet http://127.0.0.1:" + std::to_string(bound_port(quiet)), ENGINE_ADMIN);
    {
        httplib::Client leaving("127.0.0.1", port);
        leaving.set_read_timeout(0, 300000);
        leaving.Get("/proxy/quiet/");
    }
    const long long upstream = accepted(quiet);
    law(ended(upstream), "a proxied request whose client leaves while the server answers nothing lets it go");
    release(upstream);
    release(quiet);

    // a proxied body the server cuts short of the length it announced is
    // refused as a server that cannot be reached, never passed on as whole
    const long long cutting = muted();
    engine_run("set proxies url cut http://127.0.0.1:" + std::to_string(bound_port(cutting)), ENGINE_ADMIN);
    int             cut_status = 0;
    std::thread     cut_asking([&] {
        httplib::Client asking("127.0.0.1", port);
        const auto      res = asking.Get("/proxy/cut/");
        cut_status          = res ? res->status : -1;
    });
    const long long cut = accepted(cutting);
    answered(cut);
    sent(cut, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n{}");
    release(cut);
    cut_asking.join();
    release(cutting);
    law(cut_status == 502, "a proxied body the server cuts short is refused: " + std::to_string(cut_status));

    // a stream of events relayed to a client that leaves lets the request out
    // go: the server sees it end, however the answer of the client ends
    std::atomic<bool> streaming{ false };
    std::atomic<bool> stream_ended{ false };
    httplib::Server   events_server;
    events_server.Get("/events", [&](const httplib::Request &, httplib::Response & res) {
        res.set_chunked_content_provider(
            "text/event-stream",
            [&](size_t, httplib::DataSink & sink) {
                streaming               = true;
                const std::string event = "data: x\n\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return sink.write(event.data(), event.size());
            },
            [&](bool) { stream_ended = true; });
    });
    std::atomic<size_t> flooded{ 0 };
    std::atomic<bool>   flooding{ true };
    events_server.Get("/flood", [&](const httplib::Request &, httplib::Response & res) {
        res.set_chunked_content_provider("text/event-stream", [&](size_t, httplib::DataSink & sink) {
            const std::string events = [] {
                std::string all;
                while (all.size() < 65536) {
                    all += "data: " + std::string(250, 'x') + "\n\n";
                }
                return all;
            }();
            if (!flooding || !sink.write(events.data(), events.size())) {
                return false;
            }
            flooded += events.size();
            return true;
        });
    });
    const int   events_port = events_server.bind_to_any_port("127.0.0.1");
    std::thread events_listening([&] { events_server.listen_after_bind(); });
    events_server.wait_until_ready();
    engine_run("set proxies url sse http://127.0.0.1:" + std::to_string(events_port), ENGINE_ADMIN);
    const long long reader = connected(port);
    sent(reader, "GET /proxy/sse/events HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n\r\n");
    heard(reader);
    release(reader);
    for (int i = 0; i < 300 && !stream_ended; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(streaming && stream_ended, "a stream of events relayed to a client that leaves lets the request out go");

    // a stream of events relayed to a client that reads nothing holds a
    // bounded part of it: the request out waits for the client
    const long long idle = connected(port);
    sent(idle, "GET /proxy/sse/flood HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n\r\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const size_t relayed = flooded;
    law(relayed > 0 && relayed < (32u << 20),
        "a stream relayed to a client that reads nothing holds a bounded part of it: " + std::to_string(relayed) +
            " bytes");
    flooding = false;
    release(idle);
    events_server.stop();
    events_listening.join();

    // a page whose headers come in pieces far apart is heard whole: the
    // head of the frame, then its payload a while later
    const std::string events  = "ws://127.0.0.1:" + std::to_string(port) + "/conversations/events";
    const std::string headers = R"({"Authorization":""})";
    const long long   slow    = connected(port);
    sent(slow, "GET /conversations/events HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                   "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    std::string frame = "\x81";
    frame += (char) (0x80 | headers.size());
    frame.append(4, '\0');
    sent(slow, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sent(slow, headers);
    const std::string got  = heard(slow);
    const size_t      head = got.find("\r\n\r\n");
    law(got.find(" 101 ") != std::string::npos && head != std::string::npos && got.size() > head + 4 &&
            (unsigned char) got[head + 4] == 0x81,
        "a page whose headers come in pieces far apart follows the conversations");
    release(slow);

    // the process ends within a tick of the streams and the wait for a Close
    // that is never answered while a page opens the stream of the
    // conversations and sends nothing, while another sends without end, and
    // while a proxied server takes the connection and never answers: a
    // socket that listens and accepts none
    httplib::ws::WebSocketClient silent(events);
    law(silent.connect() && silent.is_open(), "a page opens the stream of the conversations");
    httplib::ws::WebSocketClient chatty(events);
    law(chatty.connect() && chatty.send(headers), "a page follows the conversations");
    std::atomic<bool> chatting{ true };
    std::thread       chatter([&] {
        while (chatting && chatty.send(std::string("x"))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const long long   mute = muted();
    engine_run("set proxies url mute http://127.0.0.1:" + std::to_string(bound_port(mute)), ENGINE_ADMIN);
    int         proxied = 0;
    std::thread asking([&] {
        httplib::Client waiting("127.0.0.1", port);
        const auto      res = waiting.Get("/proxy/mute/");
        proxied             = res ? res->status : -1;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    listener_stop();
    listening.join();
    const auto ending = std::chrono::steady_clock::now();
    quit_all();
    law(std::chrono::steady_clock::now() - ending < std::chrono::seconds(3),
        "the process ends while pages send nothing or without end and a proxied server never answers");
    chatting = false;
    chatter.join();
    asking.join();
    law(proxied == 503, "the request it proxied answers 503: " + std::to_string(proxied));
    release(mute);
    fs::remove_all(root);

    printf("[Laws] http_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
