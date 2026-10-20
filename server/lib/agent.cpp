// agent.cpp: the agent of the server: a turn per conversation, each on its own
// thread, writing its answer into the library as it streams

#include "lib/agent.h"

#include "engine/keychain.h"
#include "engine/parse.h"
#include "engine/schema.h"
#include "engine/seal.h"
#include "lib/api.h"
#include "lib/compaction.h"
#include "lib/library.h"
#include "lib/log.h"
#include "lib/mcp.h"
#include "lib/prompts.h"
#include "lib/pulse.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <algorithm>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

// what a call reads when no tool reads it, and why a call does not run
static const char * const NO_ARGS = "{}";
static const char * const USE_ON  = "on";
static const char * const USE_OFF = "off";

// the stop of the turn of each conversation
static std::mutex                                       g_mutex;
static std::map<std::string, std::shared_ptr<api_stop>> g_turns;

// the kinds of question a turn waits on: a call to grant, a form to fill, the
// lines of a call the attached pages run
static const char * const ASK_CALL = "call";
static const char * const ASK_FORM = "form";
static const char * const ASK_RUN  = "run";

// a question a turn waits on, its number, and the answer that settles it: a
// verdict, the response to the form as JSON, or what a page answers as JSON,
// {"output":...,"ok":...}
struct question_t {
    std::mutex              mutex;
    std::condition_variable settled;
    std::string             kind;
    uint64_t                number = 0;
    std::string             verdict;
};

// the number of the last question asked, so every question of this server
// has its own and an answer names the one it settles
static uint64_t g_asked = 0;

// the pages attached to this server, which run the config tool of the pages
static size_t g_pages = 0;

static const char * const ALWAYS = AGENT_VERDICTS[1];
static const char * const REFUSE = AGENT_VERDICTS[2];

// the question each turn waits on, by conversation
static std::map<std::string, std::shared_ptr<question_t>> g_questions;

// the text the user typed in a form of an MCP server, sealed out of every
// outcome as the keychain seals its secrets
static words_t g_typed;

// a question settled, the first answer winning, empty when a stop wakes it
static void answered(const std::shared_ptr<question_t> & q, const std::string & verdict) {
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->verdict.empty()) {
        q->verdict = verdict;
    }
    q->settled.notify_all();
}

// the question a conversation asks settled, if one stands: woken once its
// turn stops
static void settle_question(const std::string & id, const std::string & verdict) {
    std::shared_ptr<question_t> q;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto                  it = g_questions.find(id);
        if (it == g_questions.end()) {
            return;
        }
        q = it->second;
    }
    answered(q, verdict);
}

// why the question of that kind and number does not stand in a conversation,
// empty when it does, q then the question itself
static std::string standing(const std::string &           id,
                            const std::string &           kind,
                            uint64_t                      number,
                            std::shared_ptr<question_t> & q) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto                  it = g_questions.find(id);
    if (it == g_questions.end()) {
        return "nothing is asked in this conversation";
    }
    if (it->second->number != number) {
        return "question " + std::to_string(number) + " is not the one asked";
    }
    const std::string & k = it->second->kind;
    if (k != kind) {
        return k == ASK_CALL ? "the question awaits a verdict" :
               k == ASK_FORM ? "the question awaits a response" :
                               "the question awaits an output";
    }
    q = it->second;
    return "";
}

// the question of the kind and number settled, when it stands: the very
// question found standing, so an answer never settles one asked after it
static std::string settle_standing(const std::string & id,
                                   const std::string & kind,
                                   uint64_t            number,
                                   const std::string & answer) {
    std::shared_ptr<question_t> q;
    const std::string           why = standing(id, kind, number, q);
    if (why.empty()) {
        answered(q, answer);
    }
    return why;
}

// what a page answers a run, as JSON
static std::string ran(const std::string & output, bool ok) {
    const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_val *                      out = yyjson_mut_obj(made.get());
    yyjson_mut_doc_set_root(made.get(), out);
    yyjson_mut_obj_add_strncpy(made.get(), out, "output", output.data(), output.size());
    yyjson_mut_obj_add_bool(made.get(), out, "ok", ok);
    const std::shared_ptr<char> json(yyjson_mut_write(made.get(), 0, nullptr), free);
    return json.get();
}

// what a run answers once no page is attached
static std::string gone() {
    return ran("No page is attached", false);
}

std::string agent_grant(const std::string & id, uint64_t question, const std::string & verdict) {
    return settle_standing(id, ASK_CALL, question, verdict);
}

