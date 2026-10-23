// main.cpp: the KiSS server: one binary per machine that serves the page, its
// site configuration and the MCP of the machine
//
// The server starts on its startup configuration over the defaults it embeds,
// reads its conversations, serves its site configuration to the page, then
// listens; a terminal it runs in the foreground of opens the console, anything
// else runs it as a daemon. Each file is server.conf, client.conf and
// conversations of the directory it starts in, or the path an option gives.

#include "engine/engine.h"
#include "lib/console.h"
#include "lib/http.h"
#include "lib/library.h"
#include "lib/listener.h"
#include "lib/log.h"
#include "lib/prompts.h"
#include "lib/utf8.h"
#include "server/server.conf.hpp"
#include "version.h"

#include <csignal>
#include <cstdio>
#include <string>

static void print_usage(const char * prog) {
    fprintf(stderr, "KiSS %s\n\n", KISS_VERSION);
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Files:\n"
            "  --server-conf <file>   Startup configuration the server runs and write saves\n"
            "                         (default: ./server.conf)\n"
            "  --client-conf <file>   Site configuration the page reads at /client.conf\n"
            "                         (default: ./client.conf, else the one it embeds)\n"
            "  --conversations <dir>  Directory holding the conversations, an <id>.json each\n"
            "                         (default: ./conversations)\n"
            "\n"
            "Everything else is configuration: show running-config all lists it on the\n"
            "console, set changes it live, write saves it to the startup configuration.\n",
            prog);
}

static void on_signal(int) {
    listener_stop();
}

int main(int argc, char ** argv) {
    utf8_init(&argc, &argv);

    std::string startup       = "server.conf";
    std::string site          = "client.conf";
    std::string conversations = "conversations";

    for (int i = 1; i < argc; i++) {
        const std::string arg       = argv[i];
        const bool        has_value = i + 1 < argc;

        if (arg == "--server-conf" && has_value) {
            startup = argv[++i];
        } else if (arg == "--client-conf" && has_value) {
            site = argv[++i];
        } else if (arg == "--conversations" && has_value) {
            conversations = argv[++i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    log_info("Server", "%s, %s", ENGINE_SYSTEM, engine_component("", KISS_VERSION).c_str());

    const std::string missing = prompts_check();
    if (!missing.empty()) {
        log_error("Server", "%s", missing.c_str());
        return 1;
    }

    const std::string refused =
        engine_factory(std::string(reinterpret_cast<const char *>(server_server_conf), server_server_conf_len));
    if (!refused.empty()) {
        log_error("Server", "The factory configuration refuses:\n%s", refused.c_str());
        return 1;
    }
    http_open(site);
    engine_start(startup, KISS_VERSION);
    library_open(conversations);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#if !defined(_WIN32)
    // a script that closes its stdin early fails the write, not the server
    signal(SIGPIPE, SIG_IGN);
#endif

    console_start();
    return listener_run() ? 0 : 1;
}
