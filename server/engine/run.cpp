// run.cpp: the batch: every line compiled, then run on a draft, then applied

#include "engine/run.h"

#include "engine/archive.h"
#include "engine/filter.h"
#include "engine/parse.h"
#include "engine/registry.h"
#include "engine/schema.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>

const char * engine_platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string engine_component(const std::string & name, const std::string & version) {
    return "server " + (name.empty() ? "" : name + " ") + version + " on " + engine_platform();
}

// the running configuration over the factory configuration, the version, and the lock that
// lets one batch run at a time
static layer       g_running;
static std::string g_version;
static std::mutex  g_mutex;

const layer & running_layer() {
    return g_running;
}

const std::string & engine_version() {
    return g_version;
}

struct step {
    step_t                  run;
    std::optional<filter_t> filter;
};

// a line as the schema alone reads it: the command and its arguments checked,
// its filter compiled; a lone line that stops short lists what may follow it
static step compile(const std::string & text, bool alone) {
    const auto [head, spec]       = split_filter(text);
    const words_t           words = tokenize(head);
    std::optional<filter_t> filter;
    if (spec) {
        filter = compile_filter(*spec);
    }
    try {
        const auto [cmd, args] = resolve(words);
        return { cmd->compile(args), filter };
    } catch (const incomplete &) {
        const std::string next = join(next_words(words), " ");
        if (!alone) {
            fail("\"" + head + "\" misses words, it may go on with: " + next);
        }
        return { [next](context &) { return comment(next); }, filter };
    }
}

// the key an item names where it sends its secrets by
static const char * const URL_KEY = "url";
// what the user answers to let a url
static const char * const YES     = "true";

// the url of the item of a stored key, in the draft, when its module has one
static std::string url_of(const layer & draft, const std::string & s) {
    const auto        parts  = unstore(s);
    const std::string module = parts.first.substr(0, parts.first.find(' '));
    if (parts.second.empty() || !find_key(module + " " + URL_KEY)) {
        return "";
    }
    return reader(draft).get(module + " " + URL_KEY, parts.second).value_or("");
}

// the urls a layer gives the items of its modules, by stored key
static std::set<std::pair<std::string, std::string>> urls(const layer & l) {
    std::set<std::pair<std::string, std::string>> out;
    for (const auto & kv : l) {
        const auto        parts  = unstore(kv.first);
        const std::string module = parts.first.substr(0, parts.first.find(' '));
        if (!parts.second.empty() && parts.first == module + " " + URL_KEY) {
            out.insert(kv);
        }
    }
    return out;
}

// the urls an MCP client gives that the user is to let: those of the draft
// that neither the running configuration before the batch nor the
// startup-config holds, the user not yet letting them in this call
static std::vector<engine_ask> unlet(const layer & before, const layer & draft, const engine_typed & typed) {
    auto       known   = urls(before);
    const auto startup = urls(startup_layer());
    known.insert(startup.begin(), startup.end());
    std::vector<engine_ask> out;
    for (const auto & u : urls(draft)) {
        const auto let = typed.find(u.first);
        if (known.count(u) || (let != typed.end() && let->second == YES)) {
            continue;
        }
        const auto        parts  = unstore(u.first);
        const std::string module = parts.first.substr(0, parts.first.find(' '));
        engine_ask        ask{ u.first, u.second, true, {} };
        for (const engine_key & k : find_module(module)->keys) {
            const std::string s = stored(module + " " + k.name, parts.second);
            if (k.kind == ENGINE_SECRET && draft.count(s)) {
                ask.along.push_back(s);
            }
        }
        out.push_back(ask);
    }
    return out;
}

static std::vector<engine_ask> asked(const layer & draft, const words_t & keys) {
    std::vector<engine_ask> out;
    for (const std::string & s : keys) {
        out.push_back({ s, url_of(draft, s), false, {} });
    }
    return out;
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
    return { false, text, {} };
}