std::string agent_fill(const std::string & id, uint64_t question, const std::string & response) {
    return settle_standing(id, ASK_FORM, question, response);
}

std::string agent_ran(const std::string & id, uint64_t question, const std::string & output, bool ok) {
    return settle_standing(id, ASK_RUN, question, ran(output, ok));
}

void agent_attach() {
    size_t pages = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        pages = ++g_pages;
    }
    log_info("Agent", "A page attaches, %zu attached", pages);
}

void agent_detach() {
    // the runs asked of the pages once the last leaves, the questions
    // themselves, so none asked after them is settled in their place
    std::vector<std::shared_ptr<question_t>> left;
    size_t                                   pages = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        pages = --g_pages;
        for (const auto & q : g_questions) {
            if (!pages && q.second->kind == ASK_RUN) {
                left.push_back(q.second);
            }
        }
    }
    log_info("Agent", "A page detaches, %zu attached", pages);
    for (const std::shared_ptr<question_t> & q : left) {
        answered(q, gone());
    }
}

// whether a page is attached
static bool attached() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pages > 0;
}

bool agent_busy(const std::string & id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_turns.count(id) > 0;
}

void agent_stop(const std::string & id) {
    std::shared_ptr<api_stop> stop;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto                  it = g_turns.find(id);
        if (it != g_turns.end()) {
            stop = it->second;
        }
    }
    if (stop) {
        stop->stop();
        settle_question(id, "");
    }
}

// what one round reads of the running configuration, as it stands when the
// round starts: the endpoints, the model chosen, the system prompt, the
// parameters of every model, numbers as numbers, the tokens that compact the
// history of a model and the steps its summary keeps, each tool of the server not
// off by its own name, then each tool of every MCP server not off, a name
// served twice staying with the first; a server that fails serves the tools
// it listed last, each call answering its error, and is not asked again in
// the turn, the turn holding it as it was named, so a new url, key or header
// tries it again, and a server no longer named is forgotten; a failure and a
// name served twice log once a turn
struct setup_t {
    std::vector<endpoint>                                                   endpoints;
    std::string                                                             chosen;
    std::string                                                             system;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> parameters;
    std::map<std::string, double>                                           triggers;
    std::map<std::string, int>                                              preserves;
    std::vector<tool>                                                       tools;
    bool                                                                    pages = false;
    std::vector<mcp_tool>                                                   served;
};

// a string as JSON, the way a parameter of a model goes
static std::string json_string(const std::string & s) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return text.get();
}

// the name every tool of this server is served under, as a problem tells it
static const char * const OWN = "KiSS";

// a server as it is named: its name, url, headers and key
static std::string signature(const remote & r) {
    return r.name + "\n" + r.url + "\n" + r.headers + "\n" + r.value;
}

// what a turn met: the servers that failed, as they were named, and the
// names served twice, as logged
struct met_t {
    std::set<std::string> down;
    std::set<std::string> clashes;
};

