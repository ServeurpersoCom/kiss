// schema.cpp: the keys every module declares

#include "engine/schema.h"

#include "engine/near.h"
#include "engine/parse.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

std::string stored(const std::string & key, const std::string & item) {
    return item.empty() ? key : key + " " + item;
}

std::pair<std::string, std::string> unstore(const std::string & s) {
    const words_t w = split(s, ' ');
    return { w[0] + " " + w[1], w.size() > 2 ? w[2] : "" };
}

static std::vector<engine_module> & registry() {
    static std::vector<engine_module> modules;
    return modules;
}

bool engine_register(engine_module (*make)()) {
    registry().push_back(make());
    return true;
}

const engine_module * find_module(const std::string & name) {
    for (const engine_module & m : registry()) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

const engine_key * find_key(const std::string & key) {
    const size_t          space = key.find(' ');
    const engine_module * m     = find_module(key.substr(0, space));
    if (!m || space == std::string::npos) {
        return nullptr;
    }
    for (const engine_key & k : m->keys) {
        if (k.name == key.substr(space + 1)) {
            return &k;
        }
    }
    return nullptr;
}

words_t module_names() {
    words_t names;
    for (const engine_module & m : registry()) {
        names.push_back(m.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

words_t key_names(const std::string & module) {
    words_t names;
    for (const engine_key & k : find_module(module)->keys) {
        names.push_back(k.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool collection(const std::string & module) {
    for (const engine_key & k : find_module(module)->keys) {
        if (k.named) {
            return true;
        }
    }
    return false;
}

std::string schema_module(const std::string & word) {
    const words_t     names  = module_names();
    const std::string module = pick(word, names, "key");
    if (module.empty()) {
        fail("unknown key \"" + word + "\"" + did_you_mean(word, names));
    }
    return module;
}

std::string schema_key(const std::string & module, const std::string & word) {
    return pick(word, key_names(module), module + " key");
}

// a number as it reads back: a whole number in all its digits, as JavaScript
// writes it below 1e21, any other in the shortest digits that give it back
static std::string number(double n) {
    char buf[32];
    if (n == std::floor(n) && std::fabs(n) < 1e21) {
        snprintf(buf, sizeof(buf), "%.0f", n);
        return buf;
    }
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(buf, sizeof(buf), "%.*g", precision, n);
        if (strtod(buf, nullptr) == n) {
            break;
        }
    }
    return buf;
}

// the value a key takes from what a line writes
static std::string check(const engine_key & def, const std::string & raw) {
    switch (def.kind) {
        case ENGINE_STRING:
            return raw;
        case ENGINE_SECRET:
            if (raw.empty()) {
                fail("a secret is never empty");
            }
            return raw;
        case ENGINE_NUMBER:
            {
                char *       end = nullptr;
                const double n   = strtod(raw.c_str(), &end);
                if (trim(raw).empty() || *end || !std::isfinite(n)) {
                    fail("\"" + raw + "\" is not a number");
                }
                if (def.integer && n != std::floor(n)) {
                    fail("\"" + raw + "\" is not a whole number");
                }
                if (def.min && n < *def.min) {
                    fail("\"" + raw + "\" is below " + number(*def.min));
                }
                if (def.max && n > *def.max) {
                    fail("\"" + raw + "\" is above " + number(*def.max));
                }
                return number(n);
            }
        case ENGINE_ENUM:
            {
                // a value may be any prefix that names one value only, as IOS
                // reads it, and is kept whole
                const std::string value = raw.empty() ? "" : pick(raw, def.values, "value");
                if (value.empty()) {
                    fail("\"" + raw + "\" is not one of " + join(def.values, " "));
                }
                return value;
            }
    }
    fail("unknown kind");
}

std::string schema_parse(const std::string & key, const std::string & raw) {
    try {
        return check(*find_key(key), raw);
    } catch (const std::runtime_error & e) {
        fail(key + ": " + e.what());
    }
}

key_words schema_read(const words_t & words) {
    if (words.empty()) {
        throw incomplete();
    }
    const std::string module = schema_module(words[0]);
    if (words.size() < 2) {
        throw incomplete();
    }
    const std::string word = schema_key(module, words[1]);
    if (word.empty()) {
        fail("unknown key \"" + module + " " + words[1] + "\"" +
             did_you_mean(words[1], key_names(module), module + " "));
    }
    const std::string  key = module + " " + word;
    const engine_key * def = find_key(key);
    if (!def->named) {
        return { key, def, module, "", words_t(words.begin() + 2, words.end()) };
    }
    if (words.size() < 3) {
        throw incomplete();
    }
    if (!item_name(words[2])) {
        fail("\"" + words[2] + "\" is not a " + module + " name");
    }
    return { key, def, module, words[2], words_t(words.begin() + 3, words.end()) };
}

words_t schema_next(const engine_reader & config, const words_t & words, bool value, bool items) {
    try {
        if (words.empty()) {
            return module_names();
        }
        const std::string module = schema_module(words[0]);
        words_t           keys   = key_names(module);
        if (words.size() == 1) {
            if (items && collection(module)) {
                for (const std::string & n : config.names(module)) {
                    keys.push_back(n);
                }
            }
            return keys;
        }
        const std::string word = schema_key(module, words[1]);
        if (word.empty()) {
            return {};
        }
        const engine_key * def = find_key(module + " " + word);
        if (def->named && words.size() == 2) {
            words_t names = config.names(module);
            names.push_back("<name>");
            return names;
        }
        if (!value || words.size() != (def->named ? 3u : 2u)) {
            return {};
        }
        if (def->kind == ENGINE_ENUM) {
            return def->values;
        }
        static const char * const kinds[] = { "<string>", "<number>", "<enum>", "<secret>" };
        return { kinds[def->kind] };
    } catch (const std::runtime_error &) {
        return {};
    }
}

bool secret(const std::string & s) {
    const engine_key * def = find_key(unstore(s).first);
    return def && def->kind == ENGINE_SECRET;
}

std::string set_line(const std::string & s, const std::string & v) {
    return "set " + s + " " + quote(v);
}

std::string show_line(const std::string & s, const std::string & v) {
    return secret(s) ? comment(s + " is set") : set_line(s, v);
}

const std::vector<engine_module> & schema_modules() {
    return registry();
}
