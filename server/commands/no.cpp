// no.cpp: no <module> <key> [item], no <module> <item>

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static words_t next_key_or_item(const words_t & args) {
    return schema_next(reader(running_layer()), args, false, true);
}

// no server port names a key, no shells zsh an item of a collection, an item
// one the draft holds, a prefix being enough; a key takes the line show
// running-config writes, whatever value follows it
static step_t compile_no(const words_t & args) {
    if (args.size() == 2) {
        const std::string module = schema_module(args[0]);
        if (collection(module) && schema_key(module, args[1]).empty()) {
            const std::string word = args[1];
            return [module, word](context & ctx) {
                const std::string item = choose(word, layer_names(ctx.draft, module), module);
                for (auto it = ctx.draft.begin(); it != ctx.draft.end();) {
                    const auto [key, name] = unstore(it->first);
                    it = name == item && starts_with(key, module + " ") ? ctx.draft.erase(it) : std::next(it);
                }
                return std::string();
            };
        }
    }
    const key_words kw = schema_read(args);
    return [kw](context & ctx) {
        const std::string item = kw.item.empty() ? "" : choose(kw.item, layer_names(ctx.draft, kw.module), kw.module);
        ctx.draft.erase(stored(kw.key, item));
        return std::string();
    };
}

static command make_no() {
    return { { "no" }, true, compile_no, next_key_or_item };
}

ENGINE_COMMAND(make_no);
