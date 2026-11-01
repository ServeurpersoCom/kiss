// show.cpp: show <module> [key|item] [item]

#include "commands/show.h"

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

#include <algorithm>
#include <functional>

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

// the lines of groups, each body after its header: a failed group holds its
// error on the header, an empty one shows nothing, and one without a name, the
// one source of its module, has no header
static words_t grouped(const std::vector<engine_group> &                   groups,
                       const std::function<words_t(const std::string &)> & body) {
    words_t out;
    for (const engine_group & g : groups) {
        if (!g.error.empty()) {
            out.push_back(comment(g.group + " " + g.error));
            continue;
        }
        words_t lines;
        for (const std::string & n : g.names) {
            const words_t b = body(n);
            lines.insert(lines.end(), b.begin(), b.end());
        }
        if (!g.group.empty() && !lines.empty()) {
            out.push_back(comment(g.group));
        }
        out.insert(out.end(), lines.begin(), lines.end());
    }
    return out;
}

std::string show_module(const reader &                    config,
                        const std::string &               module,
                        const std::string &               key,
                        const std::string &               item,
                        const std::vector<engine_group> * sources) {
    const engine_module & m        = *find_module(module);
    const auto            lines_of = [&](const std::string & n) {
        words_t out = effective_lines(m, config, key, n, true);
        if (key.empty() && m.notes) {
            const words_t notes = m.notes(config, n);
            out.insert(out.end(), notes.begin(), notes.end());
        }
        return out;
    };
    if (!item.empty()) {
        const words_t out = lines_of(item);
        return out.empty() ? comment("no " + module + " " + item + " yet") : join(out, "\n");
    }
    words_t       out = effective_lines(m, config, key, "", false);
    const words_t set = config.names(module);
    if (!sources) {
        for (const std::string & n : set) {
            const words_t lines = lines_of(n);
            out.insert(out.end(), lines.begin(), lines.end());
        }
    } else {
        // the items go by group, who serves them, those unavailable last,
        // whatever keeps them away; an item without a setting shows by its
        // name alone, unless a key is named
        std::vector<engine_group> groups = *sources;
        engine_group              unavailable{ "unavailable", {}, "" };
        for (const std::string & n : set) {
            bool listed = false;
            for (const engine_group & g : groups) {
                listed = listed || std::find(g.names.begin(), g.names.end(), n) != g.names.end();
            }
            if (!listed) {
                unavailable.names.push_back(n);
            }
        }
        groups.push_back(unavailable);
        const words_t lines = grouped(groups, [&](const std::string & n) {
            const words_t shown = lines_of(n);
            return shown.empty() && key.empty() ? words_t{ comment(n) } : shown;
        });
        out.insert(out.end(), lines.begin(), lines.end());
    }
    return out.empty() ? comment("no " + module + " yet") : join(out, "\n");
}

static words_t next_key_or_item(const completing & c) {
    return schema_next(c.config, c.args, false, true, c.known);
}

// show server, show server listen-port: the word after a collection names a key as
// schema_key_after reads it, else an item, one the draft holds, a prefix being
// enough
static step_t compile_show(const words_t & args) {
    if (args.empty()) {
        throw incomplete();
    }
    const std::string module = schema_module(args[0]);
    std::string       key, item;
    if (args.size() > 1) {
        const std::string word = schema_key_after(module, args[1], args.size() > 2);
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
    // a module with sources lists their items, and an item names one set or
    // one they serve
    return [module, key, item](context & ctx) {
        const reader config(ctx.draft);
        const auto   it      = ctx.sourced.find(module);
        const auto * sources = it == ctx.sourced.end() ? nullptr : &it->second;
        words_t      known   = config.names(module);
        if (sources) {
            for (const engine_group & g : *sources) {
                known.insert(known.end(), g.names.begin(), g.names.end());
            }
        }
        return show_module(config, module, key, item.empty() ? "" : choose(item, known, module), sources);
    };
}

static command make_show() {
    return { { "show" }, true, compile_show, next_key_or_item };
}

ENGINE_COMMAND(make_show);
