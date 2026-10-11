// engine.cpp: the CLI of the server: the grammar, the schema of the modules,
// the layers of the configuration, the commands and the batch

#include "engine.h"

#include "utf8.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>

static const char * const COMMENT_PREFIX = "!";
static const char * const ERROR_PREFIX   = "% ";
static const char * const PIPE           = "|";
static const char * const RUNNING_CONFIG = "running-config";
static const char * const STARTUP_CONFIG = "startup-config";
static const char * const ALL            = "all";
static const char * const PLATFORMS[]    = { "linux", "macos", "windows" };

// a typo is a word within one edit per this many letters of a known word
static const size_t TYPO_LETTERS_PER_EDIT = 3;

const char * engine_platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

//
// words
//

using words_t = std::vector<std::string>;

static std::string comment(const std::string & text) {
    return std::string(COMMENT_PREFIX) + " " + text;
}

static std::string lower(std::string s) {
    for (char & c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = (char) (c - 'A' + 'a');
        }
    }
    return s;
}

static bool starts_with(const std::string & s, const std::string & prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

static std::string join(const words_t & words, const char * sep) {
    std::string out;
    for (const std::string & w : words) {
        if (!out.empty()) {
            out += sep;
        }
        out += w;
    }
    return out;
}

static words_t split(const std::string & text, char sep) {
    words_t out;
    size_t  start = 0;
    for (;;) {
        const size_t end = text.find(sep, start);
        out.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            return out;
        }
        start = end + 1;
    }
}

static std::string trim(const std::string & s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) {
        return "";
    }
    const size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// a command that misses words: a lone line lists what may follow instead of
// failing
struct incomplete {};

[[noreturn]] static void fail(const std::string & message) {
    throw std::runtime_error(message);
}

// the edits turning a into b: inserting, deleting or changing a letter, or
// swapping two letters side by side, each one edit
static size_t distance(const std::string & a, const std::string & b) {
    std::vector<size_t> before, prev(b.size() + 1), row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) {
        prev[j] = j;
    }
    for (size_t i = 1; i <= a.size(); i++) {
        row[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            row[j]            = std::min({ prev[j] + 1, row[j - 1] + 1, prev[j - 1] + cost });
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                row[j] = std::min(row[j], before[j - 2] + 1);
            }
        }
        before = prev;
        prev   = row;
    }
    return prev[b.size()];
}

// ", did you mean x" when a candidate is close enough to be a typo of the
// word, else nothing; head, the words before the candidate, makes the
// suggestion a whole one
static std::string did_you_mean(const std::string & word, const words_t & candidates, const std::string & head = "") {
    const size_t max  = std::max<size_t>(1, word.size() / TYPO_LETTERS_PER_EDIT);
    size_t       best = max + 1;
    std::string  near;
    for (const std::string & c : candidates) {
        const size_t d = distance(lower(word), lower(c));
        if (d < best) {
            best = d;
            near = c;
        }
    }
    return near.empty() ? "" : ", did you mean " + head + near;
}

static std::string beyond(const words_t & head, const words_t & rest) {
    return "nothing goes after " + join(head, " ") + ": " + join(rest, " ");
}

// the word a typed word names: itself, else the only word it starts
static std::string pick(const std::string & word, const words_t & words, const std::string & what) {
    const std::string typed = lower(word);
    if (std::find(words.begin(), words.end(), typed) != words.end()) {
        return typed;
    }
    words_t hits;
    for (const std::string & w : words) {
        if (starts_with(w, typed)) {
            hits.push_back(w);
        }
    }
    if (hits.size() > 1) {
        fail("ambiguous " + what + " \"" + word + "\": " + join(hits, " "));
    }
    return hits.empty() ? "" : hits[0];
}

//
// grammar
//

// one command per line; blank lines and comments are skipped
static words_t split_lines(const std::string & text) {
    words_t lines;
    for (const std::string & raw : split(text, '\n')) {
        const std::string line = trim(raw);
        if (!line.empty() && !starts_with(line, COMMENT_PREFIX)) {
            lines.push_back(line);
        }
    }
    return lines;
}

