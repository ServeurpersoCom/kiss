// set.cpp: set <module> <key> [item] <value>, a set line

#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static words_t next_set(const words_t & args) {
    return schema_next(reader(running_layer()), args, true, false);
}

// set server port 9000: a line without a platform on a key the draft holds for
// this platform changes it there
static step_t compile_set(const std::string & platform, const words_t & args) {
    const key_words kw = schema_read(args);
    if (kw.rest.empty()) {
        throw incomplete();
    }
    const std::string value = schema_parse(kw.key, join(kw.rest, " "));
    const std::string s     = stored(kw.key, kw.item);
    const engine_kind kind  = kw.def->kind;
    return [platform, s, value, kind](context & ctx) {
        if (kind == ENGINE_SECRET && ctx.role == ENGINE_MCP) {
            fail(s + " is written in server.conf or on the command line, a model would read it here");
        }
        if (platform.empty() && ctx.draft.own.count(s)) {
            ctx.draft.own[s] = value;
        } else {
            scope(ctx.draft, platform)[s] = value;
        }
        return std::string();
    };
}

static command make_set() {
    return { { "set" }, true, true, compile_set, next_set };
}

ENGINE_COMMAND(make_set);
