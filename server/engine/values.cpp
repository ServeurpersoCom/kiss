// values.cpp: the layers of the configuration

#include "engine/values.h"

#include "engine/engine.h"
#include "engine/schema.h"

layer & defaults_layer() {
    static layer defaults;
    return defaults;
}

std::optional<std::string> reader::get(const std::string & key, const std::string & item) const {
    const std::string s        = stored(key, item);
    const layer &     defaults = defaults_layer();
    for (const layer * l : { &over, &defaults }) {
        const auto it = l->find(s);
        if (it != l->end()) {
            return it->second;
        }
    }
    const engine_key * def = find_key(key);
    if (def && def->fallback) {
        return def->fallback();
    }
    return std::nullopt;
}

words_t layer_names(const layer & l, const std::string & module) {
    std::set<std::string> names;
    for (const auto & kv : l) {
        const auto [key, item] = unstore(kv.first);
        if (!item.empty() && starts_with(key, module + " ")) {
            names.insert(item);
        }
    }
    return words_t(names.begin(), names.end());
}

std::vector<std::string> reader::names(const std::string & module) const {
    std::set<std::string> names;
    const layer &         defaults = defaults_layer();
    for (const layer * l : { &over, &defaults }) {
        for (const std::string & n : layer_names(*l, module)) {
            names.insert(n);
        }
    }
    return words_t(names.begin(), names.end());
}

words_t layer_lines(const layer & l, bool secrets) {
    words_t out;
    for (const auto & kv : l) {
        out.push_back(secrets ? set_line(kv.first, kv.second) : show_line(kv.first, kv.second));
    }
    return out;
}

words_t diff(const layer & from, const layer & to) {
    words_t      lines;
    const reader before(from), after(to);
    for (const std::string & s : changed(from, to)) {
        const auto [key, item] = unstore(s);
        const auto a           = before.get(key, item);
        const auto b           = after.get(key, item);
        if (a == b) {
            continue;
        }
        if (a && b && secret(s)) {
            lines.push_back(comment(s + " changed"));
            continue;
        }
        if (a) {
            lines.push_back("- " + show_line(s, *a));
        }
        if (b) {
            lines.push_back("+ " + show_line(s, *b));
        }
    }
    return lines;
}

std::set<std::string> changed(const layer & a, const layer & b) {
    std::set<std::string> out;
    for (const layer * l : { &a, &b }) {
        for (const auto & kv : *l) {
            const auto x = a.find(kv.first);
            const auto y = b.find(kv.first);
            if (x == a.end() || y == b.end() || x->second != y->second) {
                out.insert(kv.first);
            }
        }
    }
    return out;
}
