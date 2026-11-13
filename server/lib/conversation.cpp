// conversation.cpp: the conversations as a file, read whole or refused with
// where it goes wrong, check for check as conversation.ts of the page reads
// it, and written as the page writes it; the tree of entries

#include "lib/conversation.h"

#include "engine/parse.h"
#include "engine/seal.h"
#include "lib/json.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

// the marker a file opens on, naming what it holds
static const char * const MARK = "kiss";

// a role as a file names it
static const char * const USER      = "user";
static const char * const ASSISTANT = "assistant";
static const char * const CLI       = "cli";

// the root of a place named in an error
static const char * const ROOT = "file";

// a check of one value, throwing where it goes wrong
using check_t  = std::function<void(yyjson_val *, const std::string &)>;
using fields_t = std::vector<std::pair<std::string, check_t>>;

[[noreturn]] static void wrong(const std::string & at, const std::string & what) {
    throw std::runtime_error(at + ": " + what);
}

static void string(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_str(v)) {
        wrong(at, "not a string");
    }
}

static void boolean(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_bool(v)) {
        wrong(at, "not a boolean");
    }
}

static void number(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_num(v)) {
        wrong(at, "not a number");
    }
}

// a whole number, none below zero
static void count(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_uint(v) && !(yyjson_is_sint(v) && yyjson_get_sint(v) >= 0)) {
        wrong(at, "not a count");
    }
}

static void parent(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_null(v) && !yyjson_is_str(v)) {
        wrong(at, "not an id or null");
    }
}

static void record(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_obj(v)) {
        wrong(at, "not an object");
    }
}

static check_t constant(const std::string & value) {
    return [value](yyjson_val * v, const std::string & at) {
        if (!yyjson_is_str(v) || value != yyjson_get_str(v)) {
            wrong(at, "not " + value);
        }
    };
}

static void yes(yyjson_val * v, const std::string & at) {
    if (!yyjson_is_true(v)) {
        wrong(at, "not true");
    }
}

static const check_t * field(const fields_t & fields, const std::string & name) {
    for (const auto & f : fields) {
        if (f.first == name) {
            return &f.second;
        }
    }
    return nullptr;
}

// an object holding exactly its fields, those in optional when it has them
static check_t object(fields_t fields, fields_t optional = {}) {
    return [fields, optional](yyjson_val * v, const std::string & at) {
        record(v, at);
        size_t       i, n;
        yyjson_val * k;
        yyjson_val * x;
        yyjson_obj_foreach(v, i, n, k, x) {
            const std::string key = yyjson_get_str(k);
            if (!field(fields, key) && !field(optional, key)) {
                wrong(at + "." + key, "unknown");
            }
        }
        for (const auto & f : fields) {
            yyjson_val * value = yyjson_obj_get(v, f.first.c_str());
            if (!value) {
                wrong(at + "." + f.first, "missing");
            }
            f.second(value, at + "." + f.first);
        }
        for (const auto & f : optional) {
            yyjson_val * value = yyjson_obj_get(v, f.first.c_str());
            if (value) {
                f.second(value, at + "." + f.first);
            }
        }
    };
}

static check_t array(check_t check) {
    return [check](yyjson_val * v, const std::string & at) {
        if (!yyjson_is_arr(v)) {
            wrong(at, "not an array");
        }
        size_t       i, n;
        yyjson_val * x;
        yyjson_arr_foreach(v, i, n, x) {
            check(x, at + "[" + std::to_string(i) + "]");
        }
    };
}

