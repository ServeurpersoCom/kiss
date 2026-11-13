// values.cpp: the layers of the configuration

#include "engine/values.h"

#include "engine/engine.h"
#include "engine/schema.h"

#include <algorithm>

layer & factory_layer() {
    static layer factory;
    return factory;
}

std::optional<std::string> reader::get(const std::string & key, const std::string & item) const {
    const auto         value = reveal(key, item);
    const engine_key * def   = find_key(key);
    return value && def && def->kind == ENGINE_SECRET ? std::optional<std::string>(REMOVED) : value;
}

bool factory_holds(const std::string & s) {
    return factory_layer().count(s) > 0;
}

// the value a layer over the factory configuration gives a stored key, from
// the first of them that holds it, none where it takes the key back
static std::optional<std::string> given(const layer & over, const std::string & s) {
    const auto it = over.find(s);
    if (it != over.end()) {
        return it->second;
    }
    const auto f = factory_layer().find(s);
    return f != factory_layer().end() ? f->second : std::nullopt;
}

std::optional<std::string> reader::reveal(const std::string & key, const std::string & item) const {
    const auto value = given(over, stored(key, item));
    if (value) {
        return value;
    }
    const engine_key * def = find_key(key);
    return def ? def->reset : std::nullopt;
}

// an item exists while one of its keys has a value through the layers
std::vector<std::string> reader::names(const std::string & module) const {
    std::set<std::string> names;
    const layer &         factory = factory_layer();
    for (const layer * l : { &over, &factory }) {
        for (const auto & kv : *l) {
            const auto [key, item] = unstore(kv.first);
            if (!item.empty() && starts_with(key, module + " ") && given(over, kv.first)) {
                names.insert(item);
            }
        }
    }
    return words_t(names.begin(), names.end());
}

words_t layer_lines(const layer & l, bool secrets) {
    words_t out;
    for (const auto & kv : l) {
        if (!kv.second) {
            out.push_back(no_line(kv.first));
        } else {
            out.push_back(secrets ? set_line(kv.first, *kv.second) : show_line(kv.first, *kv.second));
        }
    }
    return out;
}

// the value of a register in force through the layers, none while no layer
// names its item
static std::optional<std::string> held(const reader & config, const std::string & key, const std::string & item) {
    if (!item.empty()) {
        const words_t names = config.names(module_of(key));
        if (std::find(names.begin(), names.end(), item) == names.end()) {
            return std::nullopt;
        }
    }
    return config.reveal(key, item);
}

words_t diff(const layer & from, const layer & to) {
    words_t      lines;
    const reader before(from), after(to);
    for (const std::string & s : changed(from, to)) {
        const auto [key, item] = unstore(s);
        const auto a           = held(before, key, item);
        const auto b           = held(after, key, item);
        if (a == b) {
            continue;
        }
        if (a && b && secret(s)) {
            lines.push_back(comment(s + " changed"));
            continue;
        }
        if (a) {
            lines.push_back(GONE_PREFIX + show_line(s, *a));
        }
        if (b) {
            lines.push_back(COME_PREFIX + show_line(s, *b));
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
