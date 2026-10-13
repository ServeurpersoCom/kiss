// erase.cpp: erase running-config, erase startup-config

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"

// IOS erase: a whole configuration gone at once, once the user says yes when
// an MCP client asks: the running-config back to the factory configuration,
// the startup-config gone so the server starts on it
static step_t compile_erase(const words_t & args) {
    if (args.empty()) {
        throw incomplete();
    }
    if (args.size() > 1) {
        fail(beyond({ "erase", args[0] }, words_t(args.begin() + 1, args.end())));
    }
    const std::string name = choose(args[0], { RUNNING_CONFIG, STARTUP_CONFIG }, "configuration");
    return [name](context & ctx) {
        if (!engine_confirmed(ctx, "erase " + name, "erase " + name)) {
            return std::string();
        }
        if (name == RUNNING_CONFIG) {
            ctx.draft.clear();
        } else {
            ctx.erase = true;
        }
        return std::string();
    };
}

static words_t next_erase(const words_t & args) {
    return args.empty() ? words_t{ RUNNING_CONFIG, STARTUP_CONFIG } : words_t{};
}

static command make_erase() {
    return { { "erase" }, false, compile_erase, next_erase };
}

ENGINE_COMMAND(make_erase);
