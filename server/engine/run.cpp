// run.cpp: the batch: every line compiled, then run on a draft, then applied

#include "engine/run.h"

#include "engine/archive.h"
#include "engine/complete.h"
#include "engine/filter.h"
#include "engine/parse.h"
#include "engine/registry.h"
#include "engine/schema.h"
#include "lib/library.h"
#include "lib/log.h"
#include "lib/quit.h"

#include <algorithm>
#include <chrono>
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

const std::string & engine_version() {
    return g_version;
}

struct step {
    step_t                  run;
    std::optional<filter_t> filter;
};

// a line as the schema alone reads it: the command and its arguments checked,
// one that runs alone refused among others, its filter compiled; a lone line
// that stops short lists what may follow it in the configuration given
static step compile(const std::string & text, bool alone, const layer & target) {
    const auto [head, spec]       = split_filter(text);
    const words_t           words = tokenize(head);
    std::optional<filter_t> filter;
    if (spec) {
        filter = compile_filter(*spec);
    }
    try {
        const auto [cmd, args] = resolve(words);
        if (cmd->alone && !alone) {
            fail(join(cmd->path, " ") + " runs alone, on a line of its own");
        }
        return { cmd->compile(args), filter };
    } catch (const incomplete &) {
        const std::string next = join(complete(words, "", reader(target), {}), " ");
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
        if (!parts.second.empty() && parts.first == module + " " + URL_KEY && kv.second) {
            out.insert({ kv.first, *kv.second });
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
        engine_ask        ask{ u.first, u.second, true, {}, "" };
        for (const engine_key & k : find_module(module)->keys) {
            const std::string s    = stored(module + " " + k.name, parts.second);
            const auto        held = draft.find(s);
            if (k.kind == ENGINE_SECRET && held != draft.end() && held->second) {
                ask.along.push_back(s);
            }
        }
        out.push_back(ask);
    }
    return out;
}

// whether a change opens a guarded register: an enum moved toward its more
// open values, another key given a value
static bool opens(const engine_key &                 def,
                  const std::optional<std::string> & from,
                  const std::optional<std::string> & to) {
    if (def.kind != ENGINE_ENUM) {
        return to && to != from;
    }
    const auto at = [&](const std::optional<std::string> & v) {
        return std::find(def.values.begin(), def.values.end(), v.value_or("")) - def.values.begin();
    };
    return at(to) > at(from);
}

// the first key of the administrator a batch of an MCP client changes, read
// through the layers before and after it, whatever line changed it, none when
// it changes none
static std::optional<std::string> administered(const layer & before, const layer & draft) {
    const reader from(before), to(draft);
    for (const std::string & s : changed(before, draft)) {
        const auto [key, item] = unstore(s);
        const engine_key * def = find_key(key);
        if (def && def->admin && from.reveal(key, item) != to.reveal(key, item)) {
            return s;
        }
    }
    return std::nullopt;
}

// the guarded registers an MCP client opens, read through the layers before
// and after the batch, whatever line moved them: a yes of the user each, the
// user not yet letting it in this call
static std::vector<engine_ask> unguarded(const layer & before, const layer & draft, const engine_typed & typed) {
    std::vector<engine_ask> out;
    const reader            from(before), to(draft);
    for (const std::string & s : changed(before, draft)) {
        const auto [key, item] = unstore(s);
        const engine_key * def = find_key(key);
        const auto         let = typed.find(s);
        if (!def || !def->guard || (let != typed.end() && let->second == YES)) {
            continue;
        }
        const auto value = to.reveal(key, item);
        if (opens(*def, from.reveal(key, item), value)) {
            out.push_back({ s, "", true, {}, show_line(s, *value) });
        }
    }
    return out;
}

static std::vector<engine_ask> asked(const layer & draft, const words_t & keys) {
    std::vector<engine_ask> out;
    for (const std::string & s : keys) {
        out.push_back({ s, url_of(draft, s), false, {}, "" });
    }
    return out;
}

bool engine_confirmed(context & ctx, const std::string & key, const std::string & question) {
    const auto yes = ctx.typed.find(key);
    if (ctx.role != ENGINE_MCP || (yes != ctx.typed.end() && yes->second == YES)) {
        return true;
    }
    ctx.questions.push_back({ key, "", true, {}, question });
    return false;
}

bool engine_save(context & ctx) {
    const std::string copy = std::string("copy ") + RUNNING_CONFIG + " " + STARTUP_CONFIG;
    if (!engine_confirmed(ctx, copy, std::string("copy ") + RUNNING_CONFIG + " to " + STARTUP_CONFIG)) {
        return false;
    }
    ctx.save = true;
    return true;
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
// to ask for, a url of an MCP client the user has not let, a guarded register
// it opens, or a yes a command of an MCP client waits for, stops it once every
// line ran, before the rules:
// what the user answers decides them. A batch of an MCP client moves no
// secret and changes no key of the administrator, in the configuration that
// runs nor in the one the next start runs. A save writes the draft as the batch leaves it, an erase removes
// server.conf, and the conversations erased go, once the rules hold and
// before it replaces the target
static engine_result batch(layer &                                                  target,
                           const std::string &                                      text,
                           engine_role                                              role,
                           bool                                                     running,
                           const engine_typed &                                     typed,
                           const std::string &                                      conversation,
                           const std::map<std::string, std::vector<engine_group>> & sourced) {
    words_t lines = split_lines(text);
    if (lines.empty()) {
        lines.push_back("");
    }
    const bool        alone = lines.size() == 1;
    std::vector<step> steps;
    for (size_t i = 0; i < lines.size(); i++) {
        try {
            steps.push_back(compile(lines[i], alone, target));
        } catch (const std::runtime_error & e) {
            return failed(i, alone, e.what());
        }
    }
    layer   draft = target;
    words_t asks;
    context ctx{ draft, role, typed, asks };
    ctx.sourced      = sourced;
    ctx.conversation = conversation;
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
    if (role == ENGINE_MCP) {
        // the configuration that runs, then the one the next start runs: a
        // save writes the draft, an erase brings the factory configuration
        auto held = administered(target, draft);
        if (!held && (ctx.save || ctx.erase)) {
            held = administered(startup_layer(), ctx.erase ? layer() : draft);
        }
        if (held) {
            return failed(std::nullopt, alone, *held + " is the administrator's -> server.conf or the console");
        }
    }
    std::vector<engine_ask> wanted = role == ENGINE_MCP ? unlet(target, draft, typed) : std::vector<engine_ask>{};
    if (role == ENGINE_MCP) {
        const auto opened = unguarded(target, draft, typed);
        wanted.insert(wanted.end(), opened.begin(), opened.end());
    }
    const auto typing = asked(draft, asks);
    wanted.insert(wanted.end(), typing.begin(), typing.end());
    wanted.insert(wanted.end(), ctx.questions.begin(), ctx.questions.end());
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
    try {
        if (ctx.save) {
            out.push_back(archive_write(draft));
        }
        if (ctx.erase) {
            out.push_back(archive_erase());
        }
        library_remove(ctx.forget);
    } catch (const std::runtime_error & e) {
        return failed(std::nullopt, alone, e.what());
    }
    const words_t               change  = diff(target, draft);
    const std::set<std::string> keys    = changed(target, draft);
    const auto                  touched = [&](const engine_module & m) {
        return running && std::any_of(keys.begin(), keys.end(),
                                                       [&](const std::string & k) { return starts_with(k, m.name + " "); });
    };
    // a module whose keys changed readies the server last, nothing applied
    // when it cannot; a batch that saves or erases runs alone and changes no
    // key a module readies, so nothing it wrote waits on this
    for (const engine_module & m : schema_modules()) {
        const std::string why = touched(m) && m.prepare ? m.prepare(reader(draft)) : "";
        if (!why.empty()) {
            return failed(std::nullopt, alone, m.name + ": " + why);
        }
    }
    target = draft;
    for (const engine_module & m : schema_modules()) {
        if (touched(m) && m.apply) {
            m.apply(reader(target));
        }
    }
    out.insert(out.end(), change.begin(), change.end());
    return { true, out.empty() ? (ctx.compact ? "" : comment("no change")) : join(out, "\n"), {}, ctx.compact };
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
            compile(line, false, target);
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
            const engine_result r = batch(target, join(it->second, "\n"), ENGINE_ADMIN, false, {}, "", {});
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

// the items the sources of a module serve, asked for the running
// configuration as it is, outside the lock, as they ask over the network, and
// kept a few seconds for that configuration, as completion asks them on every
// word typed
static const auto SOURCED_KEPT = std::chrono::seconds(10);

struct sourced_t {
    layer                                 at;
    std::chrono::steady_clock::time_point when;
    std::vector<engine_group>             groups;
    bool                                  asking = false;
};

static std::mutex                       g_sourced_mutex;
static std::map<std::string, sourced_t> g_sourced;

static layer running_now() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_running;
}

static bool fresh(const sourced_t & k, const layer & now) {
    return !k.groups.empty() && k.at == now && std::chrono::steady_clock::now() - k.when < SOURCED_KEPT;
}

// the groups the sources serve for the configuration as it runs, asked now
// when the cache holds none fresh
static std::vector<engine_group> sourced(const engine_module & m) {
    const layer now = running_now();
    {
        std::lock_guard<std::mutex> lock(g_sourced_mutex);
        const auto                  it = g_sourced.find(m.name);
        if (it != g_sourced.end() && fresh(it->second, now)) {
            return it->second.groups;
        }
    }
    std::vector<engine_group> groups;
    try {
        groups = m.items(reader(now));
    } catch (const std::exception & e) {
        // a source that throws fails its module, so an item stays refused
        groups = {
            { m.name, {}, e.what() }
        };
    }
    std::lock_guard<std::mutex> lock(g_sourced_mutex);
    sourced_t &                 k = g_sourced[m.name];
    k.at                          = now;
    k.when                        = std::chrono::steady_clock::now();
    k.groups                      = groups;
    return groups;
}

// the groups the cache holds now, whatever their age, a stale cache asked
// again in a thread of its own, as completion never waits on the network: the
// words typed next complete with the answer, as the page completes with what
// its modules listed last
static std::vector<engine_group> sourced_now(const engine_module & m) {
    const layer                 now = running_now();
    std::lock_guard<std::mutex> lock(g_sourced_mutex);
    sourced_t &                 k = g_sourced[m.name];
    if (!fresh(k, now) && !k.asking) {
        k.asking = true;
        quit_thread([name = m.name, items = m.items, now] {
            std::vector<engine_group> groups;
            try {
                groups = items(reader(now));
            } catch (const std::exception & e) {
                // a source that throws fails its module
                groups = {
                    { name, {}, e.what() }
                };
            }
            std::lock_guard<std::mutex> held(g_sourced_mutex);
            g_sourced[name] = { now, std::chrono::steady_clock::now(), groups, false };
        });
    }
    return k.groups;
}

// the items every module with sources knows now, by module
static engine_known known_items() {
    engine_known out;
    for (const engine_module & m : schema_modules()) {
        if (m.items) {
            for (const engine_group & g : sourced_now(m)) {
                out[m.name].insert(out[m.name].end(), g.names.begin(), g.names.end());
            }
        }
    }
    return out;
}

// the groups of the modules the show and set lines of a batch read from their
// sources, asked before the batch holds the configuration
static std::map<std::string, std::vector<engine_group>> shown(const std::string & text) {
    std::map<std::string, std::vector<engine_group>> out;
    for (const std::string & raw : split_lines(text)) {
        try {
            const auto [line, spec] = split_filter(raw);
            const auto [c, args]    = resolve(tokenize(line));
            // show <module> lists its module, set admits an item the running
            // configuration does not hold against its module, show
            // running-config all lists every one
            std::vector<const engine_module *> listed;
            if (c->path == words_t{ "show" } && !args.empty()) {
                listed.push_back(find_module(schema_module(args[0])));
            } else if (c->path == words_t{ "set" }) {
                const key_words kw  = schema_read(args);
                const words_t   set = reader(running_now()).names(kw.module);
                if (!kw.item.empty() && std::find(set.begin(), set.end(), kw.item) == set.end()) {
                    listed.push_back(find_module(kw.module));
                }
            } else if (c->path == words_t{ "show", RUNNING_CONFIG } && !args.empty()) {
                for (const engine_module & m : schema_modules()) {
                    listed.push_back(&m);
                }
            }
            for (const engine_module * m : listed) {
                if (m && m->items && !out.count(m->name)) {
                    out[m->name] = sourced(*m);
                }
            }
        } catch (...) {
            // a line that does not read yet lists nothing; the batch tells why
        }
    }
    return out;
}

// a batch that changes the running configuration says who changed it, as IOS
// logs a configuration from the console
engine_result engine_run(const std::string &  text,
                         engine_role          role,
                         const engine_typed & typed,
                         const std::string &  conversation) {
    const auto                  groups = shown(text);
    std::lock_guard<std::mutex> lock(g_mutex);
    const layer                 before = g_running;
    const engine_result         r      = batch(g_running, text, role, true, typed, conversation, groups);
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

std::vector<std::string> engine_suggest(const std::string & text) {
    const engine_known          known = known_items();
    std::lock_guard<std::mutex> lock(g_mutex);
    try {
        const std::string partial = partial_word(text);
        const auto [line, spec]   = split_filter(text.substr(0, text.size() - partial.size()));
        if (spec) {
            words_t names;
            if (trim(*spec).empty()) {
                for (const std::string & n : filter_names()) {
                    if (starts_with(n, lower(partial))) {
                        names.push_back(n);
                    }
                }
            }
            return names;
        }
        return complete(tokenize(line), partial, reader(g_running), known);
    } catch (...) {
        // a line still being typed may not read yet: nothing follows it
        return {};
    }
}
