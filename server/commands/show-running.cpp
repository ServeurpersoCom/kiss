// show-running.cpp: show running-config [all]

#include "commands/show.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

// IOS show running-config: every line the running configuration holds, as the
// line that sets it, which copy writes to server.conf; all, every key of every
// module with its value on this platform, defaults included
static step_t compile_show_running(const std::string &, const words_t & args) {
    if (!args.empty() && !starts_with(ALL, lower(args[0]))) {
        fail(beyond({ "show", RUNNING_CONFIG }, args));
    }
    if (args.size() > 1) {
        fail(beyond({ "show", RUNNING_CONFIG, ALL }, words_t(args.begin() + 1, args.end())));
    }
    const bool every = args.size() == 1;
    return [every](context & ctx) {
        if (every) {
            words_t shown;
            for (const std::string & m : module_names()) {
                shown.push_back(show_module(reader(ctx.draft), m, "", ""));
            }
            return join(shown, "\n");
        }
        const words_t lines = layer_lines(ctx.draft, false);
        return lines.empty() ? comment("every key is at its default") : join(lines, "\n");
    };
}

static words_t next_show_running(const words_t & args) {
    return args.empty() ? words_t{ ALL } : words_t{};
}

static command make_show_running() {
    return {
        { "show", RUNNING_CONFIG },
        false, false, compile_show_running, next_show_running
    };
}

ENGINE_COMMAND(make_show_running);
