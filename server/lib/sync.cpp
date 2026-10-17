// sync.cpp: the stream of events a page follows the conversations by, and
// the actions of its user

#include "lib/sync.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "lib/agent.h"
#include "lib/library.h"
#include "lib/listener.h"
#include "lib/log.h"
#include "yyjson.h"

#include <algorithm>
#include <functional>
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

// what keeps an action from being applied: the HTTP status that tells why,
// none once applied
struct refusal {
    int         status = 0;
    std::string why;
};

static const int MALFORMED = 400;
static const int UNKNOWN   = 404;
static const int CONFLICT  = 409;
static const int FAILED    = 500;

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

// whether the library holds a conversation
static bool known(const std::string & id) {
    bool found = false;
    library_each([&](const conversation & c) { found = found || c.id == id; });
    return found;
}

// a conversation changed as change says, change refusing with its own status;
// one gone is unknown, a file not written failed
static refusal changed(const std::string & id, const std::function<refusal(conversation &)> & change) {
    refusal           r;
    const std::string why = library_change(
        id,
        [&](conversation & c) {
            r = change(c);
            return r.why;
        },
        keychain_running());
    if (why.empty()) {
        return {};
    }
    return r.status ? r : refusal{ known(id) ? FAILED : UNKNOWN, why };
}

// the model answers a message of the user
static refusal answer(const std::string & id, const std::string & user) {
    const std::string why = agent_answer(id, user);
    return why.empty() ? refusal{} : refusal{ why == AGENT_BUSY ? CONFLICT : FAILED, why };
}

// a message of the user entering a conversation after the entry parent
// names, else after the thread, the model answering it; the conversation
// takes its title from it while it has none
static refusal say(const std::string &                               id,
                   const std::string &                               text,
                   const std::optional<std::optional<std::string>> & parent = std::nullopt) {
    std::string   user;
    const refusal r = changed(id, [&](conversation & c) {
        if (c.title.empty()) {
            c.title = cut(text, TITLE_LENGTH);
        }
        conversation_entry e;
        e.role = ROLE_USER;
        e.text = text;
        user   = c.entries[conversation_append(c, parent ? *parent : c.leaf, e)].id;
        return refusal{};
    });
    return r.status ? r : answer(id, user);
}

// a response to a form: accept with its content, or cancel
static bool a_response(yyjson_val * response) {
    yyjson_val *      action = yyjson_obj_get(response, "action");
    const std::string a      = yyjson_is_str(action) ? yyjson_get_str(action) : "";
    return (a == "accept" && yyjson_is_obj(yyjson_obj_get(response, "content"))) || a == "cancel";
}

