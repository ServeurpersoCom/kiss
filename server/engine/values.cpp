// values.cpp: the layers of the configuration

#include "engine/values.h"

#include "engine/engine.h"
#include "engine/schema.h"

values_t & scope(layer & l, const std::string & platform) {
    if (platform.empty()) {
        return l.all;
    }
    if (platform == engine_platform()) {
        return l.own;
    }
    return l.foreign[platform];
}

layer & defaults_layer() {
    static layer defaults;
    return defaults;
}

std::vector<const values_t *> reader::scopes() const {
    return { &over.own, &over.all, &defaults_layer().own, &defaults_layer().all };
}

std::optional<std::string> reader::get(const std::string & key, const std::string & item) const {
    const std::string s = stored(key, item);
    for (const values_t * v : scopes()) {
        const auto it = v->find(s);
        if (it != v->end()) {
            return it->second;
        }
    }
    const engine_key * def = find_key(key);
    if (def && def->fallback) {
        return def->fallback();
    }
    return std::nullopt;
}

std::vector<std::string> reader::names(const std::string & module) const {
    std::set<std::string> names;
    for (const values_t * v : scopes()) {
        for (const auto & kv : *v) {
            const auto [key, item] = unstore(kv.first);
            if (!item.empty() && starts_with(key, module + " ")) {
                names.insert(item);
            }
        }
    }
    return words_t(names.begin(), names.end());
}

words_t layer_lines(const layer & l, bool secrets) {
    words_t out;
    auto    add = [&](const std::string & platform, const values_t & values) {
        for (const auto & kv : values) {
            out.push_back(secrets ? set_line(platform, kv.first, kv.second) : show_line(platform, kv.first, kv.second));
        }
    };
    add("", l.all);
    add(engine_platform(), l.own);
    for (const auto & f : l.foreign) {
        add(f.first, f.second);
    }
    return out;
}

words_t diff(const layer & from, const layer & to) {
    words_t               lines;
    const reader          before(from), after(to);
    std::set<std::string> keys;
    for (const layer * l : { &from, &to }) {
        for (const values_t * v : { &l->all, &l->own }) {
            for (const auto & kv : *v) {
                keys.insert(kv.first);
            }
        }
    }
    auto change = [&](const std::string & platform, const std::string & s, const std::optional<std::string> & a,
                      const std::optional<std::string> & b) {
        if (a == b) {
            return;
        }
        if (a && b && secret(s)) {
            lines.push_back(comment(prefixed(platform, s + " changed")));
            return;
        }
        if (a) {
            lines.push_back("- " + show_line(platform, s, *a));
        }
        if (b) {
            lines.push_back("+ " + show_line(platform, s, *b));
        }
    };
    for (const std::string & s : keys) {
        const auto [key, item] = unstore(s);
        change("", s, before.get(key, item), after.get(key, item));
    }
    std::set<std::string> platforms;
    for (const layer * l : { &from, &to }) {
        for (const auto & f : l->foreign) {
            platforms.insert(f.first);
        }
    }
    for (const std::string & p : platforms) {
        const values_t        empty;
        const auto            fa = from.foreign.find(p);
        const auto            fb = to.foreign.find(p);
        const values_t &      a  = fa == from.foreign.end() ? empty : fa->second;
        const values_t &      b  = fb == to.foreign.end() ? empty : fb->second;
        std::set<std::string> ks;
        for (const auto & kv : a) {
            ks.insert(kv.first);
        }
        for (const auto & kv : b) {
            ks.insert(kv.first);
        }
        for (const std::string & s : ks) {
            const auto ia = a.find(s);
            const auto ib = b.find(s);
            change(p, s, ia == a.end() ? std::nullopt : std::optional<std::string>(ia->second),
                   ib == b.end() ? std::nullopt : std::optional<std::string>(ib->second));
        }
    }
    return lines;
}

std::set<std::string> changed(const layer & a, const layer & b) {
    std::set<std::string> keys;
    for (const auto * pair : { &a.all, &b.all, &a.own, &b.own }) {
        for (const auto & kv : *pair) {
            keys.insert(kv.first);
        }
    }
    std::set<std::string> out;
    for (const std::string & k : keys) {
        auto value = [&](const layer & l) {
            const auto o = l.own.find(k);
            if (o != l.own.end()) {
                return std::optional<std::string>(o->second);
            }
            const auto x = l.all.find(k);
            return x == l.all.end() ? std::nullopt : std::optional<std::string>(x->second);
        };
        if (value(a) != value(b)) {
            out.insert(k);
        }
    }
    return out;
}