// the file: the marker, then conversations, each a tree of entries, an entry
// by its role, each role its own fields beside the link of the entry
static const check_t & file() {
    static const check_t checked = [] {
        const check_t image = object({
            { "mime", string },
            { "data", string }
        });
        // a stored call ran: it holds its outcome
        const check_t call  = object(
            {
                { "id",     string  },
                { "name",   string  },
                { "args",   string  },
                { "result", string  },
                { "ok",     boolean }
        },
            { { "images", array(image) } });
        const check_t opaque = object({
            { "protocol", string        },
            { "items",    array(record) }
        });
        const check_t round  = object(
            {
                { "reasoning", string      },
                { "text",      string      },
                { "calls",     array(call) }
        },
            { { "opaque", opaque }, { "kept", count } });
        const check_t stats = object(
            {
                { "tokens",     number },
                { "generation", number },
                { "system",     number }
        },
            { { "context", number } });
        const fields_t link = {
            { "id",     string },
            { "parent", parent },
            { "time",   number }
        };
        const auto with = [&link](fields_t more) {
            fields_t all = link;
            all.insert(all.end(), more.begin(), more.end());
            return all;
        };
        const fields_t messages = {
            { USER, object(with({ { "role", constant(USER) }, { "text", string } })) },
            { ASSISTANT, object(with({ { "role", constant(ASSISTANT) }, { "rounds", array(round) } }),
             { { "error", string }, { "stats", stats }, { "compaction", yes } }) },
            { CLI, object(with(
                       { { "role", constant(CLI) }, { "input", string }, { "output", string }, { "ok", boolean } })) },
        };
        const check_t entry = [messages](yyjson_val * v, const std::string & at) {
            yyjson_val *    role    = yyjson_is_obj(v) ? yyjson_obj_get(v, "role") : nullptr;
            const check_t * variant = yyjson_is_str(role) ? field(messages, yyjson_get_str(role)) : nullptr;
            if (!variant) {
                wrong(at + ".role", std::string("not one of ") + USER + " " + ASSISTANT + " " + CLI);
            }
            (*variant)(v, at);
        };
        const check_t conversation = object(
            {
                { "id",      string       },
                { "title",   string       },
                { "updated", number       },
                { "leaf",    parent       },
                { "entries", array(entry) }
        },
            { { "pinned", yes } });
        return object({
            { MARK,            constant(CONVERSATIONS) },
            { "conversations", array(conversation)     }
        });
    }();
    return checked;
}

// a tree: every id once, every parent an entry before its child, so no cycle,
// and a leaf that ends a branch, none only when there is no entry
static void tree(yyjson_val * c, const std::string & at) {
    yyjson_val *          entries = yyjson_obj_get(c, "entries");
    yyjson_val *          leaf    = yyjson_obj_get(c, "leaf");
    std::set<std::string> seen;
    std::set<std::string> parents;
    size_t                i, n;
    yyjson_val *          e;
    yyjson_arr_foreach(entries, i, n, e) {
        const std::string where = at + ".entries[" + std::to_string(i) + "]";
        const std::string id    = json_text(yyjson_obj_get(e, "id"));
        yyjson_val *      p     = yyjson_obj_get(e, "parent");
        if (seen.count(id)) {
            wrong(where + ".id", "used twice");
        }
        if (yyjson_is_str(p)) {
            if (!seen.count(json_text(p))) {
                wrong(where + ".parent", "names no entry before it");
            }
            parents.insert(json_text(p));
        }
        seen.insert(id);
    }
    if (yyjson_is_null(leaf) ? n > 0 : !seen.count(json_text(leaf))) {
        wrong(at + ".leaf", "names no entry");
    }
    if (yyjson_is_str(leaf) && parents.count(json_text(leaf))) {
        wrong(at + ".leaf", "ends no branch");
    }
}

static std::string field_text(yyjson_val * o, const char * key) {
    return json_text(yyjson_obj_get(o, key));
}

static std::optional<std::string> link_of(yyjson_val * v) {
    return yyjson_is_str(v) ? std::optional<std::string>(json_text(v)) : std::nullopt;
}

static conversation_call call_of(yyjson_val * v) {
    conversation_call c{ field_text(v, "id"),
                         field_text(v, "name"),
                         field_text(v, "args"),
                         field_text(v, "result"),
                         yyjson_get_bool(yyjson_obj_get(v, "ok")),
                         {} };
    size_t            i, n;
    yyjson_val *      x;
    yyjson_arr_foreach(yyjson_obj_get(v, "images"), i, n, x) {
        c.images.push_back({ field_text(x, "mime"), field_text(x, "data") });
    }
    return c;
}

static conversation_round round_of(yyjson_val * v) {
    conversation_round r{ field_text(v, "reasoning"), field_text(v, "text"), {}, std::nullopt, std::nullopt };
    size_t             i, n;
    yyjson_val *       x;
    yyjson_arr_foreach(yyjson_obj_get(v, "calls"), i, n, x) {
        r.calls.push_back(call_of(x));
    }
    if (yyjson_val * o = yyjson_obj_get(v, "opaque")) {
        const std::string items = json_write(yyjson_obj_get(o, "items"));
        r.opaque                = conversation_opaque{ field_text(o, "protocol"), items };
    }
    if (yyjson_val * kept = yyjson_obj_get(v, "kept")) {
        r.kept = (int) yyjson_get_num(kept);
    }
    return r;
}

