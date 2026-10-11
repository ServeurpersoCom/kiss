// no.cpp: no <module> <key> [item], no <module> <item>

#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static words_t next_key_or_item(const words_t & args) {
    return schema_next(reader(running_layer()), args, false, true);
}

// no server port names a key, no shells zsh an item of a collection; a key
// takes the line show running-config writes, whatever value follows it
static step_t compile_no(const std::string & platform, const words_t & args) {
    if (args.size() == 2) {
        const std::string module = schema_module(args[0]);
        if (collection(module) && schema_key(module, args[1]).empty()) {
            const std::string item = args[1];
            return [platform, module, item](context & ctx) {
                bool dropped = false;
                for (values_t * v : { &scope(ctx.draft, platform), &ctx.draft.own }) {
                    for (auto it = v->begin(); it != v->end();) {
                        const auto [key, name] = unstore(it->first);
                        if (name == item && starts_with(key, module + " ")) {
                            it      = v->erase(it);
                            dropped = true;
                        } else {
                            ++it;
                        }
                    }
                    if (!platform.empty()) {
                        break;
                    }
                }
                if (!dropped) {
                    fail("unknown " + module + " \"" + item + "\"");
                }
                return std::string();
            };
        }
    }
    const key_words   kw = schema_read(args);
    const std::string s  = stored(kw.key, kw.item);
    return [platform, s](context & ctx) {
        if (platform.empty() && ctx.draft.own.count(s)) {
            ctx.draft.own.erase(s);
        } else {
            scope(ctx.draft, platform).erase(s);
        }
        return std::string();
    };
}

static command make_no() {
    return { { "no" }, true, true, compile_no, next_key_or_item };
}

ENGINE_COMMAND(make_no);