static setup_t setup(api_stop & stop, met_t & met) {
    setup_t             s;
    std::vector<remote> servers;
    engine_with([&](const engine_reader & config) {
        s.endpoints = api_endpoints(config);
        s.chosen    = config.get("chat model").value_or("");
        s.system    = config.get("chat system-prompt").value_or("");
        for (const std::string & name : config.names("mcp")) {
            servers.push_back(remote_of(config, "mcp", name, BEARER));
        }
        const engine_module * m = find_module("models");
        for (const std::string & item : config.names("models")) {
            for (const engine_key & k : m->keys) {
                const auto v = config.get("models " + k.name, item);
                if (!v) {
                    continue;
                }
                if (k.name == COMPACTION_TRIGGER) {
                    s.triggers[item] = std::stod(*v);
                } else if (k.name == COMPACTION_PRESERVE) {
                    s.preserves[item] = std::stoi(*v);
                } else {
                    s.parameters[item].emplace_back(k.name, k.kind == ENGINE_NUMBER ? *v : json_string(*v));
                }
            }
        }
        for (tool & t : tools_list(config)) {
            if (config.get("tools use", t.suffix).value_or(USE_OFF) != USE_OFF) {
                s.tools.push_back(std::move(t));
            }
        }
        s.pages = attached() && config.get("tools use", AGENT_CLIENT_TOOL).value_or(USE_OFF) != USE_OFF;
    });
    words_t named;
    for (const remote & r : servers) {
        named.push_back(r.name);
    }
    mcp_forget(named);
    std::vector<mcp_tool> listed;
    for (const remote & r : servers) {
        std::vector<mcp_tool> tools;
        if (met.down.count(signature(r))) {
            tools = mcp_kept(r);
        } else {
            try {
                tools = mcp_tools(r, stop);
            } catch (const std::exception & e) {
                if (stop.stopped) {
                    throw;
                }
                met.down.insert(signature(r));
                tools = mcp_kept(r);
                if (tools.empty()) {
                    log_error("Agent", "MCP server %s serves no tool: %s", r.name.c_str(), e.what());
                } else {
                    log_error("Agent", "MCP server %s serves the %zu tools it listed last: %s", r.name.c_str(),
                              tools.size(), e.what());
                }
            }
        }
        listed.insert(listed.end(), tools.begin(), tools.end());
    }
    std::map<std::string, std::string> owner;
    for (const tool & t : s.tools) {
        owner[t.suffix] = OWN;
    }
    if (s.pages) {
        owner[AGENT_CLIENT_TOOL] = "the pages";
    }
    engine_with([&](const engine_reader & config) {
        for (const mcp_tool & t : listed) {
            if (config.get("tools use", t.name).value_or(USE_OFF) == USE_OFF) {
                continue;
            }
            const auto first = owner.find(t.name);
            if (first != owner.end()) {
                const std::string clash =
                    "MCP server " + t.server + ": tool " + t.name + " left out, " + first->second + " serves it";
                if (met.clashes.insert(clash).second) {
                    log_error("Agent", "%s", clash.c_str());
                }
                continue;
            }
            owner[t.name] = "mcp " + t.server;
            s.served.push_back(t);
        }
    });
    return s;
}

// the pulse of a turn as every page following its conversation shows it
static void beat(const std::string & id, const pulse & p) {
    library_turn(id, pulse_json(p));
}

// the reply as the library holds it
static conversation_entry reply_of(const std::string & id, const std::string & entry) {
    conversation_entry out;
    library_each([&](const conversation & c) {
        if (c.id == id) {
            for (const conversation_entry & e : c.entries) {
                if (e.id == entry) {
                    out = e;
                }
            }
        }
    });
    return out;
}

// the reply grows; a reply gone, its conversation erased, stops the turn
static void grow(const std::string &                               id,
                 const std::string &                               entry,
                 api_stop &                                        stop,
                 const std::function<void(conversation_entry &)> & change) {
    if (!library_grow(id, entry, change)) {
        stop.stop();
        throw api_error("Stopped");
    }
}

// a write of a conversation that failed, told in the log, the turn going on
// with the conversation as the library holds it
static void logged(const std::string & id, const std::string & why) {
    if (!why.empty()) {
        log_error("Agent", "%.8s is not written: %s", id.c_str(), why.c_str());
    }
}

// what the user answers a question of a turn, the question made its number
// and shown to every page following the conversation while it stands, the
// time it stands taken out of the pulse
static std::string ask(const std::string & id,
                       yyjson_mut_doc *    made,
                       const std::string & kind,
                       api_stop &          stop,
                       pulse &             p) {
    const double asked = pulse_now();
    const auto   q     = std::make_shared<question_t>();
    q->kind            = kind;
    {
        // a run asked once no page is attached answers at once
        std::lock_guard<std::mutex> lock(g_mutex);
        q->number       = ++g_asked;
        g_questions[id] = q;
        if (kind == ASK_RUN && !g_pages) {
            q->verdict = gone();
        }
    }
    yyjson_mut_obj_add_uint(made, yyjson_mut_doc_get_root(made), "question", q->number);
    const std::shared_ptr<char> question(yyjson_mut_write(made, 0, nullptr), free);
    library_asking(id, question.get());
    std::string verdict;
    {
        std::unique_lock<std::mutex> lock(q->mutex);
        q->settled.wait(lock, [&] { return !q->verdict.empty() || stop.stopped; });
        verdict = q->verdict;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_questions.erase(id);
    }
    library_asking(id, "");
    pulse_held(p, pulse_now() - asked);
    beat(id, p);
    if (stop.stopped) {
        throw api_error("Stopped");
    }
    return verdict;
}