// what the action does, what keeps it from it as the body, the conversation
// and the turn stand, in that order; created names the conversation a line
// went in, opened by it when the page named none
static refusal act(const std::string & action, yyjson_val * body, std::string & created, words_t & words) {
    std::string id, entry, text;
    const bool  named = text_field(body, "id", id);
    if (named && !known(id)) {
        return { UNKNOWN, "unknown conversation \"" + id + "\"" };
    }
    if (action == "send" || action == "command") {
        const bool  send = action == "send";
        std::string line;
        if (!text_field(body, send ? "text" : "input", line) || trim(line).empty()) {
            return { MALFORMED, send ? "the body holds no text" : "the body holds no input" };
        }
        if (named && agent_busy(id)) {
            return { CONFLICT, AGENT_BUSY };
        }
        if (!named) {
            // a conversation the line opens, without a title until a message names it
            conversation c;
            c.id                  = conversation_uuid();
            c.updated             = conversation_now();
            const std::string why = library_create(c, keychain_running());
            if (!why.empty()) {
                return { FAILED, why };
            }
            id = c.id;
        }
        created = id;
        if (send) {
            return say(id, line);
        }
        // a line this server runs as its config tool does
        const std::string why = agent_command(id, line);
        return why.empty() ? refusal{} : refusal{ why == AGENT_BUSY ? CONFLICT : FAILED, why };
    }
    if (action == "complete") {
        std::string line;
        if (!text_field(body, "line", line)) {
            return { MALFORMED, "the body holds no line" };
        }
        words = engine_suggest(line);
        return {};
    }
    if (action == "erase") {
        yyjson_val * ids = yyjson_obj_get(body, "ids");
        if (!yyjson_is_arr(ids)) {
            return { MALFORMED, "the body names no ids" };
        }
        words_t      gone;
        size_t       i, n;
        yyjson_val * v;
        yyjson_arr_foreach(ids, i, n, v) {
            const std::string one = yyjson_is_str(v) ? yyjson_get_str(v) : "";
            if (!known(one)) {
                return { UNKNOWN, "unknown conversation \"" + one + "\"" };
            }
            gone.push_back(one);
        }
        for (const std::string & one : gone) {
            agent_stop(one);
        }
        try {
            library_remove(gone);
        } catch (const std::runtime_error & e) {
            return { FAILED, e.what() };
        }
        return {};
    }
    if (!named) {
        return { MALFORMED, "the body names no id" };
    }
    if (action == "stop") {
        agent_stop(id);
        return {};
    }
    if (action == "answer") {
        yyjson_val * response = yyjson_obj_get(body, "response");
        std::string  verdict;
        if (response) {
            if (!a_response(response)) {
                return { MALFORMED, "the response is not accept with its content, nor cancel" };
            }
            const std::shared_ptr<char> json(yyjson_val_write(response, 0, nullptr), free);
            const std::string           why = agent_fill(id, json.get());
            return why.empty() ? refusal{} : refusal{ CONFLICT, why };
        }
        if (!text_field(body, "verdict", verdict)) {
            return { MALFORMED, "the body names no verdict and no response" };
        }
        if (std::find_if(std::begin(AGENT_VERDICTS), std::end(AGENT_VERDICTS),
                         [&](const char * v) { return verdict == v; }) == std::end(AGENT_VERDICTS)) {
            return { MALFORMED, "\"" + verdict + "\" is not once, always or refuse" };
        }
        const std::string why = agent_grant(id, verdict);
        return why.empty() ? refusal{} : refusal{ CONFLICT, why };
    }
    if (action == "pin") {
        yyjson_val * pinned = yyjson_obj_get(body, "pinned");
        if (!yyjson_is_bool(pinned)) {
            return { MALFORMED, "the body says no pinned" };
        }
        return changed(id, [&](conversation & c) {
            c.pinned = yyjson_get_bool(pinned);
            return refusal{};
        });
    }
    if (!text_field(body, "entry", entry)) {
        return { MALFORMED, "the body names no entry" };
    }
    if (action == "edit" && (!text_field(body, "text", text) || trim(text).empty())) {
        return { MALFORMED, "the body holds no text" };
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
    if (!found) {
        return { UNKNOWN, "unknown entry \"" + entry + "\"" };
    }
    if (action == "edit" && found->role != ROLE_USER) {
        return { MALFORMED, "\"" + entry + "\" is not a message of the user" };
    }
    if (action == "retry" && found->role == ROLE_CLI) {
        return { MALFORMED, "\"" + entry + "\" is not a message of the user nor an answer" };
    }
    if (action == "drop" && found->role != ROLE_CLI) {
        return { MALFORMED, "\"" + entry + "\" is not a command" };
    }
    if (agent_busy(id)) {
        return { CONFLICT, AGENT_BUSY };
    }
    if (action == "edit") {
        return found->text == text ? refusal{} : say(id, text, found->parent);
    }
    if (action == "retry") {
        return answer(id, found->role == ROLE_ASSISTANT ? *found->parent : entry);
    }
    if (action == "browse") {
        return changed(id, [&](conversation & c) {
            c.leaf = conversation_latest(c, entry);
            return refusal{};
        });
    }
    bool          empty = false;
    const refusal r     = changed(id, [&](conversation & c) {
        conversation_drop(c, entry);
        empty = c.entries.empty();
        return refusal{};
    });
    if (!r.status && empty) {
        library_remove({ id });
    }
    return r;
}

void sync_action(const httplib::Request & req, httplib::Response & res, const std::string & action) {
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
    yyjson_val *                      body = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    std::string                       created;
    words_t                           words;
    const refusal                     r =
        yyjson_is_obj(body) ? act(action, body, created, words) : refusal{ MALFORMED, "the body is no JSON object" };
    yyjson_val *      id    = yyjson_obj_get(body, "id");
    const std::string named = yyjson_is_str(id) ? yyjson_get_str(id) : created;
    if (r.status) {
        log_error("Sync", "%s %s %.8s: %s", req.remote_addr.c_str(), action.c_str(), named.c_str(), r.why.c_str());
        res.status = r.status;
        res.set_content(r.why + "\n", "text/plain");
        return;
    }
    if (action == "complete") {
        const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
        yyjson_mut_val *                      out  = yyjson_mut_obj(made.get());
        yyjson_mut_val *                      list = yyjson_mut_obj_add_arr(made.get(), out, "words");
        yyjson_mut_doc_set_root(made.get(), out);
        for (const std::string & w : words) {
            yyjson_mut_arr_add_strncpy(made.get(), list, w.data(), w.size());
        }
        const std::shared_ptr<char> json(yyjson_mut_write(made.get(), 0, nullptr), free);
        res.set_content(json.get(), "application/json");
        return;
    }
    log_info("Sync", "%s %s %.8s", req.remote_addr.c_str(), action.c_str(), named.c_str());
    if (!created.empty()) {
        res.set_content("{\"id\":\"" + created + "\"}", "application/json");
        return;
    }
    res.status = 204;
}
