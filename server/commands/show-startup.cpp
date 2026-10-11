// show-startup.cpp: show startup-config

#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

// what the server starts with: server.conf as it reads now
static step_t compile_show_startup(const std::string &, const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", STARTUP_CONFIG }, args));
    }
    return [](context &) {
        const words_t lines = layer_lines(startup_layer(), false);
        return lines.empty() ? comment("every key is at its default") : join(lines, "\n");
    };
}

static command make_show_startup() {
    return {
        { "show", STARTUP_CONFIG },
        false, false, compile_show_startup, nullptr
    };
}

ENGINE_COMMAND(make_show_startup);