// the response of the user to a form a tool asks them to fill, an MCP input
// request, shown under the name of the server that asks
static std::string fill(const std::string & id,
                        const std::string & server,
                        const std::string & input,
                        api_stop &          stop,
                        pulse &             p) {
    const std::shared_ptr<yyjson_doc>     in(yyjson_read(input.data(), input.size(), 0), yyjson_doc_free);
    const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_val *                      q = yyjson_mut_obj(made.get());
    yyjson_mut_doc_set_root(made.get(), q);
    yyjson_mut_obj_add_str(made.get(), q, "kind", "form");
    yyjson_mut_obj_add_strncpy(made.get(), q, "server", server.data(), server.size());
    yyjson_mut_obj_add_val(made.get(), q, "input", yyjson_val_mut_copy(made.get(), yyjson_doc_get_root(in.get())));
    log_info("Agent", "%.8s asks the user to fill a form of %s", id.c_str(), server.c_str());
    return ask(id, made.get(), ASK_FORM, stop, p);
}

// what a tool of this server answers, run again with what the user answers
// each time it asks them to fill a form, until it asks nothing more; one they
// cancel answers nothing; throws once stopped
static tool_outcome run_own(const std::string & id, const tool & t, yyjson_val * args, api_stop & stop, pulse & p) {
    const tool_tick tick = [&stop] {
        return !stop.stopped;
    };
    engine_typed typed;
    tool_outcome out = t.run(args, typed, tick);
    while (!out.asks.empty() && !stop.stopped) {
        const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
        yyjson_mut_val *                      input = yyjson_mut_obj(made.get());
        yyjson_mut_doc_set_root(made.get(), input);
        tools_form(made.get(), input, out);
        const std::shared_ptr<char> form(yyjson_mut_write(made.get(), 0, nullptr), free);
        const std::string response = fill(id, engine_get("server hostname").value_or(""), form.get(), stop, p);
        const std::shared_ptr<yyjson_doc> answered(yyjson_read(response.data(), response.size(), 0), yyjson_doc_free);
        if (!tools_typed(yyjson_doc_get_root(answered.get()), typed)) {
            return { false, "The user answered nothing" };
        }
        out = t.run(args, typed, tick);
    }
    if (stop.stopped) {
        throw api_error("Stopped");
    }
    return out;
}

// the config tool of the attached pages, as the agent of this server sees it
static tool client_tool() {
    tool t;
    t.suffix      = AGENT_CLIENT_TOOL;
    t.description = prompts_page_description({
        { "page", "every KiSS page attached to this server" }
    });
    t.input       = prompts_page_input();
    return t;
}

// what the attached pages answer the lines of a call each runs on its own
// configuration, the call shown to every page following the conversation,
// the first answer winning; once no page is attached it answers so; throws
// once stopped
static tool_outcome run_client(const std::string & id, yyjson_val * args, api_stop & stop, pulse & p) {
    yyjson_val *                          lines = yyjson_obj_get(args, "lines");
    const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_val *                      q = yyjson_mut_obj(made.get());
    yyjson_mut_doc_set_root(made.get(), q);
    yyjson_mut_obj_add_str(made.get(), q, "kind", ASK_RUN);
    yyjson_mut_obj_add_strncpy(made.get(), q, "lines", yyjson_get_str(lines), yyjson_get_len(lines));
    log_info("Agent", "%.8s asks the pages to run a call", id.c_str());
    const std::string                 answer = ask(id, made.get(), ASK_RUN, stop, p);
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(answer.data(), answer.size(), 0), yyjson_doc_free);
    yyjson_val *                      root   = yyjson_doc_get_root(doc.get());
    yyjson_val *                      output = yyjson_obj_get(root, "output");
    return { yyjson_get_bool(yyjson_obj_get(root, "ok")), std::string(yyjson_get_str(output), yyjson_get_len(output)) };
}