static void put_utf8(std::string & out, unsigned cp) {
    if (cp < 0x80) {
        out += (char) cp;
    } else if (cp < 0x800) {
        out += (char) (0xC0 | (cp >> 6));
        out += (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char) (0xE0 | (cp >> 12));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    } else {
        out += (char) (0xF0 | (cp >> 18));
        out += (char) (0x80 | ((cp >> 12) & 0x3F));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
}

static bool hex4(const std::string & s, size_t at, unsigned & out) {
    if (at + 4 > s.size()) {
        return false;
    }
    out = 0;
    for (size_t i = at; i < at + 4; i++) {
        const char c = s[i];
        out <<= 4;
        if (c >= '0' && c <= '9') {
            out |= (unsigned) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            out |= (unsigned) (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            out |= (unsigned) (c - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

static bool blank(char c) {
    return c == ' ' || c == '\t';
}

// splits a line on blanks; a token opening with a double quote takes JSON
// escapes and one opening with a single quote is literal, so every value
// printed by the schema reads back unchanged
static words_t tokenize(const std::string & line) {
    words_t tokens;
    size_t  i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (blank(c)) {
            i++;
            continue;
        }
        if (c == '"') {
            std::string token;
            size_t      j = i + 1;
            for (; j < line.size() && line[j] != '"'; j++) {
                if (line[j] != '\\' || j + 1 >= line.size()) {
                    token += line[j];
                    continue;
                }
                const char e  = line[++j];
                unsigned   cp = 0;
                if (e == 'u' && hex4(line, j + 1, cp)) {
                    j += 4;
                    unsigned low = 0;
                    if (cp >= 0xD800 && cp < 0xDC00 && line.compare(j + 1, 2, "\\u") == 0 && hex4(line, j + 3, low) &&
                        low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        j += 6;
                    }
                    put_utf8(token, cp);
                    continue;
                }
                switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        token += e;
                        break;
                    case 'b':
                        token += '\b';
                        break;
                    case 'f':
                        token += '\f';
                        break;
                    case 'n':
                        token += '\n';
                        break;
                    case 'r':
                        token += '\r';
                        break;
                    case 't':
                        token += '\t';
                        break;
                    default:
                        token += '\\';
                        token += e;
                }
            }
            if (j >= line.size()) {
                fail("unterminated \"");
            }
            tokens.push_back(token);
            i = j + 1;
            continue;
        }
        if (c == '\'') {
            const size_t j = line.find('\'', i + 1);
            if (j == std::string::npos) {
                fail("unterminated '");
            }
            tokens.push_back(line.substr(i + 1, j - i - 1));
            i = j + 1;
            continue;
        }
        size_t j = i;
        while (j < line.size() && !blank(line[j])) {
            j++;
        }
        tokens.push_back(line.substr(i, j - i));
        i = j;
    }
    return tokens;
}

// splits a line at its first pipe outside quotes, as IOS does: the command,
// then the filter, the rest of the line as written, pipes included
static std::pair<std::string, std::optional<std::string>> split_filter(const std::string & line) {
    char quote = 0;
    for (size_t i = 0; i < line.size(); i++) {
        const char c = line[i];
        if (quote) {
            if (c == '\\' && quote == '"') {
                i++;
            } else if (c == quote) {
                quote = 0;
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == PIPE[0]) {
            return { trim(line.substr(0, i)), line.substr(i + 1) };
        }
    }
    return { trim(line), std::nullopt };
}

using filter_t = std::function<words_t(const words_t &)>;

// the filter after a pipe, as IOS reads it: show running-config | include
// port; every filter takes a pattern, the rest of the line as written, a
// regular expression that minds case; count tells how many lines match in the
// words of IOS, as a note
static filter_t compile_filter(const std::string & spec) {
    static const words_t names   = { "begin", "count", "exclude", "include" };
    const std::string    trimmed = trim(spec);
    const std::string    word    = trimmed.substr(0, trimmed.find_first_of(" \t"));
    const std::string    pattern = trim(trimmed.substr(word.size()));
    words_t              hits;
    for (const std::string & n : names) {
        if (starts_with(n, lower(word))) {
            hits.push_back(n);
        }
    }
    if (hits.size() != 1) {
        fail("unknown filter \"" + word + "\", use " + join(names, " "));
    }
    const std::string name = hits[0];
    if (pattern.empty()) {
        fail(name + " needs a pattern");
    }
    std::regex re;
    try {
        re = std::regex(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error &) {
        fail("\"" + pattern + "\" is not a pattern");
    }
    return [name, re](const words_t & lines) {
        words_t out;
        size_t  matches = 0;
        bool    begun   = false;
        for (const std::string & l : lines) {
            const bool hit = std::regex_search(l, re);
            matches += hit;
            begun = begun || hit;
            if ((name == "begin" && begun) || (name == "include" && hit) || (name == "exclude" && !hit)) {
                out.push_back(l);
            }
        }
        if (name == "count") {
            out = { comment("number of lines which match regexp = " + std::to_string(matches)) };
        }
        return out;
    };
}

// a value that reads back as one token without quotes, a pipe opening a filter
static bool bare(const std::string & v) {
    if (v.empty() || v[0] == '"' || v[0] == '\'') {
        return false;
    }
    for (char c : v) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == PIPE[0]) {
            return false;
        }
    }
    return true;
}

// a value as a line writes it: bare when it reads back so, else a JSON string
static std::string quote(const std::string & v) {
    if (bare(v)) {
        return v;
    }
    std::string out = "\"";
    for (unsigned char c : v) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char) c;
                }
        }
    }
    return out + "\"";
}

