// no.cpp: no <module> <key> [item], no <module> <item>

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

#include <set>

static words_t next_key_or_item(const words_t & args) {
    return schema_next(reader(running_layer()), args, false, true);
}

// the value the draft sets for a key leaves it, the factory configuration
// giving its own again, as IOS goes back to the default; a key the draft sets
// nothing for that the factory configuration holds is taken back by a line of
// its own, so the reset value applies, as IOS keeps no ip http server
static void take_back(layer & draft, const std::string & s) {
    const auto it = draft.find(s);
    if ((it == draft.end() || !it->second) && factory_holds(s)) {
        draft[s] = std::nullopt;
    } else {
        draft.erase(s);
    }
}

// no server listen-port names a key, no shells zsh an item of a collection, an
// item one the configuration holds through its layers, a prefix being enough;
// a key takes the line show running-config writes, whatever value follows it
static step_t compile_no(const words_t & args) {
    if (args.size() == 2) {
        const std::string module = schema_module(args[0]);
        if (collection(module) && schema_key_after(module, args[1], false).empty()) {
            const std::string word = args[1];
            return [module, word](context & ctx) {
                const std::string     item = choose(word, reader(ctx.draft).names(module), module);
                std::set<std::string> keys;
                const layer &         draft   = ctx.draft;
                const layer &         factory = factory_layer();
                for (const layer * l : { &draft, &factory }) {
                    for (const auto & kv : *l) {
                        const auto [key, name] = unstore(kv.first);
                        if (name == item && starts_with(key, module + " ")) {
                            keys.insert(kv.first);
                        }
                    }
                }
                for (const std::string & s : keys) {
                    take_back(ctx.draft, s);
                }
                return std::string();
            };
        }
    }
    const key_words kw = schema_read(args);
    return [kw](context & ctx) {
        const std::string item = kw.item.empty() ? "" : choose(kw.item, reader(ctx.draft).names(kw.module), kw.module);
        take_back(ctx.draft, stored(kw.key, item));
        return std::string();
    };
}

static command make_no() {
    return { { "no" }, true, compile_no, next_key_or_item };
}

ENGINE_COMMAND(make_no);