// a call runs and its outcome enters it: a call reaches a tool not off as the
// configuration stands when it goes, with arguments it reads as an object; a
// tool in consent asks the user, the turn waiting for a page to answer: once,
// always, which turns the tool on, or refuse; a tool that asks the user to
// fill a form, a secret to type or a url to let, gets their response the
// same way
static void call(const std::string &           id,
                 const std::string &           entry,
                 size_t                        r,
                 size_t                        c,
                 api_stop &                    stop,
                 pulse &                       p,
                 const std::vector<mcp_tool> & served) {
    const conversation_call called = reply_of(id, entry).rounds[r].calls[c];
    std::optional<tool>     found;
    bool                    paged = false;
    std::optional<remote>   server;
    std::string             use;
    const bool              pages = attached();
    engine_with([&](const engine_reader & config) {
        for (tool & t : tools_list(config)) {
            const std::string u = config.get("tools use", t.suffix).value_or(USE_OFF);
            if (t.suffix == called.name && u != USE_OFF) {
                found = std::move(t);
                use   = u;
            }
        }
        const std::string paging = config.get("tools use", AGENT_CLIENT_TOOL).value_or(USE_OFF);
        if (!found && pages && called.name == AGENT_CLIENT_TOOL && paging != USE_OFF) {
            paged = true;
            use   = paging;
        }
        for (const mcp_tool & t : served) {
            const std::string u = config.get("tools use", t.name).value_or(USE_OFF);
            if (!found && !paged && !server && t.name == called.name && u != USE_OFF &&
                config.get("mcp url", t.server)) {
                server = remote_of(config, "mcp", t.server, BEARER);
                use    = u;
            }
        }
    });
    const auto settle = [&](const std::string & args, const std::string & result, bool ok,
                            const std::vector<conversation_image> & images) {
        grow(id, entry, stop, [&](conversation_entry & e) {
            conversation_call & x = e.rounds[r].calls[c];
            x.args                = args;
            x.result              = result;
            x.ok                  = ok;
            x.images              = images;
        });
    };
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(called.args.data(), called.args.size(), 0), yyjson_doc_free);
    yyjson_val * args  = called.args.empty() ? nullptr : doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    const bool   known = found || paged || server;
    if (!known || (!called.args.empty() && !yyjson_is_obj(args))) {
        settle(NO_ARGS, known ? "The arguments are not a JSON object" : "Unknown tool \"" + called.name + "\"", false,
               {});
        return;
    }
    if (use != USE_ON) {
        const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
        yyjson_mut_val *                      q = yyjson_mut_obj(made.get());
        yyjson_mut_doc_set_root(made.get(), q);
        yyjson_mut_obj_add_str(made.get(), q, "kind", "call");
        yyjson_mut_obj_add_strncpy(made.get(), q, "tool", called.name.data(), called.name.size());
        yyjson_mut_obj_add_strncpy(made.get(), q, "args", called.args.data(), called.args.size());
        log_info("Agent", "%.8s asks the user for %s", id.c_str(), called.name.c_str());
        const std::string verdict = ask(id, made.get(), ASK_CALL, stop, p);
        log_info("Agent", "%.8s %s: %s", id.c_str(), called.name.c_str(), verdict.c_str());
        if (verdict == REFUSE) {
            settle(called.args, "The user refused the call", false, {});
            return;
        }
        if (verdict == ALWAYS) {
            engine_run("set tools use " + quote(called.name) + " " + USE_ON, ENGINE_ADMIN);
        }
    }
    const std::string refused = found ? tools_check(*found, args) : paged ? tools_check(client_tool(), args) : "";
    if (!refused.empty()) {
        settle(called.args, refused, false, {});
        return;
    }
    grow(id, entry, stop, [&](conversation_entry & e) { e.rounds[r].calls[c].sent = true; });
    // the call written as sent before it runs, or it does not run
    const std::string unkept = library_keep(id, keychain_known());
    if (!unkept.empty()) {
        logged(id, unkept);
        settle(called.args, "The conversation is not written: " + unkept, false, {});
        return;
    }
    log_info("Agent", "%.8s calls %s", id.c_str(), called.name.c_str());
    if (server) {
        // every text the user types in a form of the server is sealed from then on
        const auto answer = [&](const std::string & input) {
            const std::string                 response = fill(id, server->name, input, stop, p);
            const std::shared_ptr<yyjson_doc> answered(yyjson_read(response.data(), response.size(), 0),
                                                       yyjson_doc_free);
            size_t                            i, n;
            yyjson_val *                      key;
            yyjson_val *                      value;
            std::lock_guard<std::mutex>       lock(g_mutex);
            yyjson_obj_foreach(yyjson_obj_get(yyjson_doc_get_root(answered.get()), "content"), i, n, key, value) {
                if (yyjson_is_str(value) && yyjson_get_len(value)) {
                    g_typed.push_back(yyjson_get_str(value));
                }
            }
            return response;
        };
        mcp_outcome out;
        try {
            out = mcp_call(*server, called.name, called.args.empty() ? NO_ARGS : called.args, stop, answer);
        } catch (const std::exception & e) {
            if (stop.stopped) {
                throw;
            }
            out.text = e.what();
        }
        words_t typed;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            typed = g_typed;
        }
        settle(called.args, seal_text(out.text, typed), out.ok, out.images);
        return;
    }
    if (paged) {
        const tool_outcome out = run_client(id, args, stop, p);
        settle(called.args, out.text, out.ok, {});
        return;
    }
    const tool_outcome              out = run_own(id, *found, args, stop, p);
    std::vector<conversation_image> images;
    if (!out.image.empty()) {
        images.push_back({ out.mime, out.image });
    }
    settle(called.args, out.text, out.ok, images);
}