static conversation_entry entry_of(yyjson_val * v) {
    conversation_entry e;
    e.id                   = field_text(v, "id");
    e.parent               = link_of(yyjson_obj_get(v, "parent"));
    e.time                 = yyjson_get_num(yyjson_obj_get(v, "time"));
    const std::string role = field_text(v, "role");
    e.role                 = role == USER ? ROLE_USER : role == ASSISTANT ? ROLE_ASSISTANT : ROLE_CLI;
    size_t       i, n;
    yyjson_val * x;
    switch (e.role) {
        case ROLE_USER:
            e.text = field_text(v, "text");
            break;
        case ROLE_ASSISTANT:
            yyjson_arr_foreach(yyjson_obj_get(v, "rounds"), i, n, x) {
                e.rounds.push_back(round_of(x));
            }
            if (yyjson_val * error = yyjson_obj_get(v, "error")) {
                e.error = json_text(error);
            }
            e.compaction = yyjson_obj_get(v, "compaction") != nullptr;
            if (yyjson_val * stats = yyjson_obj_get(v, "stats")) {
                e.stats = conversation_stats{ yyjson_get_num(yyjson_obj_get(stats, "tokens")),
                                              yyjson_get_num(yyjson_obj_get(stats, "generation")),
                                              yyjson_get_num(yyjson_obj_get(stats, "system")), std::nullopt };
                if (yyjson_val * context = yyjson_obj_get(stats, "context")) {
                    e.stats->context = yyjson_get_num(context);
                }
            }
            break;
        case ROLE_CLI:
            e.input  = field_text(v, "input");
            e.output = field_text(v, "output");
            e.ok     = yyjson_get_bool(yyjson_obj_get(v, "ok"));
            break;
    }
    return e;
}

std::vector<conversation> conversation_parse(const std::string & text) {
    const json_doc doc = json_read(text);
    if (!doc) {
        throw std::runtime_error("not JSON");
    }
    yyjson_val * root = yyjson_doc_get_root(doc.get());
    file()(root, ROOT);
    std::vector<conversation> out;
    std::set<std::string>     seen;
    size_t                    i, n;
    yyjson_val *              c;
    yyjson_arr_foreach(yyjson_obj_get(root, "conversations"), i, n, c) {
        const std::string at = std::string(ROOT) + ".conversations[" + std::to_string(i) + "]";
        const std::string id = json_text(yyjson_obj_get(c, "id"));
        if (seen.count(id)) {
            wrong(at + ".id", "used twice");
        }
        seen.insert(id);
        tree(c, at);
        conversation got;
        got.id      = id;
        got.title   = field_text(c, "title");
        got.updated = yyjson_get_num(yyjson_obj_get(c, "updated"));
        got.pinned  = yyjson_obj_get(c, "pinned") != nullptr;
        got.leaf    = link_of(yyjson_obj_get(c, "leaf"));
        size_t       j, m;
        yyjson_val * e;
        yyjson_arr_foreach(yyjson_obj_get(c, "entries"), j, m, e) {
            got.entries.push_back(entry_of(e));
        }
        out.push_back(std::move(got));
    }
    return out;
}

// a number as the page writes it: whole as an integer, else as a float
static yyjson_mut_val * number_val(yyjson_mut_doc * d, double v) {
    const int64_t whole = (int64_t) v;
    return (double) whole == v ? yyjson_mut_sint(d, whole) : yyjson_mut_real(d, v);
}

static void add_link(yyjson_mut_doc * d, yyjson_mut_val * o, const char * key, const std::optional<std::string> & v) {
    if (v) {
        json_put(d, o, key, *v);
    } else {
        yyjson_mut_obj_add_null(d, o, key);
    }
}

// a call, its outcome once it ran
static yyjson_mut_val * call_val(yyjson_mut_doc * d, const conversation_call & c) {
    yyjson_mut_val * call = yyjson_mut_obj(d);
    json_put(d, call, "id", c.id);
    json_put(d, call, "name", c.name);
    json_put(d, call, "args", c.args);
    if (c.ok) {
        json_put(d, call, "result", c.result);
        yyjson_mut_obj_add_bool(d, call, "ok", *c.ok);
    }
    if (!c.images.empty()) {
        yyjson_mut_val * images = yyjson_mut_obj_add_arr(d, call, "images");
        for (const conversation_image & i : c.images) {
            yyjson_mut_val * image = yyjson_mut_arr_add_obj(d, images);
            json_put(d, image, "mime", i.mime);
            json_put(d, image, "data", i.data);
        }
    }
    return call;
}

