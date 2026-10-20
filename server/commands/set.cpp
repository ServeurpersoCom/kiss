// set.cpp: set <module> <key> [item] <value>, a set line

#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static words_t next_set(const words_t & args) {
    return schema_next(reader(running_layer()), args, true, false);
}

// set server listen-port 9000. A model may write the lines of an MCP client, so a
// secret never comes in one: set proxies key deepwiki, without its value, asks
// the user to type it, and the batch runs again with what they typed
static step_t compile_set(const words_t & args) {
    const key_words   kw   = schema_read(args);
    const engine_kind kind = kw.def->kind;
    const std::string s    = stored(kw.key, kw.item);
    if (kw.rest.empty() && kind != ENGINE_SECRET) {
        throw incomplete();
    }
    if (kw.rest.empty()) {
        const std::string key = kw.key;
        return [s, key](context & ctx) {
            if (ctx.role == ENGINE_ADMIN) {
                fail(s + " misses its value");
            }
            const auto typed = ctx.typed.find(s);
            if (typed == ctx.typed.end()) {
                ctx.asks.push_back(s);
                return std::string();
            }
            ctx.draft[s] = schema_parse(key, typed->second);
            return comment("the user typed " + s);
        };
    }
    const std::string value = schema_parse(kw.key, join(kw.rest, " "));
    return [s, value, kind](context & ctx) {
        if (kind == ENGINE_SECRET && ctx.role == ENGINE_MCP) {
            fail(s + " is a secret, a typed value would reach the model -> set " + s + " alone asks the user");
        }
        ctx.draft[s] = value;
        return std::string();
    };
}

static command make_set() {
    return { { "set" }, true, compile_set, next_set };
}

ENGINE_COMMAND(make_set);