// what went wrong, with the command that fixes it when the endpoint is the
// cause, its first letter capital unless it opens on a URL
static std::string explain(const std::exception & e, const api_stop & stop) {
    if (stop.stopped) {
        return "Stopped";
    }
    const api_error * a = dynamic_cast<const api_error *>(&e);
    std::string       s = std::string(e.what()) + (a && !a->fix.empty() ? " -> " + a->fix : "");
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z' && !starts_with(s, "http")) {
        s[0] = (char) (s[0] - 'a' + 'A');
    }
    return s;
}

// the endpoint asked again, the clock running since it first failed
static void retried(const std::string & id, const endpoint & e, pulse & p, int status, int attempt, int attempts) {
    log_info("Agent", "%.8s %s answers %d, attempt %d/%d", id.c_str(), e.server.name.c_str(), status, attempt,
             attempts);
    pulse_retry(p, e.server.name, status, attempt, attempts, pulse_now());
    beat(id, p);
}

// the summary the steps a request folds go into, streamed into a round of the
// reply that keeps the steps it does not fold; it runs no call, and one that
// writes no summary fails the turn
static void summarize(const std::string & id,
                      const std::string & entry,
                      const endpoint &    e,
                      const api_request & req,
                      int                 kept,
                      api_stop &          stop,
                      pulse &             p) {
    log_info("Agent", "%.8s compacting, %d steps kept", id.c_str(), kept);
    pulse_mark(p, PHASE_COMPACTING, e.server.name, pulse_now());
    beat(id, p);
    size_t at = 0;
    grow(id, entry, stop, [&](conversation_entry & x) {
        conversation_round r;
        r.kept = kept;
        x.rounds.push_back(r);
        at = x.rounds.size() - 1;
    });
    std::optional<double> usage;
    api_chat(
        e, req, stop,
        [&](const api_delta & d) {
            const double now = pulse_now();
            if (d.usage) {
                usage = d.usage;
            }
            pulse_chunk(p, !d.content.empty() || !d.reasoning.empty(), now);
            if (d.content.empty() && d.reasoning.empty()) {
                return;
            }
            pulse_mark(p, PHASE_COMPACTING, e.server.name, now);
            grow(id, entry, stop, [&](conversation_entry & x) {
                x.rounds[at].reasoning += d.reasoning;
                x.rounds[at].text += d.content;
            });
            beat(id, p);
        },
        [&](int status, int attempt, int attempts) { retried(id, e, p, status, attempt, attempts); });
    pulse_close(p, usage);
    if (reply_of(id, entry).rounds[at].text.empty()) {
        throw api_error("Endpoint " + e.server.name + " wrote no summary");
    }
}

