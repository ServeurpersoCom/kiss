// show-diff.cpp: show diff <config> <config>

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"

// from one configuration to another: the running-config as the batch leaves
// it, or the startup-config, server.conf as it reads now
static step_t compile_show_diff(const words_t & args) {
    if (args.size() < 2) {
        throw incomplete();
    }
    if (args.size() > 2) {
        fail(beyond({ "show", "diff", args[0], args[1] }, words_t(args.begin() + 2, args.end())));
    }
    const words_t     configs = { RUNNING_CONFIG, STARTUP_CONFIG };
    const std::string from    = choose(args[0], configs, "configuration");
    const std::string to      = choose(args[1], configs, "configuration");
    return [from, to](context & ctx) {
        const auto config = [&](const std::string & name) {
            return name == RUNNING_CONFIG ? ctx.draft : startup_layer();
        };
        const words_t lines = diff(config(from), config(to));
        return lines.empty() ? comment("no difference") : join(lines, "\n");
    };
}

static words_t next_show_diff(const words_t & args) {
    return args.size() < 2 ? words_t{ RUNNING_CONFIG, STARTUP_CONFIG } : words_t{};
}

static command make_show_diff() {
    return {
        { "show", "diff" },
        false, compile_show_diff, next_show_diff
    };
}

ENGINE_COMMAND(make_show_diff);
