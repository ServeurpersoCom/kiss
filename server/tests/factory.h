#pragma once
// factory.h: the factory configuration a law runs on: the one the server
// embeds, its listen-port moved to a port free on this machine, so a server
// listening on the port of the factory beside the build, a production one,
// changes no law, and an erase of the running configuration keeps that port

#include "engine/engine.h"
#include "httplib.h"
#include "server/server.conf.hpp"

#include <string>
#include <thread>

// a port free on this machine: httplib closes a bound socket only once it
// listens
inline int laws_free_port() {
    httplib::Server probe;
    const int       port = probe.bind_to_any_port("127.0.0.1");
    std::thread     probing([&] { probe.listen_after_bind(); });
    probe.wait_until_ready();
    probe.stop();
    probing.join();
    return port;
}

// the factory configuration of the server on a free port, which it answers
inline int laws_factory() {
    const int port = laws_free_port();
    engine_factory(std::string(reinterpret_cast<const char *>(server_server_conf), server_server_conf_len) +
                   "set server listen-port " + std::to_string(port) + "\n");
    return port;
}