// every line compiles before the first one runs, then runs on a copy of the
// target, which it replaces only when all of them succeed and the rules of
// every module hold; the batch answers with what it changed, and on the
// running configuration the modules whose keys changed apply. A secret left
// to ask for, or a url of an MCP client the user has not let, stops it once
// every line ran, before the rules: what the user answers decides them. A
// batch of an MCP client moves no secret. A save
// writes the draft as the batch leaves it, once the rules hold and before it
// replaces the target
static engine_result batch(layer &              target,
                           const std::string &  text,
                           engine_role          role,
                           bool                 running,
                           const engine_typed & typed) {
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
    words_t asks;
    context ctx{ draft, role, typed, asks };
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
    std::vector<engine_ask> wanted = role == ENGINE_MCP ? unlet(target, draft, typed) : std::vector<engine_ask>{};
    const auto              typing = asked(draft, asks);
    wanted.insert(wanted.end(), typing.begin(), typing.end());
    if (!wanted.empty()) {
        return { true, "", wanted };
    }
    words_t broken;
    for (const engine_module & m : schema_modules()) {
        const std::string reason = m.validate ? m.validate(reader(draft)) : "";
        if (!reason.empty()) {
            broken.push_back(m.name + ": " + reason);
        }
    }
    if (!broken.empty()) {
        return failed(std::nullopt, alone, join(broken, "\n"));
    }
    if (ctx.save) {
        try {
            out.push_back(archive_write(draft));
        } catch (const std::runtime_error & e) {
            return failed(std::nullopt, alone, e.what());
        }
    }
    const words_t               change = diff(target, draft);
    const std::set<std::string> keys   = changed(target, draft);
    target                             = draft;
    for (const engine_module & m : schema_modules()) {
        const bool touched =
            std::any_of(keys.begin(), keys.end(), [&](const std::string & k) { return starts_with(k, m.name + " "); });
        if (running && touched && m.apply) {
            m.apply(reader(target));
        }
    }
    out.insert(out.end(), change.begin(), change.end());
    return { true, out.empty() ? comment("no change") : join(out, "\n"), {} };
}

// the block a line that compiles belongs to, as IOS groups the lines of an
// interface: the item a set line names, else the key it names, else the line
// alone
static std::string block_of(const std::string & line) {
    const auto [cmd, args] = resolve(tokenize(split_filter(line).first));
    if (cmd->path != words_t{ "set" }) {
        return line;
    }
    const key_words kw = schema_read(args);
    return kw.item.empty() ? kw.key : kw.module + " " + kw.item;
}

// why a batch refused, its lines joined, what it did not apply left out
static std::string refusal(const std::string & text) {
    const std::string prefix = ERROR_PREFIX;
    words_t           out;
    for (const std::string & l : split(text, '\n')) {
        const std::string s = starts_with(l, prefix) ? l.substr(prefix.size()) : l;
        if (s != "nothing applied") {
            out.push_back(s);
        }
    }
    return join(out, "; ");
}

// a configuration enters a layer as IOS runs one at boot: a line the parser
// refuses stays out, named by its number, never by its text, which may hold a
// secret; then the rest goes block by block, a block its batch refuses staying
// out, tried again once others entered, as one item may name another; returns
// each line and block that stays out and why
static words_t enter(layer & target, const std::string & text) {
    words_t                                      out;
    std::vector<std::pair<std::string, words_t>> blocks;
    const words_t                                raw = split(text, '\n');
    for (size_t i = 0; i < raw.size(); i++) {
        const std::string line = trim(raw[i]);
        if (line.empty() || starts_with(line, COMMENT_PREFIX)) {
            continue;
        }
        try {
            compile(line, false);
        } catch (const std::runtime_error & e) {
            out.push_back("line " + std::to_string(i + 1) + ": " + e.what());
            continue;
        }
        const std::string b  = block_of(line);
        const auto        it = std::find_if(blocks.begin(), blocks.end(), [&](const auto & x) { return x.first == b; });
        if (it == blocks.end()) {
            blocks.push_back({ b, { line } });
        } else {
            it->second.push_back(line);
        }
    }
    std::map<std::string, std::string> why;
    for (bool moved = true; moved;) {
        moved = false;
        for (auto it = blocks.begin(); it != blocks.end();) {
            const engine_result r = batch(target, join(it->second, "\n"), ENGINE_ADMIN, false, {});
            if (r.ok) {
                it    = blocks.erase(it);
                moved = true;
            } else {
                why[it->first] = refusal(r.text);
                ++it;
            }
        }
    }
    for (const auto & b : blocks) {
        out.push_back(b.first + ": " + why[b.first]);
    }
    return out;
}

