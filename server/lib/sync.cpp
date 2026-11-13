// sync.cpp: the stream of events a page follows the conversations by, and
// the actions of its user

#include "lib/sync.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "lib/agent.h"
#include "lib/http.h"
#include "lib/json.h"
#include "lib/library.h"
#include "lib/listener.h"
#include "lib/log.h"
#include "lib/mcp.h"
#include "lib/quit.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// how long a stream waits for a change before it looks whether the page is
// still there, and how long it stays silent at most, an empty message telling
// a page that waits it is still there
static const int TICK_MS = 1000;
static const int IDLE_MS = 15000;

// a page follows the conversations attached, running the lines the agent
// calls the config tool of the pages with while its stream lasts. One thread
// reads what the page sends: its first message, then the answers to the pings
// and its Close; this one waits for the first message, sends the events, and
// closes the stream once the page leaves, the listener moves or the process
// ends, which ends the read too
void sync_events(const httplib::Request &                                req,
                 httplib::ws::WebSocket &                                ws,
                 const std::function<std::string(const std::string &)> & admit) {
    const unsigned          epoch = listener_epoch();
    const std::string       from  = req.remote_addr;
    std::mutex              mutex;
    std::condition_variable said;
    std::string             first;
    bool                    heard = false;
    std::atomic<bool>       gone{ false };
    std::thread             reader([&] {
        std::string message;
        while (ws.read(message) != httplib::ws::Fail) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!heard) {
                first = message;
                heard = true;
                said.notify_all();
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        gone = true;
        said.notify_all();
    });
    const auto              open = [&] {
        return !gone && listener_epoch() == epoch && !quit_ending();
    };
    bool said_first = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!heard && open()) {
            said.wait_for(lock, std::chrono::milliseconds(TICK_MS));
        }
        said_first = heard;
    }
    const std::string why = said_first ? admit(first) : "the page sent no headers";
    if (!why.empty()) {
        log_error("HTTP", "Refused %s: %s", from.c_str(), why.c_str());
        ws.close(httplib::ws::CloseStatus::PolicyViolation, why);
        reader.join();
        return;
    }
    library_follower f;
    agent_attach();
    log_info("Sync", "%s follows the conversations", from.c_str());
    for (int idle = 0; open();) {
        const std::string events = library_follow(f, TICK_MS, keychain_known());
        idle                     = events.empty() ? idle + TICK_MS : 0;
        if ((!events.empty() || idle >= IDLE_MS) && !ws.send(events)) {
            break;
        }
        idle = idle >= IDLE_MS ? 0 : idle;
    }
    agent_detach();
    ws.close();
    reader.join();
    log_info("Sync", "%s leaves", from.c_str());
}

// what keeps an action from being applied: the HTTP status that tells why,
// none once applied
struct refusal {
    int         status = 0;
    std::string why;
};

static const int MALFORMED = httplib::StatusCode::BadRequest_400;
static const int UNKNOWN   = httplib::StatusCode::NotFound_404;
static const int CONFLICT  = httplib::StatusCode::Conflict_409;
static const int FAILED    = httplib::StatusCode::InternalServerError_500;

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
        keychain_known());
    if (why.empty()) {
        return {};
    }
    return r.status ? r : refusal{ known(id) ? FAILED : UNKNOWN, why };
}

// the model answers the message of the user that user names, writing it
// first when it enters with the turn
static refusal answer(const std::string &                                id,
                      const std::function<std::string(conversation &)> & user,
                      bool                                               compact = false) {
    const std::string why = agent_answer(id, user, compact);
    return why.empty() ? refusal{} : refusal{ why == AGENT_BUSY ? CONFLICT : FAILED, why };
}

// a message of the user entering a conversation after the entry parent
// names, else after the thread, the model answering it; the conversation
// takes its title from it while it has none
static refusal say(const std::string &                               id,
                   const std::string &                               text,
                   const std::optional<std::optional<std::string>> & parent = std::nullopt) {
    return answer(id, [&](conversation & c) {
        if (c.title.empty()) {
            c.title = cut(text, TITLE_LENGTH);
        }
        conversation_entry e;
        e.role = ROLE_USER;
        e.text = text;
        return c.entries[conversation_append(c, parent ? *parent : c.leaf, e)].id;
    });
}

// a response to a form: accept with its content, or cancel
static bool a_response(yyjson_val * response) {
    const std::string a = json_text(yyjson_obj_get(response, "action"));
    return (a == MCP_ACCEPT && yyjson_is_obj(yyjson_obj_get(response, "content"))) || a == MCP_CANCEL;
}

// an action as it runs: its body, the conversation it names, when it names
// one, and the entry it names; what it hands back, the conversation a line
// went in and the words that complete a line
struct acting {
    yyjson_val *           body = nullptr;
    std::string            id;
    bool                   named = false;
    std::string            entry;
    std::string            created;
    std::optional<words_t> words;
};

// nothing kept an action from being applied, or the status that tells why
static refusal unless(const std::string & why, int status) {
    return why.empty() ? refusal{} : refusal{ status, why };
}

