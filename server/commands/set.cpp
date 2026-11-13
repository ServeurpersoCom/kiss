// set.cpp: set <module> <key> [item] <value>, a set line

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

#include <algorithm>

static words_t next_set(const completing & c) {
    return schema_next(c.config, c.args, true, false, c.known);
}

// an item a line names: one the draft already holds, or one the sources of its
// module serve, as the page admits it; a module whose sources the batch did
// not ask, as at the start, takes any name
static void admit(const context & ctx, const std::string & module, const std::string & item) {
    const auto it = ctx.sourced.find(module);
    if (item.empty() || it == ctx.sourced.end()) {
        return;
    }
    const words_t set = reader(ctx.draft).names(module);
    if (std::find(set.begin(), set.end(), item) != set.end()) {
        return;
    }
    words_t known, failed;
    for (const engine_group & g : it->second) {
        known.insert(known.end(), g.names.begin(), g.names.end());
        if (!g.error.empty()) {
            failed.push_back(g.group + ": " + g.error);
        }
    }
    if (std::find(known.begin(), known.end(), item) != known.end()) {
        return;
    }
    fail("unknown " + module + " \"" + item + "\"" +
         (failed.empty() ? did_you_mean(item, known) : ", " + join(failed, ", ")));
}

// set server listen-port 9000, an item admitted first. A model may write the
// lines of an MCP client, so a secret never comes in one: set proxies key
// deepwiki, without its value, asks the user to type it, and the batch runs
// again with what they typed
static step_t compile_set(const words_t & args) {
    const key_words   kw   = schema_read(args);
    const engine_kind kind = kw.def->kind;
    const std::string s    = stored(kw.key, kw.item);
    if (kw.rest.empty() && kind != ENGINE_SECRET) {
        throw incomplete();
    }
    const std::string module = kw.module;
    const std::string item   = kw.item;
    if (kw.rest.empty()) {
        const std::string key = kw.key;
        return [s, key, module, item](context & ctx) {
            admit(ctx, module, item);
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
    return [s, value, kind, module, item](context & ctx) {
        admit(ctx, module, item);
        if (kind == ENGINE_SECRET && ctx.role == ENGINE_MCP) {
            fail(s + " is a secret, a typed value would reach the model -> set " + s + " alone asks the user");
        }
        ctx.draft[s] = value;
        return std::string();
    };
}

static command make_set() {
    return { { SET }, true, compile_set, next_set };
}

ENGINE_COMMAND(make_set);