layer startup_layer(words_t * skipped) {
    bool              found = false;
    const std::string text  = archive_read(found);
    layer             startup;
    const words_t     out = found ? enter(startup, text) : words_t{};
    if (skipped) {
        *skipped = out;
    }
    return startup;
}

std::string engine_factory(const std::string & text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    layer                       factory;
    words_t                     problems;
    for (const std::string & line : split_lines(text)) {
        try {
            const words_t words = tokenize(line);
            if (words.empty() || lower(words[0]) != "set") {
                fail("the factory configuration holds set lines only");
            }
            const key_words kw = schema_read(words_t(words.begin() + 1, words.end()));
            if (kw.rest.empty()) {
                fail(kw.key + " misses its value");
            }
            factory[stored(kw.key, kw.item)] = schema_parse(kw.key, join(kw.rest, " "));
        } catch (const std::runtime_error & e) {
            problems.push_back(line + ": " + e.what());
        } catch (const incomplete &) {
            problems.push_back(line + ": misses words");
        }
    }
    for (const engine_module & m : schema_modules()) {
        for (const engine_key & k : m.keys) {
            try {
                if (k.reset) {
                    schema_parse(m.name + " " + k.name, *k.reset);
                }
            } catch (const std::runtime_error & e) {
                problems.push_back(std::string("reset value of ") + e.what());
            }
        }
    }
    if (!problems.empty()) {
        return join(problems, "\n");
    }
    factory_layer() = factory;
    const layer none;
    for (const engine_module & m : schema_modules()) {
        const std::string reason = m.validate ? m.validate(reader(none)) : "";
        if (!reason.empty()) {
            problems.push_back(m.name + ": " + reason);
        }
    }
    if (!problems.empty()) {
        factory_layer().clear();
    }
    return join(problems, "\n");
}

void engine_start(const std::string & path, const std::string & version) {
    std::lock_guard<std::mutex> lock(g_mutex);
    archive_open(path);
    g_version = version;
    words_t skipped;
    g_running = startup_layer(&skipped);
    for (const std::string & s : skipped) {
        log_error("Engine", "%s skips %s", archive_path().c_str(), s.c_str());
    }
    for (const engine_module & m : schema_modules()) {
        if (m.apply) {
            m.apply(reader(g_running));
        }
    }
}

// the shortest value masked as a secret: a shorter one is no key, and would
// mask common text
static const size_t SECRET_SHORTEST = 8;

std::string engine_mask(const std::string & text) {
    words_t secrets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const reader                config(g_running);
        for (const engine_module & m : schema_modules()) {
            for (const engine_key & k : m.keys) {
                if (k.kind != ENGINE_SECRET) {
                    continue;
                }
                for (const std::string & item : k.named ? config.names(m.name) : words_t{ "" }) {
                    const auto value = config.get(m.name + " " + k.name, item);
                    if (value && value->size() >= SECRET_SHORTEST) {
                        secrets.push_back(*value);
                    }
                }
            }
        }
    }
    std::sort(secrets.begin(), secrets.end(),
              [](const std::string & a, const std::string & b) { return a.size() > b.size(); });
    std::string out = text;
    for (const std::string & s : secrets) {
        for (size_t at = out.find(s); at != std::string::npos; at = out.find(s, at + std::strlen(REMOVED))) {
            out.replace(at, s.size(), REMOVED);
        }
    }
    return out;
}

// a batch that changes the running configuration says who changed it, as IOS
// logs a configuration from the console
engine_result engine_run(const std::string & text, engine_role role, const engine_typed & typed) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const layer                 before = g_running;
    const engine_result         r      = batch(g_running, text, role, true, typed);
    if (r.ok && !(before == g_running)) {
        log_info("Engine", "Configured by %s", role == ENGINE_ADMIN ? "the administrator" : "an MCP client");
    }
    return r;
}

std::optional<std::string> engine_get(const std::string & key, const std::string & item) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return reader(g_running).get(key, item);
}

void engine_with(const std::function<void(const engine_reader &)> & read) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read(reader(g_running));
}

std::vector<std::string> engine_next(const std::string & head) {
    std::lock_guard<std::mutex> lock(g_mutex);
    try {
        const auto [line, spec] = split_filter(head);
        if (spec) {
            return trim(*spec).empty() ? filter_names() : words_t{};
        }
        return next_words(tokenize(line));
    } catch (...) {
        // a line still being typed may not read yet: nothing follows it
        return {};
    }
}