static yyjson_mut_val * round_val(yyjson_mut_doc * d, const conversation_round & r) {
    yyjson_mut_val * o = yyjson_mut_obj(d);
    json_put(d, o, "reasoning", r.reasoning);
    json_put(d, o, "text", r.text);
    yyjson_mut_val * calls = yyjson_mut_obj_add_arr(d, o, "calls");
    for (const conversation_call & c : r.calls) {
        yyjson_mut_arr_append(calls, call_val(d, c));
    }
    if (r.opaque) {
        yyjson_mut_val * opaque = yyjson_mut_obj_add_obj(d, o, "opaque");
        json_put(d, opaque, "protocol", r.opaque->protocol);
        const json_doc items = json_read(r.opaque->items);
        yyjson_mut_obj_add_val(d, opaque, "items", yyjson_val_mut_copy(d, yyjson_doc_get_root(items.get())));
    }
    if (r.kept) {
        yyjson_mut_obj_add_int(d, o, "kept", *r.kept);
    }
    return o;
}

static yyjson_mut_val * entry_val(yyjson_mut_doc * d, const conversation_entry & e) {
    yyjson_mut_val * o = yyjson_mut_obj(d);
    switch (e.role) {
        case ROLE_USER:
            yyjson_mut_obj_add_str(d, o, "role", USER);
            json_put(d, o, "text", e.text);
            break;
        case ROLE_ASSISTANT:
            {
                yyjson_mut_obj_add_str(d, o, "role", ASSISTANT);
                yyjson_mut_val * rounds = yyjson_mut_obj_add_arr(d, o, "rounds");
                for (const conversation_round & r : e.rounds) {
                    yyjson_mut_arr_append(rounds, round_val(d, r));
                }
                if (e.error) {
                    json_put(d, o, "error", *e.error);
                }
                if (e.compaction) {
                    yyjson_mut_obj_add_true(d, o, "compaction");
                }
                if (e.stats) {
                    yyjson_mut_val * stats = yyjson_mut_obj_add_obj(d, o, "stats");
                    yyjson_mut_obj_add_val(d, stats, "tokens", number_val(d, e.stats->tokens));
                    yyjson_mut_obj_add_val(d, stats, "generation", number_val(d, e.stats->generation));
                    yyjson_mut_obj_add_val(d, stats, "system", number_val(d, e.stats->system));
                    if (e.stats->context) {
                        yyjson_mut_obj_add_val(d, stats, "context", number_val(d, *e.stats->context));
                    }
                }
                break;
            }
        case ROLE_CLI:
            yyjson_mut_obj_add_str(d, o, "role", CLI);
            json_put(d, o, "input", e.input);
            json_put(d, o, "output", e.output);
            yyjson_mut_obj_add_bool(d, o, "ok", e.ok);
            break;
    }
    json_put(d, o, "id", e.id);
    add_link(d, o, "parent", e.parent);
    yyjson_mut_obj_add_val(d, o, "time", number_val(d, e.time));
    return o;
}

static yyjson_mut_val * conversation_val(yyjson_mut_doc * d, const conversation & c) {
    yyjson_mut_val * o = yyjson_mut_obj(d);
    json_put(d, o, "id", c.id);
    json_put(d, o, "title", c.title);
    yyjson_mut_obj_add_val(d, o, "updated", number_val(d, c.updated));
    if (c.pinned) {
        yyjson_mut_obj_add_true(d, o, "pinned");
    }
    add_link(d, o, "leaf", c.leaf);
    yyjson_mut_val * entries = yyjson_mut_obj_add_arr(d, o, "entries");
    for (const conversation_entry & e : c.entries) {
        yyjson_mut_arr_append(entries, entry_val(d, e));
    }
    return o;
}

// a value written as JSON, every secret sealed as decoded
static std::string sealed(yyjson_mut_doc * d, yyjson_mut_val * root, const words_t & secrets) {
    yyjson_mut_doc_set_root(d, root);
    const std::string text = json_write(d);
    std::string       out;
    seal_json(text, secrets, out);
    return out;
}

std::string conversation_json(const conversation & c, const words_t & secrets) {
    const json_mut doc = json_new();
    return sealed(doc.get(), conversation_val(doc.get(), c), secrets);
}

std::string conversation_round_json(const conversation_round & r, const words_t & secrets) {
    const json_mut doc = json_new();
    return sealed(doc.get(), round_val(doc.get(), r), secrets);
}