// what keeps a turn from being taken: one that runs, else a file not written
static refusal taken(const std::string & why) {
    return unless(why, why == AGENT_BUSY ? CONFLICT : FAILED);
}

// a line entering a conversation, opened by it when the page named none: a
// message of the user, or a line this server runs as its config tool does
static refusal enter(acting & a, const char * field, const char * missing, bool message) {
    std::string line;
    if (!text_field(a.body, field, line) || trim(line).empty()) {
        return { MALFORMED, missing };
    }
    if (a.named && agent_busy(a.id)) {
        return { CONFLICT, AGENT_BUSY };
    }
    if (!a.named) {
        // a conversation the line opens, without a title until a message names it
        conversation c;
        c.id                  = conversation_uuid();
        c.updated             = conversation_now();
        const std::string why = library_create(c, keychain_known());
        if (!why.empty()) {
            return { FAILED, why };
        }
        a.id = c.id;
    }
    a.created = a.id;
    return message ? say(a.id, line) : taken(agent_command(a.id, line));
}

static refusal send(acting & a) {
    return enter(a, "text", "the body holds no text", true);
}

static refusal command(acting & a) {
    return enter(a, "input", "the body holds no input", false);
}

static refusal complete(acting & a) {
    std::string line;
    if (!text_field(a.body, "line", line)) {
        return { MALFORMED, "the body holds no line" };
    }
    a.words = engine_suggest(line);
    return {};
}

