// registry.cpp: the commands of the CLI and how a line resolves to one

#include "engine/registry.h"

#include "engine/near.h"
#include "engine/schema.h"

#include <algorithm>
#include <set>

// the commands, sorted by path once every file registered its own
static std::vector<command> & commands() {
    static std::vector<command> list;
    return list;
}

bool engine_command(command (*make)()) {
    std::vector<command> & list = commands();
    list.push_back(make());
    std::sort(list.begin(), list.end(),
              [](const command & a, const command & b) { return join(a.path, " ") < join(b.path, " "); });
    return true;
}

walk_t walk(const words_t & words) {
    walk_t w;
    for (const command & c : commands()) {
        w.pool.push_back(&c);
    }
    const words_t modules = module_names();
    size_t        i       = 0;
    for (; i < words.size(); i++) {
        const std::string            word = lower(words[i]);
        std::vector<const command *> next;
        std::set<std::string>        names;
        for (const command * c : w.pool) {
            if (c->path.size() > i && starts_with(c->path[i], word)) {
                next.push_back(c);
                names.insert(c->path[i]);
            }
        }
        if (names.empty()) {
            break;
        }
        words_t rivals;
        if (w.found && w.found->module && w.depth == i) {
            for (const std::string & m : modules) {
                if (starts_with(m, word)) {
                    rivals.push_back(m);
                }
            }
        }
        const bool exact = names.count(word) > 0;
        if (!exact && std::find(rivals.begin(), rivals.end(), word) != rivals.end()) {
            break;
        }
        std::string name = exact ? word : (names.size() == 1 && rivals.empty() ? *names.begin() : "");
        if (name.empty()) {
            words_t candidates(names.begin(), names.end());
            candidates.insert(candidates.end(), rivals.begin(), rivals.end());
            std::sort(candidates.begin(), candidates.end());
            w.consumed  = i;
            w.ambiguous = words[i];
            w.names     = candidates;
            return w;
        }
        w.pool.clear();
        for (const command * c : next) {
            if (c->path[i] == name) {
                w.pool.push_back(c);
            }
        }
        for (const command * c : w.pool) {
            if (c->path.size() == i + 1) {
                w.found = c;
                w.depth = i + 1;
            }
        }
    }
    w.consumed = i;
    return w;
}

std::pair<const command *, words_t> resolve(const words_t & words) {
    const walk_t w = walk(words);
    if (!w.ambiguous.empty()) {
        fail("ambiguous word \"" + w.ambiguous + "\": " + join(w.names, " "));
    }
    std::set<std::string> names_set;
    for (const command * c : w.pool) {
        if (c->path.size() > w.consumed) {
            names_set.insert(c->path[w.consumed]);
        }
    }
    const words_t     names(names_set.begin(), names_set.end());
    const std::string head = join(words_t(words.begin(), words.begin() + (long) w.consumed), " ");
    // after a command naming a module, a word that is neither a module nor a
    // longer command is a typo of either
    if (w.found && w.found->module && w.depth == w.consumed && !names.empty() && w.consumed < words.size()) {
        const std::string typed   = lower(words[w.consumed]);
        const words_t     modules = module_names();
        if (std::none_of(modules.begin(), modules.end(),
                         [&](const std::string & m) { return starts_with(m, typed); })) {
            words_t all = names;
            all.insert(all.end(), modules.begin(), modules.end());
            fail("unknown word \"" + words[w.consumed] + "\" after " + head + did_you_mean(words[w.consumed], all));
        }
    }
    if (w.found) {
        return { w.found, words_t(words.begin() + (long) w.depth, words.end()) };
    }
    if (w.consumed >= words.size()) {
        throw incomplete();
    }
    const std::string word = words[w.consumed];
    fail("unknown " + (w.consumed ? "word \"" + word + "\" after " + head : "command \"" + word + "\"") +
         did_you_mean(word, names));
}
