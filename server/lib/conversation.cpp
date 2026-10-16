// conversation.cpp: the conversations as a file, read whole or refused with
// where it goes wrong, check for check as conversation.ts of the page reads it

#include "lib/conversation.h"

#include "engine/parse.h"
#include "yyjson.h"

#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

// the marker a file opens on, naming what it holds
static const char * const MARK = "kiss";
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
            { { "opaque", opaque } });
        const check_t  stats = object({
            { "tokens",     number },
            { "generation", number },
            { "system",     number }
        });
        const fields_t link  = {
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
            { "user", object(with({ { "role", constant("user") }, { "text", string } })) },
            { "assistant", object(with({ { "role", constant("assistant") }, { "rounds", array(round) } }),
             { { "error", string }, { "stats", stats } }) },
            { "cli",
             object(with(
                  { { "role", constant("cli") }, { "input", string }, { "output", string }, { "ok", boolean } })) },
        };
        const check_t entry = [messages](yyjson_val * v, const std::string & at) {
            yyjson_val *    role    = yyjson_is_obj(v) ? yyjson_obj_get(v, "role") : nullptr;
            const check_t * variant = yyjson_is_str(role) ? field(messages, yyjson_get_str(role)) : nullptr;
            if (!variant) {
                wrong(at + ".role", "not one of user assistant cli");
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

static std::string text_of(yyjson_val * v) {
    return std::string(yyjson_get_str(v), yyjson_get_len(v));
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
        const std::string id    = text_of(yyjson_obj_get(e, "id"));
        yyjson_val *      p     = yyjson_obj_get(e, "parent");
        if (seen.count(id)) {
            wrong(where + ".id", "used twice");
        }
        if (yyjson_is_str(p)) {
            if (!seen.count(text_of(p))) {
                wrong(where + ".parent", "names no entry before it");
            }
            parents.insert(text_of(p));
        }
        seen.insert(id);
    }
    if (yyjson_is_null(leaf) ? n > 0 : !seen.count(text_of(leaf))) {
        wrong(at + ".leaf", "names no entry");
    }
    if (yyjson_is_str(leaf) && parents.count(text_of(leaf))) {
        wrong(at + ".leaf", "ends no branch");
    }
}

std::vector<conversation> conversation_parse(const std::string & text) {
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(text.data(), text.size(), 0), yyjson_doc_free);
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
        const std::string id = text_of(yyjson_obj_get(c, "id"));
        if (seen.count(id)) {
            wrong(at + ".id", "used twice");
        }
        seen.insert(id);
        tree(c, at);
        out.push_back({ id, text_of(yyjson_obj_get(c, "title")), yyjson_get_num(yyjson_obj_get(c, "updated")),
                        yyjson_obj_get(c, "pinned") != nullptr });
    }
    return out;
}

std::string conversation_titled(const std::string & title) {
    return title.empty() ? "" : " " + quote(title);
}
