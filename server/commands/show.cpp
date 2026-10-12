// show.cpp: show <module> [key|item] [item]

#include "commands/show.h"

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static words_t effective_lines(const engine_module & m,
                               const reader &        config,
                               const std::string &   only_key,
                               const std::string &   only_item,
                               bool                  named_items) {
    words_t out;
    for (const engine_key & k : m.keys) {
        const std::string key = m.name + " " + k.name;
        if (!only_key.empty() && key != only_key) {
            continue;
        }
        if (k.named != named_items) {
            continue;
        }
        const auto value = config.get(key, only_item);
        if (value) {
            out.push_back(show_line(stored(key, only_item), *value));
        } else if (!only_key.empty() && (!only_item.empty() || !k.named)) {
            out.push_back(comment(stored(key, only_item) + " is not set"));
        }
    }
    return out;
}

std::string show_module(const reader &      config,
                        const std::string & module,
                        const std::string & key,
                        const std::string & item) {
    const engine_module & m = *find_module(module);
    words_t               out;
    if (item.empty()) {
        out = effective_lines(m, config, key, "", false);
        for (const std::string & n : config.names(module)) {
            const words_t lines = effective_lines(m, config, key, n, true);
            out.insert(out.end(), lines.begin(), lines.end());
            if (key.empty() && m.notes) {
                const words_t notes = m.notes(config, n);
                out.insert(out.end(), notes.begin(), notes.end());
            }
        }
    } else {
        out = effective_lines(m, config, key, item, true);
        if (key.empty() && m.notes) {
            const words_t notes = m.notes(config, item);
            out.insert(out.end(), notes.begin(), notes.end());
        }
    }
    if (out.empty()) {
        return comment("no " + module + (item.empty() ? "" : " " + item) + " yet");
    }
    return join(out, "\n");
}

static words_t next_key_or_item(const words_t & args) {
    return schema_next(reader(running_layer()), args, false, true);
}

// show server, show server port: the word after a collection names a key when
// it names one, else an item, one the draft holds, a prefix being enough
static step_t compile_show(const words_t & args) {
    if (args.empty()) {
        throw incomplete();
    }
    const std::string module = schema_module(args[0]);
    std::string       key, item;
    if (args.size() > 1) {
        const std::string word = schema_key(module, args[1]);
        if (word.empty()) {
            if (!collection(module)) {
                fail("unknown key \"" + module + " " + args[1] + "\"");
            }
            if (args.size() > 2) {
                fail(beyond({ "show", module, args[1] }, words_t(args.begin() + 2, args.end())));
            }
            item = args[1];
        } else {
            key                = module + " " + word;
            const size_t words = find_key(key)->named ? 3 : 2;
            if (args.size() > words) {
                fail(beyond({ "show", key }, words_t(args.begin() + (long) words, args.end())));
            }
            if (args.size() == 3) {
                item = args[2];
            }
        }
    }
    return [module, key, item](context & ctx) {
        const reader config(ctx.draft);
        return show_module(config, module, key, item.empty() ? "" : choose(item, config.names(module), module));
    };
}

static command make_show() {
    return { { "show" }, true, compile_show, next_key_or_item };
}

ENGINE_COMMAND(make_show);
