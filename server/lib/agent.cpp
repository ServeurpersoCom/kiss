// agent.cpp: the agent of the server: a turn per conversation, each on its own
// thread, writing its answer into the library as it streams

#include "lib/agent.h"

#include "engine/keychain.h"
#include "engine/schema.h"
#include "lib/api.h"
#include "lib/library.h"
#include "lib/log.h"
#include "lib/mcp.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <chrono>
#include <cmath>
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
    }
}

// what one round reads of the running configuration, as it stands when the
// round starts: the endpoints, the model chosen, the system prompt, the
// parameters of every model, numbers as numbers, each tool of the server not
// off by its own name, then each tool of every MCP server not off, a name
// served twice staying with the first; a server that does not answer sits
// out the round
struct setup_t {
    std::vector<endpoint>                                                   endpoints;
    std::string                                                             chosen;
    std::string                                                             system;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> parameters;
    std::vector<tool>                                                       tools;
    std::vector<mcp_tool>                                                   served;
};

// a string as JSON, the way a parameter of a model goes
static std::string json_string(const std::string & s) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return text.get();
}

static setup_t setup(api_stop & stop) {
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
                if (v) {
                    s.parameters[item].emplace_back(k.name, k.kind == ENGINE_NUMBER ? *v : json_string(*v));
                }
            }
        }
        for (tool & t : tools_list(config)) {
            if (config.get("tools use", t.suffix).value_or(USE_OFF) != USE_OFF) {
                s.tools.push_back(std::move(t));
            }
        }
    });
    std::vector<mcp_tool> listed;
    for (const remote & r : servers) {
        try {
            const std::vector<mcp_tool> tools = mcp_tools(r, stop);
            listed.insert(listed.end(), tools.begin(), tools.end());
        } catch (const std::exception & e) {
            if (stop.stopped) {
                throw;
            }
            log_error("Agent", "MCP server %s sits out: %s", r.name.c_str(), e.what());
        }
    }
    std::set<std::string> taken;
    for (const tool & t : s.tools) {
        taken.insert(t.suffix);
    }
    engine_with([&](const engine_reader & config) {
        for (const mcp_tool & t : listed) {
            if (config.get("tools use", t.name).value_or(USE_OFF) != USE_OFF && taken.insert(t.name).second) {
                s.served.push_back(t);
            }
        }
    });
    return s;
}

// what a turn spent: its tokens, and in milliseconds, generating them, the
// first chunk of the round to its last, and in all
struct spent_t {
    double tokens     = 0;
    double generation = 0;
    double round      = 0;
    double first      = -1;
    double last       = -1;

    void chunk(bool token, double now) {
        if (first < 0) {
            first = now;
        }
        last = now;
        round += token ? 1 : 0;
    }

    void close(std::optional<double> usage) {
        if (first < 0) {
            return;
        }
        tokens += usage ? *usage : round;
        generation += last - first;
        first = last = -1;
        round        = 0;
    }
};

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
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

// the conversation written as it settled, its reply holding its sent calls
static void keep(const std::string & id) {
    library_change(id, [](conversation &) { return std::string(); }, keychain_running());
}

