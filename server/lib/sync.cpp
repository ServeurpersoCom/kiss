// sync.cpp: the stream of events a page follows the conversations by, and
// the actions of its user

#include "lib/sync.h"

#include "engine/keychain.h"
#include "lib/agent.h"
#include "lib/library.h"
#include "lib/listener.h"
#include "lib/log.h"
#include "yyjson.h"

#include <algorithm>
#include <memory>

// how long a stream waits for a change before it writes a comment, which
// tells a page that left
static const int TICK_MS = 1000;

void sync_events(const httplib::Request & req, httplib::Response & res) {
    const unsigned    epoch = listener_epoch();
    const std::string from  = req.remote_addr;
    const auto        f     = std::make_shared<library_follower>();
    log_info("Sync", "%s follows the conversations", from.c_str());
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider("text/event-stream", [f, epoch, from](size_t, httplib::DataSink & sink) {
        const std::string events = library_follow(*f, TICK_MS, keychain_running());
        const std::string bytes  = events.empty() ? ":\n\n" : events;
        if (listener_epoch() != epoch || !sink.write(bytes.data(), bytes.size())) {
            log_info("Sync", "%s leaves", from.c_str());
            return false;
        }
        return true;
    });
}

// a string field of the body, false when it has none
static bool text_field(yyjson_val * body, const char * key, std::string & out) {
    yyjson_val * v = yyjson_obj_get(body, key);
    if (!yyjson_is_str(v)) {
        return false;
    }
    out = std::string(yyjson_get_str(v), yyjson_get_len(v));
    return true;
}

static const conversation_entry * entry_of(const conversation & c, const std::string & id) {
    for (const conversation_entry & e : c.entries) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

// the characters a title takes of the first message, as the page cuts it
static const size_t TITLE_LENGTH = 60;

// the first characters of a text, whole UTF-8 characters
static std::string cut(const std::string & text, size_t characters) {
    size_t i = 0;
    for (size_t n = 0; i < text.size(); i++) {
        if (((unsigned char) text[i] & 0xC0) != 0x80 && n++ == characters) {
            break;
        }
    }
    return text.substr(0, i);
}

// a message of the user entering a conversation after an entry, the model
// answering it; the conversation names it while it has no title
static std::string say(const std::string &                id,
                       const std::optional<std::string> & parent,
                       bool                               after_leaf,
                       const std::string &                text) {
    std::string       user;
    const std::string why = library_change(
        id,
        [&](conversation & c) {
            if (c.title.empty()) {
                c.title = cut(text, TITLE_LENGTH);
            }
            conversation_entry e;
            e.role = ROLE_USER;
            e.text = text;
            user   = c.entries[conversation_append(c, after_leaf ? c.leaf : parent, e)].id;
            return std::string();
        },
        keychain_running());
    return why.empty() ? agent_answer(id, user) : why;
}

// the message of a busy conversation, which waits for its turn to end
static const char * const BUSY = "a turn runs in this conversation";

// what the action does, the message of what keeps it from it, empty once
// done; created names a conversation a message opened
static std::string act(const std::string & action, yyjson_val * body, std::string & created) {
    std::string id, entry, text;
    if (action == "send") {
        if (!text_field(body, "text", text) || trim(text).empty()) {
            return "the body holds no text";
        }
        if (text_field(body, "id", id)) {
            if (agent_busy(id)) {
                return BUSY;
            }
            created = id;
            return say(id, std::nullopt, true, text);
        }
        conversation c;
        c.id                  = conversation_uuid();
        c.updated             = conversation_now();
        const std::string why = library_create(c, keychain_running());
        if (!why.empty()) {
            return why;
        }
        created = c.id;
        return say(c.id, std::nullopt, false, text);
    }
    if (action == "erase") {
        yyjson_val * ids = yyjson_obj_get(body, "ids");
        if (!yyjson_is_arr(ids)) {
            return "the body names no ids";
        }
        words_t known, gone;
        library_each([&](const conversation & c) { known.push_back(c.id); });
        size_t       i, n;
        yyjson_val * v;
        yyjson_arr_foreach(ids, i, n, v) {
            const std::string one = yyjson_is_str(v) ? yyjson_get_str(v) : "";
            if (std::find(known.begin(), known.end(), one) == known.end()) {
                return "unknown conversation \"" + one + "\"";
            }
            gone.push_back(one);
        }
        for (const std::string & one : gone) {
            agent_stop(one);
        }
        library_remove(gone);
        return "";
    }
    if (!text_field(body, "id", id)) {
        return "the body names no id";
    }
    if (action == "stop") {
        agent_stop(id);
        return "";
    }
    const words_t secrets = keychain_running();
    if (action == "pin") {
        yyjson_val * pinned = yyjson_obj_get(body, "pinned");
        if (!yyjson_is_bool(pinned)) {
            return "the body says no pinned";
        }
        return library_change(
            id,
            [&](conversation & c) {
                c.pinned = yyjson_get_bool(pinned);
                return std::string();
            },
            secrets);
    }
    if (!text_field(body, "entry", entry)) {
        return "the body names no entry";
    }
    if (agent_busy(id)) {
        return BUSY;
    }
    // the entry as the conversation holds it, a copy
    std::optional<conversation_entry> found;
    library_each([&](const conversation & c) {
        if (c.id == id) {
            const conversation_entry * e = entry_of(c, entry);
            if (e) {
                found = *e;
            }
        }
    });
    if (action == "edit") {
        if (!text_field(body, "text", text) || trim(text).empty()) {
            return "the body holds no text";
        }
        if (!found || found->role != ROLE_USER) {
            return "\"" + entry + "\" is not a message of the user";
        }
        return found->text == text ? "" : say(id, found->parent, false, text);
    }
    if (action == "retry") {
        if (!found || found->role == ROLE_CLI) {
            return "\"" + entry + "\" is not a message of the user nor an answer";
        }
        return agent_answer(id, found->role == ROLE_ASSISTANT ? *found->parent : entry);
    }
    if (action == "browse") {
        return library_change(
            id,
            [&](conversation & c) {
                if (!entry_of(c, entry)) {
                    return "unknown entry \"" + entry + "\"";
                }
                c.leaf = conversation_latest(c, entry);
                return std::string();
            },
            secrets);
    }
    bool              empty = false;
    const std::string why   = library_change(
        id,
        [&](conversation & c) {
            const conversation_entry * e = entry_of(c, entry);
            if (!e) {
                return "unknown entry \"" + entry + "\"";
            }
            if (e->role != ROLE_CLI) {
                return "\"" + entry + "\" is not a command";
            }
            conversation_drop(c, entry);
            empty = c.entries.empty();
            return std::string();
        },
        secrets);
    if (why.empty() && empty) {
        library_remove({ id });
    }
    return why;
}

void sync_action(const httplib::Request & req, httplib::Response & res, const std::string & action) {
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
    yyjson_val *                      body = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    std::string                       why  = yyjson_is_obj(body) ? "" : "the body is no JSON object";
    std::string                       created;
    try {
        if (why.empty()) {
            why = act(action, body, created);
        }
    } catch (const std::runtime_error & e) {
        why = e.what();
    }
    if (!why.empty()) {
        log_error("Sync", "%s %s: %s", req.remote_addr.c_str(), action.c_str(), why.c_str());
        res.status = 400;
        res.set_content(why + "\n", "text/plain");
        return;
    }
    log_info("Sync", "%s %s %s", req.remote_addr.c_str(), action.c_str(), req.body.c_str());
    if (action == "send") {
        res.set_content("{\"id\":\"" + created + "\"}", "application/json");
        return;
    }
    res.status = 204;
}