static refusal erase(acting & a) {
    yyjson_val * ids = yyjson_obj_get(a.body, "ids");
    if (!yyjson_is_arr(ids)) {
        return { MALFORMED, "the body names no ids" };
    }
    words_t      gone;
    size_t       i, n;
    yyjson_val * v;
    yyjson_arr_foreach(ids, i, n, v) {
        const std::string one = json_text(v);
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

static refusal stop(acting & a) {
    agent_stop(a.id);
    return {};
}

// the answer to the question a turn asks: what a page that ran a call
// answers, the response of the user to a form, or their verdict on a call
static refusal answered(acting & a) {
    yyjson_val * number = yyjson_obj_get(a.body, "question");
    if (!yyjson_is_uint(number)) {
        return { MALFORMED, "the body names no question" };
    }
    const uint64_t question = yyjson_get_uint(number);
    yyjson_val *   response = yyjson_obj_get(a.body, "response");
    yyjson_val *   ok       = yyjson_obj_get(a.body, "ok");
    std::string    verdict, output;
    if (text_field(a.body, "output", output)) {
        if (!yyjson_is_bool(ok)) {
            return { MALFORMED, "the body holds an output without ok" };
        }
        return unless(agent_ran(a.id, question, output, yyjson_get_bool(ok)), CONFLICT);
    }
    if (response) {
        if (!a_response(response)) {
            return { MALFORMED, "the response is neither accept with its content nor cancel" };
        }
        return unless(agent_fill(a.id, question, json_write(response)), CONFLICT);
    }
    if (!text_field(a.body, "verdict", verdict)) {
        return { MALFORMED, "the body names no verdict, no response and no output" };
    }
    if (std::find_if(std::begin(AGENT_VERDICTS), std::end(AGENT_VERDICTS),
                     [&](const char * v) { return verdict == v; }) == std::end(AGENT_VERDICTS)) {
        return { MALFORMED, "\"" + verdict + "\" is not " + AGENT_VERDICTS[0] + ", " + AGENT_VERDICTS[1] + " or " +
                                AGENT_VERDICTS[2] };
    }
    return unless(agent_grant(a.id, question, verdict), CONFLICT);
}

static refusal pin(acting & a) {
    yyjson_val * pinned = yyjson_obj_get(a.body, "pinned");
    if (!yyjson_is_bool(pinned)) {
        return { MALFORMED, "the body holds no pinned" };
    }
    return changed(a.id, [&](conversation & c) {
        c.pinned = yyjson_get_bool(pinned);
        return refusal{};
    });
}

// the entry an action names as the conversation holds it, a copy
static refusal held(const acting & a, conversation_entry & out) {
    bool found = false;
    library_each([&](const conversation & c) {
        const conversation_entry * e = c.id == a.id ? entry_of(c, a.entry) : nullptr;
        if (e) {
            out   = *e;
            found = true;
        }
    });
    return found ? refusal{} : refusal{ UNKNOWN, "unknown entry \"" + a.entry + "\"" };
}

static refusal edit(acting & a) {
    std::string text;
    if (!text_field(a.body, "text", text) || trim(text).empty()) {
        return { MALFORMED, "the body holds no text" };
    }
    conversation_entry e;
    const refusal      r = held(a, e);
    if (r.status) {
        return r;
    }
    if (e.role != ROLE_USER) {
        return { MALFORMED, "\"" + a.entry + "\" is not a message of the user" };
    }
    return e.text == text ? refusal{} : say(a.id, text, e.parent);
}

static refusal retry(acting & a) {
    conversation_entry e;
    const refusal      r = held(a, e);
    if (r.status) {
        return r;
    }
    if (e.role == ROLE_CLI) {
        return { MALFORMED, "\"" + a.entry + "\" is not a message of the user nor an answer" };
    }
    // an answer whose command was dismissed at the root follows no entry
    if (e.role == ROLE_ASSISTANT && !e.parent) {
        return { MALFORMED, "\"" + a.entry + "\" follows no entry" };
    }
    // a summary writes itself again after the entry it follows, whatever
    // that entry is now
    const std::string user = e.role == ROLE_ASSISTANT ? *e.parent : a.entry;
    return answer(a.id, [&](conversation &) { return user; }, e.compaction);
}

// a change of the tree: a turn taken before it refuses it, one taken after
// it starts from it, as a turn is taken before it writes its entry, under
// the lock of the change
static refusal moved(const acting & a, const std::function<void(conversation &)> & move) {
    return changed(a.id, [&](conversation & c) {
        if (agent_busy(a.id)) {
            return refusal{ CONFLICT, AGENT_BUSY };
        }
        move(c);
        return refusal{};
    });
}

static refusal browse(acting & a) {
    conversation_entry e;
    const refusal      r = held(a, e);
    return r.status ? r : moved(a, [&](conversation & c) { c.leaf = conversation_latest(c, a.entry); });
}

// a command leaves its conversation, which leaves with its last entry
static refusal drop(acting & a) {
    conversation_entry e;
    refusal            r = held(a, e);
    if (r.status) {
        return r;
    }
    if (e.role != ROLE_CLI) {
        return { MALFORMED, "\"" + a.entry + "\" is not a command" };
    }
    bool empty = false;
    r          = moved(a, [&](conversation & c) {
        conversation_drop(c, a.entry);
        empty = c.entries.empty();
    });
    if (!r.status && empty) {
        library_remove({ a.id });
    }
    return r;
}

// an action a page sends: its name, what it does, and what its body names, a
// conversation, one of its entries
struct action_t {
    const char * name;
    refusal (*run)(acting &);
    bool id;
    bool entry;
};

static const action_t ACTIONS[] = {
    { "send",     send,     false, false },
    { "command",  command,  false, false },
    { "complete", complete, false, false },
    { "erase",    erase,    false, false },
    { "stop",     stop,     true,  false },
    { "answer",   answered, true,  false },
    { "pin",      pin,      true,  false },
    { "edit",     edit,     true,  true  },
    { "retry",    retry,    true,  true  },
    { "browse",   browse,   true,  true  },
    { "drop",     drop,     true,  true  },
};

std::string sync_actions() {
    std::string out;
    for (const action_t & a : ACTIONS) {
        out += (out.empty() ? "" : "|") + std::string(a.name);
    }
    return out;
}

// what the action does, what keeps it from it as the body, the conversation
// and the turn stand, in that order
static refusal act(const action_t & action, acting & a) {
    a.named = text_field(a.body, "id", a.id);
    if (a.named && !known(a.id)) {
        return { UNKNOWN, "unknown conversation \"" + a.id + "\"" };
    }
    if (action.id && !a.named) {
        return { MALFORMED, "the body names no id" };
    }
    if (action.entry && !text_field(a.body, "entry", a.entry)) {
        return { MALFORMED, "the body names no entry" };
    }
    return action.run(a);
}

void sync_action(const httplib::Request & req, httplib::Response & res, const std::string & name) {
    const action_t * action =
        std::find_if(std::begin(ACTIONS), std::end(ACTIONS), [&](const action_t & x) { return name == x.name; });
    const json_doc doc = json_read(req.body);
    acting         a;
    a.body          = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    const refusal r = yyjson_is_obj(a.body) ? act(*action, a) : refusal{ MALFORMED, "the body is not a JSON object" };
    const std::string named = conversation_shown(a.id);
    if (r.status) {
        log_error("Sync", "%s %s %s: %s", req.remote_addr.c_str(), name.c_str(), named.c_str(), r.why.c_str());
        res.status = r.status;
        res.set_content(r.why + "\n", TEXT_TYPE);
        return;
    }
    if (a.words) {
        const json_mut   made = json_new();
        yyjson_mut_val * out  = yyjson_mut_obj(made.get());
        yyjson_mut_val * list = yyjson_mut_obj_add_arr(made.get(), out, "words");
        yyjson_mut_doc_set_root(made.get(), out);
        for (const std::string & w : *a.words) {
            yyjson_mut_arr_add_strncpy(made.get(), list, w.data(), w.size());
        }
        res.set_content(json_write(made), JSON_TYPE);
        return;
    }
    log_info("Sync", "%s %s %s", req.remote_addr.c_str(), name.c_str(), named.c_str());
    if (!a.created.empty()) {
        res.set_content("{\"id\":" + json_quote(a.created) + "}", JSON_TYPE);
        return;
    }
    res.status = httplib::StatusCode::NoContent_204;
}