// a call runs and its outcome enters it: a call reaches a tool not off as the
// configuration stands when it goes, with arguments it reads as an object; a
// tool in consent asks the user, and nobody is here to answer yet
static void call(const std::string &           id,
                 const std::string &           entry,
                 size_t                        r,
                 size_t                        c,
                 api_stop &                    stop,
                 const std::vector<mcp_tool> & served) {
    const conversation_call called = reply_of(id, entry).rounds[r].calls[c];
    std::optional<tool>     found;
    std::optional<remote>   server;
    std::string             use;
    engine_with([&](const engine_reader & config) {
        for (tool & t : tools_list(config)) {
            const std::string u = config.get("tools use", t.suffix).value_or(USE_OFF);
            if (t.suffix == called.name && u != USE_OFF) {
                found = std::move(t);
                use   = u;
            }
        }
        for (const mcp_tool & t : served) {
            const std::string u = config.get("tools use", t.name).value_or(USE_OFF);
            if (!found && !server && t.name == called.name && u != USE_OFF && config.get("mcp url", t.server)) {
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
    yyjson_val * args = called.args.empty() ? nullptr : doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    if ((!found && !server) || (!called.args.empty() && !yyjson_is_obj(args))) {
        settle(NO_ARGS,
               found || server ? "The arguments are not a JSON object" : "Unknown tool \"" + called.name + "\"", false,
               {});
        return;
    }
    if (use != USE_ON) {
        settle(called.args, "Nobody is here to allow the call", false, {});
        return;
    }
    const std::string refused = found ? tools_check(*found, args) : "";
    if (!refused.empty()) {
        settle(called.args, refused, false, {});
        return;
    }
    grow(id, entry, stop, [&](conversation_entry & e) { e.rounds[r].calls[c].sent = true; });
    keep(id);
    log_info("Agent", "%.8s calls %s", id.c_str(), called.name.c_str());
    if (server) {
        mcp_outcome out;
        try {
            out = mcp_call(*server, called.name, called.args.empty() ? NO_ARGS : called.args, stop);
        } catch (const std::exception & e) {
            if (stop.stopped) {
                throw;
            }
            out.text = e.what();
        }
        settle(called.args, out.text, out.ok, out.images);
        return;
    }
    const tool_outcome out = found->run(args, {}, [&stop] { return !stop.stopped; });
    if (stop.stopped) {
        throw api_error("Stopped");
    }
    if (!out.asks.empty()) {
        settle(called.args, "Nobody is here to answer: " + out.text, false, {});
        return;
    }
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

// one round streamed into the reply; whether it called anything
static bool round(const std::string &                     id,
                  const std::string &                     entry,
                  const std::vector<conversation_entry> & before,
                  size_t                                  r,
                  api_stop &                              stop,
                  spent_t &                               spent) {
    const setup_t     s     = setup(stop);
    const auto        pick  = api_pick(s.endpoints, s.chosen, stop);
    const endpoint &  e     = pick.first;
    const std::string model = pick.second;
    api_request       req;
    req.model    = model;
    req.system   = s.system;
    req.messages = before;
    req.messages.push_back(reply_of(id, entry));
    const auto params = s.parameters.find(e.server.name + "/" + model);
    if (params != s.parameters.end()) {
        req.parameters = params->second;
    }
    for (const tool & t : s.tools) {
        req.tools.push_back({ t.suffix, t.description, t.input });
    }
    for (const mcp_tool & t : s.served) {
        req.tools.push_back({ t.name, t.description, t.input });
    }
    if (e.speaks->described) {
        req.info = api_describe(e, model, stop);
    }
    log_info("Agent", "%.8s round %zu: %s/%s", id.c_str(), r + 1, e.server.name.c_str(), model.c_str());
    grow(id, entry, stop, [](conversation_entry & x) { x.rounds.emplace_back(); });
    // the calls of the round by the index the stream gives them
    std::map<int, size_t> calls;
    std::optional<double> usage;
    api_chat(
        e, req, stop,
        [&](const api_delta & d) {
            const double now = now_ms();
            if (d.usage) {
                usage = d.usage;
            }
            spent.chunk(!d.content.empty() || !d.reasoning.empty() || !d.calls.empty(), now);
            if (d.content.empty() && d.reasoning.empty() && d.calls.empty()) {
                return;
            }
            grow(id, entry, stop, [&](conversation_entry & x) {
                conversation_round & round = x.rounds[r];
                round.reasoning += d.reasoning;
                round.text += d.content;
                for (const api_call_piece & p : d.calls) {
                    if (!calls.count(p.index)) {
                        conversation_call c;
                        c.id = "call-" + std::to_string(r) + "-" + std::to_string(p.index);
                        round.calls.push_back(c);
                        calls[p.index] = round.calls.size() - 1;
                    }
                    conversation_call & c = round.calls[calls[p.index]];
                    if (!p.id.empty()) {
                        c.id = p.id;
                    }
                    c.name += p.name;
                    c.args += p.args;
                }
            });
        },
        [&](int status, int attempt, int attempts) {
            log_info("Agent", "%.8s %s answers %d, attempt %d/%d", id.c_str(), e.server.name.c_str(), status, attempt,
                     attempts);
        });
    spent.close(usage);
    const size_t n = reply_of(id, entry).rounds[r].calls.size();
    for (size_t c = 0; c < n; c++) {
        if (stop.stopped) {
            throw api_error("Stopped");
        }
        call(id, entry, r, c, stop, s.served);
    }
    return n > 0;
}

// one turn, rounds streamed until a round calls nothing; after a stop no call
// runs, and the reply keeps what settled before it, with what it spent
static void turn(std::string                     id,
                 std::string                     entry,
                 std::vector<conversation_entry> before,
                 std::shared_ptr<api_stop>       stop) {
    const double               start = now_ms();
    spent_t                    spent;
    std::optional<std::string> error;
    size_t                     r = 0;
    try {
        int rounds = 0;
        engine_with(
            [&](const engine_reader & config) { rounds = std::stoi(config.get("tools max-rounds").value_or("1")); });
        bool more = true;
        for (; more && r < (size_t) rounds; r++) {
            more = round(id, entry, before, r, *stop, spent);
        }
        if (more) {
            error = "Stopped after " + std::to_string(r) + " tool rounds";
        }
    } catch (const std::exception & e) {
        error = explain(e, *stop);
    }
    spent.close(std::nullopt);
    const double system = now_ms() - start;
    library_change(
        id,
        [&](conversation & c) {
            for (conversation_entry & e : c.entries) {
                if (e.id == entry) {
                    e.rounds = conversation_settled(e.rounds);
                    e.error  = error;
                    if (spent.tokens > 0) {
                        e.stats = conversation_stats{ spent.tokens, std::round(spent.generation), std::round(system) };
                    }
                }
            }
            return std::string();
        },
        keychain_running());
    log_info("Agent", "%.8s %s in %.0f ms", id.c_str(), error ? error->c_str() : "answered", system);
    library_answering(id, false);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_turns.erase(id);
}

std::string agent_answer(const std::string & id, const std::string & user) {
    const auto stop = std::make_shared<api_stop>();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_turns.count(id)) {
            return "a turn runs in this conversation";
        }
        g_turns[id] = stop;
    }
    std::string                     entry;
    std::vector<conversation_entry> before;
    const std::string               why = library_change(
        id,
        [&](conversation & c) {
            bool is_user = false;
            for (const conversation_entry & e : c.entries) {
                is_user = is_user || (e.id == user && e.role == ROLE_USER);
            }
            if (!is_user) {
                return "\"" + user + "\" is not a message of the user";
            }
            c.leaf = user;
            before = conversation_path(c);
            conversation_entry reply;
            reply.role         = ROLE_ASSISTANT;
            const size_t index = conversation_append(c, user, reply);
            entry              = c.entries[index].id;
            c.updated          = c.entries[index].time;
            return std::string();
        },
        keychain_running());
    if (!why.empty()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_turns.erase(id);
        return why;
    }
    library_answering(id, true);
    std::thread(turn, id, entry, before, stop).detach();
    return "";
}
