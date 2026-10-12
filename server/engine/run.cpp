// run.cpp: the batch: every line compiled, then run on a draft, then applied

#include "engine/run.h"

#include "engine/archive.h"
#include "engine/filter.h"
#include "engine/parse.h"
#include "engine/registry.h"
#include "engine/schema.h"
#include "log.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <stdexcept>
#include <tuple>

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

// the running configuration over the defaults, the version, and the lock that
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

// the url of the item of a stored key, in the draft, when its module has one
static std::string url_of(const layer & draft, const std::string & s) {
    const auto        parts  = unstore(s);
    const std::string module = parts.first.substr(0, parts.first.find(' '));
    if (parts.second.empty() || !find_key(module + " " + URL_KEY)) {
        return "";
    }
    return reader(draft).get(module + " " + URL_KEY, parts.second).value_or("");
}

static std::vector<engine_ask> asked(const layer & draft, const words_t & keys) {
    std::vector<engine_ask> out;
    for (const std::string & s : keys) {
        out.push_back({ s, url_of(draft, s) });
    }
    return out;
}

// what a layer sends: every secret of an item with a url, with that url and
// its value
using sending = std::set<std::tuple<std::string, std::string, std::string>>;

static sending sent(const layer & l) {
    sending out;
    for (const engine_module & m : schema_modules()) {
        const std::string url = m.name + " " + URL_KEY;
        if (!find_key(url)) {
            continue;
        }
        for (const auto & kv : l) {
            const auto parts = unstore(kv.first);
            if (parts.first != url) {
                continue;
            }
            for (const engine_key & k : m.keys) {
                const auto held = l.find(stored(m.name + " " + k.name, parts.second));
                if (k.kind == ENGINE_SECRET && held != l.end()) {
                    out.insert({ held->first, kv.second, held->second });
                }
            }
        }
    }
    return out;
}

// on the lines of an MCP client, a secret goes only where it went before, in
// the running-config or the startup-config, or where the user saw it go as
// they typed it: a key a model moves or restores by a copy reaches no url the
// model wrote
static std::string moved_secret(const layer & before, const layer & draft, const engine_typed & typed) {
    sending known = sent(before);
    try {
        const sending startup = sent(startup_layer());
        known.insert(startup.begin(), startup.end());
    } catch (const std::runtime_error &) {
        // a startup-config that does not read vouches for nothing
    }
    for (const auto & t : sent(draft)) {
        const std::string & sk = std::get<0>(t);
        const auto          ty = typed.find(sk);
        if (known.count(t) || (ty != typed.end() && schema_parse(unstore(sk).first, ty->second) == std::get<2>(t))) {
            continue;
        }
        const auto parts = unstore(sk);
        return parts.first.substr(0, parts.first.find(' ')) + " " + parts.second + " would send its secret to " +
               std::get<1>(t) + ", where only the administrator or the user typing it sends it";
    }
    return "";
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
// to ask for stops it once every line ran, before the rules: the values the
// user types decide them. A batch of an MCP client moves no secret. A save
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
    if (!asks.empty()) {
        return { true, "", asked(draft, asks) };
    }
    if (role == ENGINE_MCP) {
        const std::string reason = moved_secret(target, draft, typed);
        if (!reason.empty()) {
            return failed(std::nullopt, alone, reason);
        }
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

layer startup_layer() {
    bool              found = false;
    const std::string text  = archive_read(found);
    layer             startup;
    if (!found) {
        return startup;
    }
    const engine_result r = batch(startup, text, ENGINE_ADMIN, false, {});
    if (!r.ok) {
        fail(archive_path() + ":\n" + r.text);
    }
    return startup;
}

std::string engine_defaults(const std::string & text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    layer                       defaults;
    words_t                     problems;
    for (const std::string & line : split_lines(text)) {
        try {
            const words_t words = tokenize(line);
            if (words.empty() || lower(words[0]) != "set") {
                fail("the defaults hold set lines only");
            }
            const key_words kw = schema_read(words_t(words.begin() + 1, words.end()));
            if (kw.rest.empty()) {
                fail(kw.key + " misses its value");
            }
            defaults[stored(kw.key, kw.item)] = schema_parse(kw.key, join(kw.rest, " "));
        } catch (const std::runtime_error & e) {
            problems.push_back(line + ": " + e.what());
        } catch (const incomplete &) {
            problems.push_back(line + ": misses words");
        }
    }
    if (problems.empty()) {
        defaults_layer() = defaults;
    }
    return join(problems, "\n");
}

engine_result engine_start(const std::string & path, const std::string & version) {
    std::lock_guard<std::mutex> lock(g_mutex);
    archive_open(path);
    g_version = version;
    try {
        g_running = startup_layer();
    } catch (const std::runtime_error & e) {
        return { false, e.what(), {} };
    }
    for (const engine_module & m : schema_modules()) {
        if (m.apply) {
            m.apply(reader(g_running));
        }
    }
    return { true, "", {} };
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