// one round streamed into the reply, the history first compacted when the
// last request held as many tokens as the trigger of the model; whether it
// called anything; context, the tokens the last request held once answered,
// none once unknown
static bool round(const std::string &                     id,
                  const std::string &                     entry,
                  const std::vector<conversation_entry> & before,
                  size_t                                  r,
                  std::optional<double> &                 context,
                  api_stop &                              stop,
                  pulse &                                 p,
                  met_t &                                 met) {
    p.round = (int) r + 1;
    pulse_mark(p, PHASE_PREPARING, "", pulse_now());
    beat(id, p);
    const setup_t     s       = setup(stop, met);
    const auto        pick    = api_pick(s.endpoints, s.chosen, stop);
    const endpoint &  e       = pick.first;
    const std::string model   = pick.second;
    const std::string item    = e.server.name + "/" + model;
    // the history as it stands, the reply as the library holds it last
    const auto        history = [&] {
        std::vector<conversation_entry> h = before;
        h.push_back(reply_of(id, entry));
        return h;
    };
    api_request req;
    req.model         = model;
    req.system        = s.system;
    const auto params = s.parameters.find(item);
    if (params != s.parameters.end()) {
        req.parameters = params->second;
    }
    for (const tool & t : s.tools) {
        req.tools.push_back({ t.suffix, t.description, t.input });
    }
    if (s.pages) {
        const tool t = client_tool();
        req.tools.push_back({ t.suffix, t.description, t.input });
    }
    for (const mcp_tool & t : s.served) {
        req.tools.push_back({ t.name, t.description, t.input });
    }
    if (e.speaks->described) {
        req.info = api_describe(e, model, stop);
    }
    const auto trigger = s.triggers.find(item);
    if (trigger != s.triggers.end() && context && *context >= trigger->second) {
        const auto preserve = s.preserves.find(item);
        const auto fold     = compaction_folding(history(), preserve == s.preserves.end() ? 0 : preserve->second);
        if (fold) {
            api_request ask = req;
            ask.messages    = fold->messages;
            summarize(id, entry, e, ask, fold->kept, stop, p);
        }
    }
    context.reset();
    req.messages = compaction_compacted(history());
    log_info("Agent", "%.8s round %zu: %s/%s", id.c_str(), r + 1, e.server.name.c_str(), model.c_str());
    pulse_mark(p, PHASE_WAITING, e.server.name, pulse_now());
    beat(id, p);
    // the round as the reply holds it
    size_t at = 0;
    grow(id, entry, stop, [&](conversation_entry & x) {
        x.rounds.emplace_back();
        at = x.rounds.size() - 1;
    });
    // the calls of the round by the index the stream gives them
    std::map<int, size_t> calls;
    std::optional<double> usage;
    // the call the stream writes now
    std::string           calling;
    api_chat(
        e, req, stop,
        [&](const api_delta & d) {
            const double now = pulse_now();
            if (d.usage) {
                usage = d.usage;
            }
            if (d.context) {
                context = d.context;
            }
            pulse_chunk(p, !d.content.empty() || !d.reasoning.empty() || !d.calls.empty(), now);
            if (d.content.empty() && d.reasoning.empty() && d.calls.empty() && d.opaque.empty()) {
                return;
            }
            if (!d.reasoning.empty()) {
                pulse_mark(p, PHASE_THINKING, "", now);
            }
            if (!d.content.empty()) {
                pulse_mark(p, PHASE_WRITING, "", now);
            }
            grow(id, entry, stop, [&](conversation_entry & x) {
                conversation_round & round = x.rounds[at];
                round.reasoning += d.reasoning;
                round.text += d.content;
                if (!d.opaque.empty()) {
                    // an item the protocol needs back, after those of the round
                    if (!round.opaque) {
                        round.opaque = conversation_opaque{ e.speaks->name, "[]" };
                    }
                    std::string & items = round.opaque->items;
                    items.insert(items.size() - 1, (items == "[]" ? "" : ",") + d.opaque);
                }
                for (const api_call_piece & piece : d.calls) {
                    if (!calls.count(piece.index)) {
                        conversation_call c;
                        c.id = "call-" + std::to_string(r) + "-" + std::to_string(piece.index);
                        round.calls.push_back(c);
                        calls[piece.index] = round.calls.size() - 1;
                    }
                    conversation_call & c = round.calls[calls[piece.index]];
                    if (!piece.id.empty()) {
                        c.id = piece.id;
                    }
                    c.name += piece.name;
                    c.args += piece.args;
                    calling = c.name;
                }
            });
            if (!d.calls.empty()) {
                pulse_mark(p, PHASE_CALLING, calling, now);
            }
            beat(id, p);
        },
        [&](int status, int attempt, int attempts) { retried(id, e, p, status, attempt, attempts); });
    pulse_close(p, usage);
    const size_t n = reply_of(id, entry).rounds[at].calls.size();
    for (size_t c = 0; c < n; c++) {
        if (stop.stopped) {
            throw api_error("Stopped");
        }
        pulse_mark(p, PHASE_RUNNING, reply_of(id, entry).rounds[at].calls[c].name, pulse_now());
        beat(id, p);
        call(id, entry, at, c, stop, p, s.served);
    }
    return n > 0;
}