// a name of an item: a word of its own, as a server writes the name of a tool
static bool item_name(const std::string & s) {
    if (s.empty() || !isalnum((unsigned char) s[0])) {
        return false;
    }
    for (char c : s) {
        if (!isalnum((unsigned char) c) && !strchr("_.:/-", c)) {
            return false;
        }
    }
    return true;
}

//
// schema
//

// a stored key is the key, then the item name for a named key
static std::string stored(const std::string & key, const std::string & item) {
    return item.empty() ? key : key + " " + item;
}

// the key and the item name of a stored key
static std::pair<std::string, std::string> unstore(const std::string & s) {
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

static const engine_module * find_module(const std::string & name) {
    for (const engine_module & m : registry()) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

static const engine_key * find_key(const std::string & key) {
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

static words_t module_names() {
    words_t names;
    for (const engine_module & m : registry()) {
        names.push_back(m.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

static words_t key_names(const std::string & module) {
    words_t names;
    for (const engine_key & k : find_module(module)->keys) {
        names.push_back(k.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

static bool collection(const std::string & module) {
    for (const engine_key & k : find_module(module)->keys) {
        if (k.named) {
            return true;
        }
    }
    return false;
}

static std::string schema_module(const std::string & word) {
    const words_t     names  = module_names();
    const std::string module = pick(word, names, "key");
    if (module.empty()) {
        fail("unknown key \"" + word + "\"" + did_you_mean(word, names));
    }
    return module;
}

static std::string schema_key(const std::string & module, const std::string & word) {
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
            if (std::find(def.values.begin(), def.values.end(), raw) == def.values.end()) {
                fail("\"" + raw + "\" is not one of " + join(def.values, " "));
            }
            return raw;
    }
    fail("unknown kind");
}

static std::string schema_parse(const std::string & key, const std::string & raw) {
    try {
        return check(*find_key(key), raw);
    } catch (const std::runtime_error & e) {
        fail(key + ": " + e.what());
    }
}

struct key_words {
    std::string        key;
    const engine_key * def;
    std::string        module;
    std::string        item;
    words_t            rest;
};

// the key the words name, its item for a named key, and the words after it
static key_words schema_read(const words_t & words) {
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

// what may follow the words of a key: modules, keys, items, a value
static words_t schema_next(const engine_reader & config, const words_t & words, bool value, bool items) {
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

static bool secret(const std::string & s) {
    const engine_key * def = find_key(unstore(s).first);
    return def && def->kind == ENGINE_SECRET;
}

static std::string prefixed(const std::string & platform, const std::string & line) {
    return platform.empty() ? line : platform + " " + line;
}

// the line that sets a key, a secret with its value
static std::string set_line(const std::string & platform, const std::string & s, const std::string & v) {
    return prefixed(platform, "set " + s + " " + quote(v));
}

// the line that shows a key, a secret out of view
static std::string show_line(const std::string & platform, const std::string & s, const std::string & v) {
    return secret(s) ? comment(prefixed(platform, s + " is set")) : set_line(platform, s, v);
}

//
// configuration
//

using values_t = std::map<std::string, std::string>;

// a layer of the configuration: the lines without a platform, the lines of
// this platform, and the lines of the other platforms, kept to be written
// back whole
struct layer {
    values_t                        all;
    values_t                        own;
    std::map<std::string, values_t> foreign;

    bool operator==(const layer & o) const { return all == o.all && own == o.own && foreign == o.foreign; }
};

// the values of a platform within a layer: own for this platform, all for none
static values_t & scope(layer & l, const std::string & platform) {
    if (platform.empty()) {
        return l.all;
    }
    if (platform == engine_platform()) {
        return l.own;
    }
    return l.foreign[platform];
}

// the defaults the binary embeds, and the running configuration over them
static layer       g_defaults;
static layer       g_running;
static std::string g_startup;
static std::string g_version;
static std::mutex  g_mutex;

// a configuration as a module reads it: a layer over the defaults, each
// taking the value of this platform before the one of none, then the
// fallback of the key
struct reader : engine_reader {
    const layer & over;

    explicit reader(const layer & l) : over(l) {}

    // where a value is looked for, the first that holds it winning
    std::vector<const values_t *> scopes() const { return { &over.own, &over.all, &g_defaults.own, &g_defaults.all }; }

    std::optional<std::string> get(const std::string & key, const std::string & item) const override {
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

    std::vector<std::string> names(const std::string & module) const override {
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
};

// the lines of a layer: none, then this platform, then the others, sorted;
// secrets in view only when they are to be written
static words_t layer_lines(const layer & l, bool secrets) {
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

// what a change does, line by line: the old line after -, the new one after
// +, every value of this platform resolved through the layers and the lines
// of the other platforms as they are; a secret replaced by another reads as
// one line, its values out of view
static words_t diff(const layer & from, const layer & to) {
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

// a whole file, found false when it does not exist
static std::string read_file(const std::string & path, bool & found) {
    found    = false;
    FILE * f = utf8_fopen(path.c_str(), "rb");
    if (!f) {
        return "";
    }
    found = true;
    std::string text;
    char        buf[4096];
    size_t      n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    fclose(f);
    return text;
}

//
// commands
//

struct context {
    layer &     draft;
    engine_role role;
};

using step_t = std::function<std::string(context &)>;

struct command {
    words_t path;
    // its first argument names a module, so after its path the name of a
    // module competes with the words of longer commands
    bool    module;
    // whether a platform may open its line
    bool    scoped;
    // the step the arguments spell, checked before any line runs
    step_t (*compile)(const std::string & platform, const words_t & args);
    // candidates for the next argument; a candidate in <> names what to type
    words_t (*complete)(const words_t & args);
};

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
            out.push_back(show_line("", stored(key, only_item), *value));
        } else if (!only_key.empty() && (!only_item.empty() || !k.named)) {
            out.push_back(comment(stored(key, only_item) + " is not set"));
        }
    }
    return out;
}

// the values the words name, defaults included, as set lines to paste back
static std::string show_module(const reader &      config,
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
        }
    } else {
        out = effective_lines(m, config, key, item, true);
    }
    if (out.empty()) {
        return comment("no " + module + (item.empty() ? "" : " " + item) + " yet");
    }
    return join(out, "\n");
}

static std::string configuration(const std::string & word) {
    for (const char * n : { RUNNING_CONFIG, STARTUP_CONFIG }) {
        if (starts_with(n, lower(word))) {
            return n;
        }
    }
    return word;
}

static layer load_startup();

static words_t next_set(const words_t & args) {
    return schema_next(reader(g_running), args, true, false);
}

static words_t next_key_or_item(const words_t & args) {
    return schema_next(reader(g_running), args, false, true);
}

// set server port 9000: a line without a platform on a key the draft holds for
// this platform changes it there
static step_t compile_set(const std::string & platform, const words_t & args) {
    const key_words kw = schema_read(args);
    if (kw.rest.empty()) {
        throw incomplete();
    }
    const std::string value = schema_parse(kw.key, join(kw.rest, " "));
    const std::string s     = stored(kw.key, kw.item);
    const engine_kind kind  = kw.def->kind;
    return [platform, s, value, kind](context & ctx) {
        if (kind == ENGINE_SECRET && ctx.role == ENGINE_MCP) {
            fail(s + " is written in server.conf or on the command line, a model would read it here");
        }
        if (platform.empty() && ctx.draft.own.count(s)) {
            ctx.draft.own[s] = value;
        } else {
            scope(ctx.draft, platform)[s] = value;
        }
        return std::string();
    };
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

// show server, show server port: the word after a collection names a key when
// it names one, else an item
static step_t compile_show(const std::string &, const words_t & args) {
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
        return show_module(reader(ctx.draft), module, key, item);
    };
}

// IOS show running-config: every line the running configuration holds, as the
// line that sets it, which copy writes to server.conf; all, every key of every
// module with its value on this platform, defaults included
static step_t compile_show_running(const std::string &, const words_t & args) {
    if (!args.empty() && !starts_with(ALL, lower(args[0]))) {
        fail(beyond({ "show", RUNNING_CONFIG }, args));
    }
    if (args.size() > 1) {
        fail(beyond({ "show", RUNNING_CONFIG, ALL }, words_t(args.begin() + 1, args.end())));
    }
    const bool every = args.size() == 1;
    return [every](context & ctx) {
        if (every) {
            words_t shown;
            for (const std::string & m : module_names()) {
                shown.push_back(show_module(reader(ctx.draft), m, "", ""));
            }
            return join(shown, "\n");
        }
        const words_t lines = layer_lines(ctx.draft, false);
        return lines.empty() ? comment("every key is at its default") : join(lines, "\n");
    };
}

static words_t next_show_running(const words_t & args) {
    return args.empty() ? words_t{ ALL } : words_t{};
}

// what the server starts with: server.conf as it reads now
static step_t compile_show_startup(const std::string &, const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", STARTUP_CONFIG }, args));
    }
    return [](context &) {
        const words_t lines = layer_lines(load_startup(), false);
        return lines.empty() ? comment("every key is at its default") : join(lines, "\n");
    };
}

static step_t compile_show_version(const std::string &, const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", "version" }, args));
    }
    return [](context &) {
        return comment("KiSS server " + g_version + ", " + engine_platform());
    };
}

// server.conf written whole, through a file beside it renamed over it, so a
// failure never leaves half a file
static std::string write_startup(const layer & l) {
    const std::string tmp = g_startup + ".tmp";
    FILE *            f   = utf8_fopen(tmp.c_str(), "wb");
    if (!f) {
        fail("cannot write " + tmp);
    }
    const std::string text = join(layer_lines(l, true), "\n") + "\n";
    const bool        ok   = fwrite(text.data(), 1, text.size(), f) == text.size();
    if (fclose(f) != 0 || !ok) {
        fail("cannot write " + tmp);
    }
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(g_startup), ec);
    if (ec) {
        fail("cannot write " + g_startup + ": " + ec.message());
    }
    return comment(std::string("copied ") + RUNNING_CONFIG + " to " + STARTUP_CONFIG);
}

// copy running-config startup-config writes server.conf, copy startup-config
// running-config reads it back into the draft
static step_t compile_copy(const std::string &, const words_t & args) {
    if (args.size() < 2) {
        throw incomplete();
    }
    if (args.size() > 2) {
        fail(beyond({ "copy", args[0], args[1] }, words_t(args.begin() + 2, args.end())));
    }
    const std::string from = configuration(args[0]);
    const std::string to   = configuration(args[1]);
    for (const std::string & n : { from, to }) {
        if (n != RUNNING_CONFIG && n != STARTUP_CONFIG) {
            fail("unknown configuration \"" + n + "\", use " + RUNNING_CONFIG + " " + STARTUP_CONFIG);
        }
    }
    if (from == to) {
        fail("a copy goes from one configuration to another");
    }
    return [to](context & ctx) {
        if (to == RUNNING_CONFIG) {
            ctx.draft = load_startup();
            return std::string();
        }
        return write_startup(ctx.draft);
    };
}

static words_t next_copy(const words_t & args) {
    return args.size() < 2 ? words_t{ RUNNING_CONFIG, STARTUP_CONFIG } : words_t{};
}

static std::vector<command> & commands() {
    static std::vector<command> list = {
        { { "set" },                  true,  true,  compile_set,          next_set          },
        { { "no" },                   true,  true,  compile_no,           next_key_or_item  },
        { { "show" },                 true,  false, compile_show,         next_key_or_item  },
        { { "show", RUNNING_CONFIG }, false, false, compile_show_running, next_show_running },
        { { "show", STARTUP_CONFIG }, false, false, compile_show_startup, nullptr           },
        { { "show", "version" },      false, false, compile_show_version, nullptr           },
        { { "copy" },                 false, false, compile_copy,         next_copy         },
    };
    return list;
}

struct walk_t {
    // the deepest command whose whole path the words spell
    const command *              found = nullptr;
    // words consumed by that command's path
    size_t                       depth = 0;
    // commands still matching after the last consumed word
    std::vector<const command *> pool;
    // words consumed by the walk, matched command or not
    size_t                       consumed = 0;
    // the word that matched several path words, with those words
    std::string                  ambiguous;
    words_t                      names;
};

// IOS lookup: each word may be any unambiguous prefix of a path word, an exact
// word always wins, and the walk stops at the first word no path continues
// with; right after a command whose first argument names a module, the names
// of the modules compete with the path words, a prefix of both being ambiguous
static walk_t walk(const words_t & words) {
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

static std::pair<const command *, words_t> resolve(const words_t & words) {
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

// what may follow the words: path words while the path goes on, then the
// arguments the command proposes
static words_t next_words(const words_t & words) {
    const walk_t w = walk(words);
    if (!w.ambiguous.empty()) {
        return {};
    }
    std::set<std::string> found;
    if (w.consumed == words.size()) {
        for (const command * c : w.pool) {
            if (c->path.size() > w.consumed) {
                found.insert(c->path[w.consumed]);
            }
        }
    }
    if (w.found && w.found->complete && (w.consumed == words.size() || w.depth == w.consumed)) {
        for (const std::string & a : w.found->complete(words_t(words.begin() + (long) w.depth, words.end()))) {
            found.insert(a);
        }
    }
    words_t out(found.begin(), found.end());
    std::stable_partition(out.begin(), out.end(), [](const std::string & s) { return s[0] != '<'; });
    return out;
}

struct step {
    step_t                  run;
    std::optional<filter_t> filter;
};

// a line as the schema alone reads it: the command and its arguments checked,
// its filter compiled; a lone line that stops short lists what may follow it
static step compile(const std::string & text, bool alone) {
    const auto [head, spec] = split_filter(text);
    words_t     words       = tokenize(head);
    std::string platform;
    if (!words.empty()) {
        for (const char * p : PLATFORMS) {
            if (lower(words[0]) == p) {
                platform = p;
                words.erase(words.begin());
                break;
            }
        }
    }
    std::optional<filter_t> filter;
    if (spec) {
        filter = compile_filter(*spec);
    }
    try {
        const auto [cmd, args] = resolve(words);
        if (!platform.empty() && !cmd->scoped) {
            fail(platform + " opens a set or a no line only");
        }
        return { cmd->compile(platform, args), filter };
    } catch (const incomplete &) {
        const std::string next = join(next_words(words), " ");
        if (!alone) {
            fail("\"" + head + "\" misses words, it may go on with: " + next);
        }
        return { [next](context &) { return comment(next); }, filter };
    }
}

static engine_result failed(std::optional<size_t> line, bool alone, const std::string & message) {
    const std::string where = !line || alone ? "" : "line " + std::to_string(*line + 1) + ": ";
    std::string       text  = ERROR_PREFIX + where;
    for (char c : message) {
        text += c;
        if (c == '\n') {
            text += ERROR_PREFIX;
        }
    }
    if (!alone) {
        text += std::string("\n") + ERROR_PREFIX + "nothing applied";
    }
    return { false, text };
}

// the stored keys whose value differs between two layers, foreign ones aside
static std::set<std::string> changed(const layer & a, const layer & b) {
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

// every line compiles before the first one runs, then runs on a copy of the
// target, which it replaces only when all of them succeed and the rules of
// every module hold; the batch answers with what it changed, and on the
// running configuration the modules whose keys changed apply
static engine_result batch(layer & target, const std::string & text, engine_role role, bool running) {
    words_t lines = split_lines(text);
    if (lines.empty()) {
        lines.push_back("");
    }
    const bool        alone = lines.size() == 1;
    std::vector<step> steps;
    for (size_t i = 0; i < lines.size(); i++) {
        try {
            steps.push_back(compile(lines[i], alone));
        } catch (const std::runtime_error & e) {
            return failed(i, alone, e.what());
        }
    }
    layer   draft = target;
    context ctx{ draft, role };
    words_t out;
    for (size_t i = 0; i < steps.size(); i++) {
        try {
            std::string printed = steps[i].run(ctx);
            if (steps[i].filter) {
                const words_t kept = (*steps[i].filter)(printed.empty() ? words_t{} : split(printed, '\n'));
                printed            = kept.empty() ? comment("no line matches") : join(kept, "\n");
            }
            if (!printed.empty()) {
                out.push_back(printed);
            }
        } catch (const std::runtime_error & e) {
            return failed(i, alone, e.what());
        }
    }
    words_t broken;
    for (const engine_module & m : registry()) {
        const std::string reason = m.validate ? m.validate(reader(draft)) : "";
        if (!reason.empty()) {
            broken.push_back(m.name + ": " + reason);
        }
    }
    if (!broken.empty()) {
        return failed(std::nullopt, alone, join(broken, "\n"));
    }
    const words_t               change = diff(target, draft);
    const std::set<std::string> keys   = changed(target, draft);
    target                             = draft;
    for (const engine_module & m : registry()) {
        const bool touched =
            std::any_of(keys.begin(), keys.end(), [&](const std::string & k) { return starts_with(k, m.name + " "); });
        if (running && touched && m.apply) {
            m.apply(reader(target));
        }
    }
    out.insert(out.end(), change.begin(), change.end());
    return { true, out.empty() ? comment("no change") : join(out, "\n") };
}

// server.conf as a layer of its own: a batch of the administrator over nothing
static layer load_startup() {
    bool              found = false;
    const std::string text  = read_file(g_startup, found);
    layer             startup;
    if (!found) {
        return startup;
    }
    const engine_result r = batch(startup, text, ENGINE_ADMIN, false);
    if (!r.ok) {
        fail(g_startup + ":\n" + r.text);
    }
    return startup;
}

std::string engine_defaults(const std::string & text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    layer                       defaults;
    words_t                     problems;
    for (const std::string & line : split_lines(text)) {
        try {
            words_t     words = tokenize(line);
            std::string platform;
            for (const char * p : PLATFORMS) {
                if (!words.empty() && lower(words[0]) == p) {
                    platform = p;
                    words.erase(words.begin());
                    break;
                }
            }
            if (words.empty() || lower(words[0]) != "set") {
                fail("the defaults hold set lines only");
            }
            const key_words kw = schema_read(words_t(words.begin() + 1, words.end()));
            if (kw.rest.empty()) {
                fail(kw.key + " misses its value");
            }
            scope(defaults, platform)[stored(kw.key, kw.item)] = schema_parse(kw.key, join(kw.rest, " "));
        } catch (const std::runtime_error & e) {
            problems.push_back(line + ": " + e.what());
        } catch (const incomplete &) {
            problems.push_back(line + ": misses words");
        }
    }
    if (problems.empty()) {
        g_defaults = defaults;
    }
    return join(problems, "\n");
}

engine_result engine_start(const std::string & path, const std::string & version) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_startup = path;
    g_version = version;
    try {
        g_running = load_startup();
    } catch (const std::runtime_error & e) {
        return { false, e.what() };
    }
    for (const engine_module & m : registry()) {
        if (m.apply) {
            m.apply(reader(g_running));
        }
    }
    return { true, "" };
}

engine_result engine_run(const std::string & text, engine_role role) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return batch(g_running, text, role, true);
}

std::optional<std::string> engine_get(const std::string & key, const std::string & item) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return reader(g_running).get(key, item);
}