std::string conversation_call_json(const conversation_call & c, const words_t & secrets) {
    const json_mut doc = json_new();
    return sealed(doc.get(), call_val(doc.get(), c), secrets);
}

std::string conversation_file(const std::vector<conversation> & conversations, const words_t & secrets) {
    const json_mut   doc  = json_new();
    yyjson_mut_val * root = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add_str(doc.get(), root, MARK, CONVERSATIONS);
    yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc.get(), root, "conversations");
    for (const conversation & c : conversations) {
        yyjson_mut_arr_append(list, conversation_val(doc.get(), c));
    }
    return sealed(doc.get(), root, secrets) + "\n";
}

std::vector<conversation_round> conversation_settled(const std::vector<conversation_round> & rounds) {
    std::vector<conversation_round> out;
    for (conversation_round r : rounds) {
        std::vector<conversation_call> calls;
        for (conversation_call c : r.calls) {
            if (!c.ok && c.sent) {
                c.result = STOPPED;
                c.ok     = false;
            }
            if (c.ok) {
                calls.push_back(std::move(c));
            }
        }
        r.calls = std::move(calls);
        if (r.kept ? !r.folding && !r.text.empty() : !r.reasoning.empty() || !r.text.empty() || !r.calls.empty()) {
            out.push_back(std::move(r));
        }
    }
    return out;
}

conversation conversation_stored(const conversation & c) {
    conversation out = c;
    for (conversation_entry & e : out.entries) {
        if (e.role == ROLE_ASSISTANT) {
            e.rounds = conversation_settled(e.rounds);
        }
    }
    return out;
}

std::string conversation_uuid() {
    static std::mutex   m;
    static std::mt19937 rng{ std::random_device{}() };
    unsigned char       b[16];
    {
        std::lock_guard<std::mutex> lock(m);
        for (unsigned char & x : b) {
            x = (unsigned char) rng();
        }
    }
    // the bits that say it is random, version 4, variant 10
    b[6]                          = (unsigned char) ((b[6] & 0x0f) | 0x40);
    b[8]                          = (unsigned char) ((b[8] & 0x3f) | 0x80);
    static const char * const HEX = "0123456789abcdef";
    std::string               out;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out += '-';
        }
        out += HEX[b[i] >> 4];
        out += HEX[b[i] & 15];
    }
    return out;
}

double conversation_now() {
    using namespace std::chrono;
    return (double) duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

size_t conversation_append(conversation & c, const std::optional<std::string> & parent, conversation_entry e) {
    e.id     = conversation_uuid();
    e.parent = parent;
    e.time   = conversation_now();
    c.leaf   = e.id;
    c.entries.push_back(std::move(e));
    return c.entries.size() - 1;
}

std::vector<conversation_entry> conversation_path(const conversation & c) {
    std::map<std::string, const conversation_entry *> by_id;
    for (const conversation_entry & e : c.entries) {
        by_id[e.id] = &e;
    }
    std::vector<conversation_entry> out;
    for (std::optional<std::string> id = c.leaf; id;) {
        const conversation_entry * e = by_id[*id];
        out.push_back(*e);
        id = e->parent;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string conversation_latest(const conversation & c, const std::string & id) {
    std::map<std::string, const conversation_entry *> by_id;
    for (const conversation_entry & e : c.entries) {
        by_id[e.id] = &e;
    }
    for (auto it = c.entries.rbegin(); it != c.entries.rend(); ++it) {
        for (const conversation_entry * e = &*it; e; e = e->parent ? by_id[*e->parent] : nullptr) {
            if (e->id == id) {
                return it->id;
            }
        }
    }
    return id;
}

void conversation_drop(conversation & c, const std::string & id) {
    std::optional<std::string> parent;
    for (const conversation_entry & e : c.entries) {
        if (e.id == id) {
            parent = e.parent;
        }
    }
    c.entries.erase(
        std::remove_if(c.entries.begin(), c.entries.end(), [&](const conversation_entry & e) { return e.id == id; }),
        c.entries.end());
    for (conversation_entry & e : c.entries) {
        if (e.parent == id) {
            e.parent = parent;
        }
    }
    if (c.leaf == id) {
        c.leaf = parent;
    }
}

std::string conversation_shown(const std::string & id) {
    return id.substr(0, ID_SHOWN);
}

std::string conversation_titled(const std::string & title) {
    return title.empty() ? "" : " " + quote(title);
}