// one turn, rounds streamed until a round calls nothing; after a stop no call
// runs, and the reply keeps what settled before it, with what it spent once
// it streamed anything and the tokens its last request held; the pulse
// follows the turn chunk by chunk
static void turn(std::string                     id,
                 std::string                     entry,
                 std::vector<conversation_entry> before,
                 std::shared_ptr<api_stop>       stop,
                 pulse                           p) {
    std::optional<std::string> error;
    size_t                     r = 0;
    met_t                      met;
    // the tokens the last request held once answered, as the last turn kept
    // them, none once unknown
    std::optional<double>      context;
    for (const conversation_entry & e : before) {
        if (e.role == ROLE_ASSISTANT) {
            context = e.stats ? e.stats->context : std::nullopt;
        }
    }
    try {
        int rounds = 0;
        engine_with(
            [&](const engine_reader & config) { rounds = std::stoi(config.get("tools max-rounds").value_or("1")); });
        bool more = true;
        for (; more && r < (size_t) rounds; r++) {
            more = round(id, entry, before, r, context, *stop, p, met);
        }
        if (more) {
            error = "Stopped after " + std::to_string(r) + " tool rounds";
        }
    } catch (const std::exception & e) {
        error = explain(e, *stop);
    }
    std::optional<conversation_stats> spent = pulse_stats(p, pulse_now());
    if (spent) {
        spent->context = context;
    }
    logged(id, library_change(
                   id,
                   [&](conversation & c) {
                       for (conversation_entry & e : c.entries) {
                           if (e.id == entry) {
                               e.rounds = conversation_settled(e.rounds);
                               e.error  = error;
                               if (spent) {
                                   e.stats = *spent;
                               }
                           }
                       }
                       return std::string();
                   },
                   keychain_known()));
    log_info("Agent", "%.8s %s in %.0f ms", id.c_str(), error ? error->c_str() : "answered", pulse_now() - p.start);
    library_turn(id, "");
    std::lock_guard<std::mutex> lock(g_mutex);
    g_turns.erase(id);
}

// a line the user typed runs as the config tool of this server runs it, the
// turn of the conversation while it asks; its output enters after the thread
static void command(std::string id, std::string input, std::shared_ptr<api_stop> stop, pulse p) {
    std::optional<tool> config;
    engine_with([&](const engine_reader & reader) {
        for (tool & t : tools_list(reader)) {
            if (t.suffix == "config") {
                config = std::move(t);
            }
        }
    });
    const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_val *                      args = yyjson_mut_obj(made.get());
    yyjson_mut_doc_set_root(made.get(), args);
    yyjson_mut_obj_add_strncpy(made.get(), args, "lines", input.data(), input.size());
    const std::shared_ptr<yyjson_doc> doc(yyjson_mut_doc_imut_copy(made.get(), nullptr), yyjson_doc_free);
    conversation_entry                e;
    e.role  = ROLE_CLI;
    e.input = input;
    try {
        const tool_outcome out = run_own(id, *config, yyjson_doc_get_root(doc.get()), *stop, p);
        e.output               = out.text;
        e.ok                   = out.ok;
    } catch (const std::exception & err) {
        e.output = explain(err, *stop);
    }
    logged(id, library_change(
                   id,
                   [&](conversation & c) {
                       conversation_append(c, c.leaf, e);
                       return std::string();
                   },
                   keychain_known()));
    log_info("Agent", "%.8s ran %s", id.c_str(), e.ok ? "a line" : "a line that failed");
    library_turn(id, "");
    std::lock_guard<std::mutex> lock(g_mutex);
    g_turns.erase(id);
}

std::string agent_command(const std::string & id, const std::string & input) {
    const auto stop = std::make_shared<api_stop>();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_turns.count(id)) {
            return AGENT_BUSY;
        }
        g_turns[id] = stop;
    }
    pulse p = pulse_start(pulse_now());
    pulse_mark(p, PHASE_RUNNING, "config", pulse_now());
    beat(id, p);
    std::thread(command, id, input, stop, p).detach();
    return "";
}

std::string agent_answer(const std::string & id, const std::function<std::string(conversation &)> & user) {
    const auto stop = std::make_shared<api_stop>();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_turns.count(id)) {
            return AGENT_BUSY;
        }
        g_turns[id] = stop;
    }
    std::string                     entry;
    std::vector<conversation_entry> before;
    const std::string               why = library_change(
        id,
        [&](conversation & c) {
            const std::string asked   = user(c);
            bool              is_user = false;
            for (const conversation_entry & e : c.entries) {
                is_user = is_user || (e.id == asked && e.role == ROLE_USER);
            }
            if (!is_user) {
                return "\"" + asked + "\" is not a message of the user";
            }
            c.leaf = asked;
            before = conversation_path(c);
            conversation_entry reply;
            reply.role         = ROLE_ASSISTANT;
            const size_t index = conversation_append(c, asked, reply);
            entry              = c.entries[index].id;
            c.updated          = c.entries[index].time;
            return std::string();
        },
        keychain_known());
    if (!why.empty()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_turns.erase(id);
        return why;
    }
    const pulse p = pulse_start(pulse_now());
    beat(id, p);
    std::thread(turn, id, entry, before, stop, p).detach();
    return "";
}
