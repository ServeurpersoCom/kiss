// main.cpp: the KiSS server: one binary per machine that serves the page, its
// site configuration and the MCP of the machine
//
// The server starts on server.conf of the directory it runs in, over the
// defaults it embeds, then listens; a stdin that is a terminal opens the
// console, any other runs it as a daemon. It takes no argument.

#include "console.h"
#include "engine/engine.h"
#include "listener.h"
#include "log.h"
#include "server.conf.hpp"
#include "utf8.h"
#include "version.h"

#include <csignal>
#include <cstdio>
#include <string>

// the startup-config, in the directory the server runs in
static const char * const STARTUP_FILE = "server.conf";

static void on_signal(int) {
    listener_stop();
}

int main(int argc, char ** argv) {
    utf8_init(&argc, &argv);

    if (argc > 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }

    log_info("Server", "%s, %s", ENGINE_SYSTEM, engine_component("", KISS_VERSION).c_str());

    const std::string refused =
        engine_defaults(std::string(reinterpret_cast<const char *>(server_conf), server_conf_len));
    if (!refused.empty()) {
        log_info("Server", "The embedded defaults refuse:\n%s", refused.c_str());
        return 1;
    }
    const engine_result started = engine_start(STARTUP_FILE, KISS_VERSION);
    if (!started.ok) {
        log_info("Server", "Cannot start on %s:\n%s", STARTUP_FILE, started.text.c_str());
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#if !defined(_WIN32)
    // a script that closes its stdin early fails the write, not the server
    signal(SIGPIPE, SIG_IGN);
#endif

    console_start();
    return listener_run() ? 0 : 1;
}
