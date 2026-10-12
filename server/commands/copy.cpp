// copy.cpp: copy between the running-config and the startup-config

#include "engine/archive.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static std::string configuration(const std::string & word) {
    for (const char * n : { RUNNING_CONFIG, STARTUP_CONFIG }) {
        if (starts_with(n, lower(word))) {
            return n;
        }
    }
    return word;
}

// copy running-config startup-config writes server.conf once the batch holds,
// copy startup-config running-config reads it back into the draft
static step_t compile_copy(const words_t & args) {
    if (args.size() < 2) {
        throw incomplete();
    }
    if (args.size() > 2) {
        fail(beyond({ "copy", args[0], args[1] }, words_t(args.begin() + 2, args.end())));
    }
    const std::string from = configuration(args[0]);
    const std::string to   = configuration(args[1]);
    for (const std::string & n : { from, to }) {
        if (n != RUNNING_CONFIG && n != STARTUP_CONFIG) {
            fail("unknown configuration \"" + n + "\", use " + RUNNING_CONFIG + " " + STARTUP_CONFIG);
        }
    }
    if (from == to) {
        fail("a copy goes from one configuration to another");
    }
    return [to](context & ctx) {
        if (to == RUNNING_CONFIG) {
            ctx.draft = startup_layer();
            return std::string();
        }
        ctx.save = true;
        return std::string();
    };
}

static words_t next_copy(const words_t & args) {
    return args.size() < 2 ? words_t{ RUNNING_CONFIG, STARTUP_CONFIG } : words_t{};
}

static command make_copy() {
    return { { "copy" }, false, compile_copy, next_copy };
}

ENGINE_COMMAND(make_copy);
