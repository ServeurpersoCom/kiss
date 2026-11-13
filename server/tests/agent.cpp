// agent.cpp: the laws of the agent of the server, run as the build ends, a
// turn against an endpoint and MCP servers of the test: every round sends the
// system prompt alone; a server that fails is asked once in the turn; a tool
// whose name one before holds, the server itself, is served under its server
// and its name, called by its own;
// a history compacts as the page compacts it, the very bodies sent;
// a line the user types runs as the config tool runs it, its output after
// the thread, a url it asks to let shown as a form, the line running again
// once the user lets it; the config tool of the pages is the model's while a
// page is attached, one for all of them, its call granted then run by the
// pages, the first answer winning, the last page that leaves answering that
// none is attached; a conversation erased once its round streamed, before its
// calls run, stops its turn; show and completion list the models and the tools
// their sources serve, completion never waiting on a slow one; show
// conversations marks the one a batch runs in, and an erase asks for a
// conversation as the page asks

#include "lib/agent.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "factory.h"
#include "httplib.h"
#include "lib/api.h"
#include "lib/conversation.h"
#include "lib/library.h"
#include "lib/quit.h"
#include "lib/sync.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] agent_test broken: %s\n", what.c_str());
    }
}

// what the endpoint of the test saw: the system prompt and the tools of each
// request, and how often the server that fails was asked to open a session
static std::mutex               g_mutex;
static std::vector<std::string> g_systems;
static std::vector<std::string> g_tools;
static std::atomic<int>         g_down{ 0 };
// the messages of each request of the conversation that compacts
static std::vector<std::string> g_compacted;
// whether the endpoint that hangs is asked, whether it took a message
// silent, and whether the laws let them answer
static std::atomic<bool>        g_hanging{ false };
static std::atomic<bool>        g_silent{ false };
static std::atomic<bool>        g_released{ false };

static std::string json(const std::string & s) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return text.get();
}

int main() {
    httplib::Server server;
    // the endpoint: a message go calls bash, a message page calls the config
    // tool of the pages, a tool result answers text
    server.Get("/v1/models", [](const httplib::Request &, httplib::Response & res) {
        res.set_content("{\"data\":[{\"id\":\"m\"}]}", "application/json");
    });
    server.Post("/v1/chat/completions", [](const httplib::Request & req, httplib::Response & res) {
        const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
        yyjson_val *                      root = yyjson_doc_get_root(doc.get());
        std::string                       tools;
        size_t                            i, n;
        yyjson_val *                      t;
        yyjson_arr_foreach(yyjson_obj_get(root, "tools"), i, n, t) {
            tools += std::string(tools.empty() ? "" : " ") +
                     yyjson_get_str(yyjson_obj_get(yyjson_obj_get(t, "function"), "name"));
        }
        yyjson_val *      messages = yyjson_obj_get(root, "messages");
        yyjson_val *      last     = yyjson_arr_get_last(messages);
        const std::string role     = yyjson_get_str(yyjson_obj_get(last, "role"));
        const std::string text     = role == "user" ? yyjson_get_str(yyjson_obj_get(last, "content")) : "";
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_systems.push_back(yyjson_get_str(yyjson_obj_get(yyjson_arr_get(messages, 0), "content")));
            g_tools.push_back(tools);
        }
        // the turns that compact: the summary they ask, empty when the
        // history opens on empty, then the request they fold, answered with
        // the tokens it held, none for quiet; the bodies of the turn that
        // opens on one and asks two kept
        const bool        summary = text.rfind("Summarize this conversation", 0) == 0;
        yyjson_val *      opening = yyjson_obj_get(yyjson_arr_get(messages, 1), "content");
        const std::string first   = yyjson_is_str(opening) ? yyjson_get_str(opening) : "";
        if (summary || text == "two" || text == "quiet") {
            const std::shared_ptr<char> sent(yyjson_val_write(messages, 0, nullptr), free);
            if (summary ? first == "one" : text == "two") {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_compacted.push_back(sent.get());
            }
            const std::string content = summary ? (first == "empty" ? "" : "S") : "done";
            const std::string finish  = summary && first == "cut" ? ",\"finish_reason\":\"length\"" : "";
            const std::string usage   = summary       ? "{\"prompt_tokens\":20,\"completion_tokens\":5}" :
                                        text == "two" ? "{\"prompt_tokens\":130,\"completion_tokens\":10}" :
                                                        "";
            res.set_content("data: {\"choices\":[{\"delta\":{\"content\":\"" + content + "\"}" + finish + "}]}\n\n" +
                                (usage.empty() ? "" : "data: {\"choices\":[],\"usage\":" + usage + "}\n\n") +
                                "data: [DONE]\n\n",
                            "text/event-stream");
            return;
        }
        // a message silent takes the request and answers nothing, not even
        // the head of the answer, until the laws let it
        if (text == "silent") {
            g_silent = true;
            for (int i = 0; i < 1000 && !g_released; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return;
        }
        // the conversation of a message vanish erased once its call streamed
        // into the reply, before the end of the stream
        if (text == "vanish") {
            res.set_chunked_content_provider("text/event-stream", [](size_t, httplib::DataSink & sink) {
                const std::string call =
                    "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":"
                    "\"bash\",\"arguments\":\"{}\"}}]}";
                const std::string chunk = "data: {\"choices\":[{\"delta\":" + call + "}]}\n\n";
                sink.write(chunk.data(), chunk.size());
                std::string id;
                while (id.empty()) {
                    library_each([&](const conversation & c) {
                        for (const conversation_entry & e : c.entries) {
                            if (!e.rounds.empty() && !e.rounds[0].calls.empty() && c.entries[0].text == "vanish") {
                                id = c.id;
                            }
                        }
                    });
                    std::this_thread::yield();
                }
                library_remove({ id });
                const std::string done = "data: [DONE]\n\n";
                sink.write(done.data(), done.size());
                sink.done();
                return true;
            });
            return;
        }
        const std::string delta =
            text == "fold" ? "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"config\","
                             "\"arguments\":\"{\\\"description\\\":\\\"x\\\",\\\"lines\\\":\\\"compact\\\"}\"}}]}" :
            text == "twin" ? "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":"
                             "\"twin_config\",\"arguments\":\"{}\"}}]}" :
            text == "go" ? "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"bash\",\"arguments\":"
                           "\"{\\\"description\\\":\\\"x\\\",\\\"command\\\":\\\"echo hi\\\"}\"}}]}" :
            text == "page" ?
                           "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"client_config\","
                           "\"arguments\":\"{\\\"description\\\":\\\"x\\\",\\\"lines\\\":\\\"show version\\\"}\"}}]}" :
                           "{\"content\":\"done\"}";
        res.set_content("data: {\"choices\":[{\"delta\":" + delta + "}]}\n\ndata: [DONE]\n\n", "text/event-stream");
    });
    // an MCP server of the 2025 era serving a tool of the name the server
    // serves itself on every machine, a call answering the name it was called
    // by, and one that fails every request
    server.Post("/twin", [](const httplib::Request & req, httplib::Response & res) {
        const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
        yyjson_val *                      root   = yyjson_doc_get_root(doc.get());
        const std::string                 method = yyjson_get_str(yyjson_obj_get(root, "method"));
        if (!yyjson_obj_get(root, "id")) {
            res.status = 202;
            return;
        }
        const std::string result =
            method == "initialize" ?
                "\"result\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{}}" :
            method == "tools/list" ?
                "\"result\":{\"tools\":[{\"name\":\"config\",\"inputSchema\":{\"type\":\"object\"}}]}" :
            method == "tools/call" ?
                "\"result\":{\"content\":[{\"type\":\"text\",\"text\":" +
                    json(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "params"), "name"))) + "}]}" :
                "\"error\":{\"code\":-32601,\"message\":\"no such method\"}";
        res.set_content("{\"jsonrpc\":\"2.0\",\"id\":1," + result + "}", "application/json");
    });
    // an endpoint that lists its models two seconds late
    server.Get("/slow/v1/models", [](const httplib::Request &, httplib::Response & res) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        res.set_content("{\"data\":[{\"id\":\"z\"}]}", "application/json");
    });
    // an endpoint that lists its models once the laws let it
    server.Get("/hung/v1/models", [](const httplib::Request &, httplib::Response & res) {
        g_hanging = true;
        for (int i = 0; i < 1000 && !g_released; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        res.set_content("{\"data\":[{\"id\":\"h\"}]}", "application/json");
    });
    server.Post("/down", [](const httplib::Request & req, httplib::Response & res) {
        g_down += req.body.find("\"initialize\"") != std::string::npos;
        res.status = 500;
        res.set_content("down", "text/plain");
    });
    const int         port = server.bind_to_any_port("127.0.0.1");
    std::thread       listening([&] { server.listen_after_bind(); });
    const std::string at = "http://127.0.0.1:" + std::to_string(port);
    server.wait_until_ready();

    const fs::path root = fs::temp_directory_path() / ("agent_test." + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    std::ofstream(root / "server.conf") << "set endpoints url e " << at << "/v1\n"
                                        << "set chat model e/m\n"
                                        << "set chat system-prompt 'be brief'\n"
                                        << "set models compaction-trigger-tokens e/m 100\n"
                                        << "set models compaction-preserve-steps e/m 2\n"
                                        << "set mcp url down " << at << "/down\n"
                                        << "set mcp url twin " << at << "/twin\n";
    laws_factory();
    engine_start((root / "server.conf").u8string(), "test");
    library_open((root / "conversations").u8string());

    conversation c;
    c.id = conversation_uuid();
    conversation_entry user;
    user.text = "go";
    conversation_append(c, std::nullopt, user);
    library_create(c, {});
    const std::string first = c.entries[0].id;
    law(agent_answer(c.id, [&](conversation &) { return first; }).empty(), "a turn starts");
    for (int i = 0; i < 1000 && agent_busy(c.id); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(!agent_busy(c.id), "the turn ends");

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        law(g_systems.size() == 2, "the turn takes two rounds, a call then an answer");
        g_systems.resize(2);
        g_tools.resize(2);
        for (size_t r = 0; r < 2; r++) {
            law(g_systems[r] == "be brief",
                "round " + std::to_string(r + 1) + " sends the system prompt alone: " + json(g_systems[r]));
        }
        law(g_down == 1, "a server that fails is asked once in the turn: " + std::to_string(g_down.load()));
        const words_t listed = split(g_tools[0], ' ');
        law(std::count(listed.begin(), listed.end(), "config") == 1 &&
                std::count(listed.begin(), listed.end(), "twin_config") == 1,
            "a tool whose name the server holds goes under its server and its name: " + g_tools[0]);
    }

    // the entries of the conversation, as the library holds them
    const auto entries = [&] {
        std::vector<conversation_entry> out;
        library_each([&](const conversation & x) {
            if (x.id == c.id) {
                out = x.entries;
            }
        });
        return out;
    };
    const auto settled = [&] {
        for (int i = 0; i < 1000 && agent_busy(c.id); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return !agent_busy(c.id);
    };
    law(agent_command(c.id, "show version").empty() && settled() && entries().back().role == ROLE_CLI &&
            entries().back().input == "show version" && entries().back().ok &&
            entries().back().output.find("server") != std::string::npos,
        "a line the user types runs on the server, its output after the thread");
    // the number of the question a conversation asks, as its pages read it,
    // the last one seen once none stands
    library_follower watcher;
    std::string      seen;
    const auto       number = [&]() -> uint64_t {
        seen += library_follow(watcher, 0, {});
        const size_t at = seen.rfind("\"question\":");
        return at == std::string::npos ? 0 : std::stoull(seen.substr(at + 11));
    };
    law(agent_command(c.id, "set endpoints url t9 https://example.com/v1").empty(), "a line that asks starts");
    for (int i = 0;
         i < 1000 &&
         !agent_fill(c.id, number(), "{\"action\":\"accept\",\"content\":{\"endpoints url t9\":true}}").empty();
         i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(settled() && entries().back().input == "set endpoints url t9 https://example.com/v1" &&
            entries().back().output == "+ set endpoints url t9 https://example.com/v1" &&
            engine_get("endpoints url t9").value_or("") == "https://example.com/v1",
        "a url a line asks to let runs once the user lets it: " + entries().back().output);

    // a message after the thread, a turn answering it; the call of its turn,
    // once it settled
    const auto message = [&](const std::string & text) {
        return agent_answer(c.id,
                            [&](conversation & x) {
                                conversation_entry e;
                                e.text = text;
                                return x.entries[conversation_append(x, x.leaf, e)].id;
                            })
            .empty();
    };
    const auto called = [&] {
        const conversation_entry e = entries().back();
        return e.rounds.empty() || e.rounds[0].calls.empty() ? conversation_call{} : e.rounds[0].calls[0];
    };
    // until a question of the kind stands, answered as answer does
    const auto when = [&](const std::function<bool()> & answer) {
        for (int i = 0; i < 1000 && !answer(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    law(message("twin"), "a turn calling a tool served under its server starts");
    when([&] { return agent_grant(c.id, number(), "once").empty(); });
    when([&] { return settled(); });
    law(called().name == "twin_config" && called().ok && called().result == "config",
        "a tool served under its server is called by its own name: " + called().result);
    // the server turning its own tool off keeps the name of the tool of the
    // MCP server as it was, so no line of tools use changes what it names
    engine_run("set tools use config off", ENGINE_ADMIN);
    law(message("still"), "a turn with the own tool off starts");
    when([&] { return settled(); });
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const words_t               sent = split(g_tools.back(), ' ');
        law(std::count(sent.begin(), sent.end(), "twin_config") == 1 &&
                std::count(sent.begin(), sent.end(), "config") == 0,
            "a tool turned off keeps its name, the one after it its prefix: " + g_tools.back());
    }
    engine_run("set tools use config on", ENGINE_ADMIN);
    library_follower page;
    agent_attach();
    agent_attach();
    library_follow(page, 0, {});
    law(message("page"), "a turn calling the pages starts");
    const size_t count = entries().size();
    law(!message("again") && entries().size() == count, "a message sent while a turn runs enters nothing");
    when([&] { return agent_grant(c.id, 0, "once") == "question 0 is not the one asked"; });
    law(agent_grant(c.id, 0, "once") == "question 0 is not the one asked",
        "an answer that names another question settles nothing");
    when([&] { return agent_grant(c.id, number(), "once").empty(); });
    std::string asked;
    when([&] {
        asked += library_follow(page, 0, {});
        return asked.find("\"kind\":\"run\",\"lines\":\"show version\"") != std::string::npos;
    });
    law(asked.find("\"lines\":\"show version\"") != std::string::npos, "the call asks the pages to run its lines");
    const uint64_t run = number();
    when([&] { return agent_ran(c.id, run, "! page ran", true).empty(); });
    law(settled() && called().name == "client_config" && called().ok == true && called().result == "! page ran",
        "the first page that answers settles the call: " + called().result);
    law(!agent_ran(c.id, run, "! page ran too", true).empty() && called().result == "! page ran",
        "the answer of another page settles nothing more");
    library_follow(page, 0, {});
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        law(g_tools.back().find("client_config") != std::string::npos,
            "the model sees the tool of the pages: " + g_tools.back());
    }
    agent_detach();
    law(message("page"), "a second turn calling the pages starts");
    when([&] { return agent_grant(c.id, number(), "once").empty(); });
    asked.clear();
    when([&] {
        asked += library_follow(page, 0, {});
        return asked.find("\"kind\":\"run\"") != std::string::npos;
    });
    agent_detach();
    const bool left = settled();
    law(left && called().ok == false && called().result == "No page is attached",
        "the last page that leaves answers the call: " + called().result);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        law(g_tools.back().find("client_config") == std::string::npos,
            "the tool leaves with the last page: " + g_tools.back());
    }

    // a question asked after a turn whose last request held 150 tokens, the
    // first message of the conversation given, its turn once it settled, the
    // turn the page answers the same way for one then two
    const auto compacting = [&](const std::string & opening, const std::string & question) {
        conversation folded;
        folded.id = conversation_uuid();
        conversation_entry first;
        first.text = opening;
        conversation_append(folded, std::nullopt, first);
        conversation_entry earlier;
        earlier.role = ROLE_ASSISTANT;
        earlier.rounds.emplace_back();
        earlier.rounds[0].text = "first";
        earlier.stats          = conversation_stats{ 1, 1, 1, 150 };
        conversation_append(folded, folded.leaf, earlier);
        conversation_entry asked;
        asked.text            = question;
        const std::string ask = folded.entries[conversation_append(folded, folded.leaf, asked)].id;
        library_create(folded, {});
        agent_answer(folded.id, [&](conversation &) { return ask; });
        for (int i = 0; i < 1000 && agent_busy(folded.id); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        conversation_entry answer;
        library_each([&](const conversation & x) {
            if (x.id == folded.id) {
                answer = x.entries.back();
            }
        });
        return answer;
    };
    const conversation_entry answer = compacting("one", "two");
    {
        std::ifstream            in(COMPACTION);
        std::vector<std::string> golden;
        for (std::string line; std::getline(in, line);) {
            golden.push_back(line);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        law(golden.size() == 2 && g_compacted == golden,
            "a history compacts as the page compacts it, the very bodies sent: " +
                (g_compacted.empty() ? std::string("none") : g_compacted[0]));
    }
    law(answer.rounds.size() == 2 && answer.rounds[0].text == "S" && answer.rounds[0].kept == 2 &&
            answer.rounds[1].text == "done" && !answer.rounds[1].kept && answer.stats && answer.stats->tokens == 15 &&
            answer.stats->context == 140,
        "the summary is a round of the turn that keeps the preserved steps, the tokens held kept with the turn");
    const conversation_entry empty = compacting("empty", "two");
    law(empty.rounds.empty() && empty.error == std::optional<std::string>("Endpoint e wrote no summary"),
        "a summary that comes back empty fails the turn, the history left as it was");
    const conversation_entry quiet = compacting("silent", "quiet");
    law(quiet.rounds.size() == 2 && quiet.stats && !quiet.stats->context,
        "once folded, the tokens held are those the next request tells");

    // compact runs only in a conversation, the config tool of the agent the
    // one place it takes
    const engine_result               console = engine_run("compact", ENGINE_ADMIN);
    const std::shared_ptr<yyjson_doc> lines(yyjson_read("{\"lines\":\"compact\"}", 19, 0), yyjson_doc_free);
    law(!console.ok && console.text == "% no conversation here" && !console.compact &&
            tools_config(yyjson_doc_get_root(lines.get()), {}, c.id).compact &&
            !tools_config(yyjson_doc_get_root(lines.get()), {}, "").compact,
        "compact runs only in a conversation: " + console.text);
    // show conversations marks the one the batch runs in, and an erase asks
    // the user as the page asks it
    const std::shared_ptr<yyjson_doc> listing(yyjson_read("{\"lines\":\"show conversations\"}", 30, 0),
                                              yyjson_doc_free);
    law(tools_config(yyjson_doc_get_root(listing.get()), {}, c.id).text.find("! * " + c.id.substr(0, 8)) !=
                std::string::npos &&
            engine_run("show conversations", ENGINE_ADMIN).text.find("! * ") == std::string::npos,
        "show conversations marks the conversation the batch runs in");
    const engine_result erase = engine_run("erase " + c.id.substr(0, 8), ENGINE_MCP);
    law(erase.asks.size() == 1 && erase.asks[0].question == "erase this conversation",
        "an erase asks for a conversation as the page asks: " +
            (erase.asks.empty() ? std::string() : erase.asks[0].question));
    // a batch that asks several things asks them under one question, each on
    // a line of its own as the lines of the batch, the CLI as written
    const std::string batch =
        R"({"lines":"set endpoints url u1 https://a.example/v1\nset endpoints url u2 https://b.example/v1"})";
    const std::shared_ptr<yyjson_doc> two(yyjson_read(batch.data(), batch.size(), 0), yyjson_doc_free);
    const tool_outcome                both = tools_config(yyjson_doc_get_root(two.get()), {}, c.id);
    law(both.asks.size() == 2 &&
            both.text == "Allow this change of the KiSS server " + engine_get("server hostname").value_or("") +
                             "?\nlet endpoints u1 reach https://a.example/v1\nlet endpoints u2 reach "
                             "https://b.example/v1",
        "a batch asks its things under one question, one a line: " + both.text);
    engine_run("set models compaction-preserve-steps e/m 1", ENGINE_ADMIN);
    // the entries of a conversation once its turn ended
    const auto waited = [&](const std::string & id) {
        for (int i = 0; i < 1000 && agent_busy(id); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::vector<conversation_entry> out;
        library_each([&](const conversation & x) {
            if (x.id == id) {
                out = x.entries;
            }
        });
        return out;
    };
    // a turn answered, then the line that asks to compact, the summary it
    // asks the same on the page
    conversation typed;
    typed.id = conversation_uuid();
    conversation_entry asked_one;
    asked_one.text = "one";
    conversation_append(typed, std::nullopt, asked_one);
    conversation_entry first_reply;
    first_reply.role = ROLE_ASSISTANT;
    first_reply.rounds.emplace_back();
    first_reply.rounds[0].text = "first";
    conversation_append(typed, typed.leaf, first_reply);
    library_create(typed, {});
    law(agent_command(typed.id, "compact").empty(), "a line that compacts starts");
    std::vector<conversation_entry> after = waited(typed.id);
    {
        std::ifstream in(COMPACT);
        std::string   golden;
        std::getline(in, golden);
        std::lock_guard<std::mutex> lock(g_mutex);
        law(!g_compacted.empty() && g_compacted.back() == golden,
            "a line that compacts asks the summary the page asks: " +
                (g_compacted.empty() ? std::string("none") : g_compacted.back()));
    }
    law(after.size() == 4 && after[2].role == ROLE_CLI && after[2].ok && after[2].output.empty() &&
            after[3].role == ROLE_ASSISTANT && after[3].parent == after[2].id && after[3].compaction &&
            after[3].rounds.size() == 1 && after[3].rounds[0].text == "S" && after[3].rounds[0].kept == 1 &&
            !after[3].error,
        "the summary is written alone after the line that asks it, its entry holding so");
    law(!agent_answer(typed.id, [&](conversation &) { return after[2].id; }).empty() &&
            agent_answer(
                typed.id, [&](conversation &) { return after[2].id; }, true)
                .empty(),
        "a summary writes itself again after its line, a line no message to answer");
    after = waited(typed.id);
    law(after.size() == 5 && after[4].parent == after[2].id && after[4].rounds.size() == 1 &&
            after[4].rounds[0].kept == 1,
        "the summary written again enters beside the one it had");
    engine_run("set models compaction-preserve-steps e/m 3", ENGINE_ADMIN);
    law(agent_command(typed.id, "compact").empty(), "a line with nothing to compact starts");
    after = waited(typed.id);
    law(after.back().role == ROLE_ASSISTANT && after.back().rounds.empty() &&
            after.back().error == std::optional<std::string>("Nothing to compact"),
        "a line with nothing to compact fails its turn, asking nothing");
    engine_run("set models compaction-preserve-steps e/m 1", ENGINE_ADMIN);
    // the line dropped, the summary hangs from the answer before it and still
    // writes itself again from there
    library_change(typed.id,
                   [&](conversation & c) {
                       conversation_drop(c, after[2].id);
                       return std::string();
                   },
                   {});
    after = waited(typed.id);
    law(agent_answer(
            typed.id, [&](conversation &) { return after[1].id; }, true)
            .empty(),
        "a summary whose line is dropped writes itself again");
    after = waited(typed.id);
    law(after.back().parent == after[1].id && after.back().compaction && after.back().rounds.size() == 1 &&
            after.back().rounds[0].kept == 1 && !after.back().error,
        "a summary whose line is dropped writes itself again after the answer it now follows");
    // a summary whose line was the first entry, dismissed: its retry follows no
    // entry and is refused, the conversation left as it was
    conversation lone;
    lone.id = conversation_uuid();
    conversation_entry lone_line;
    lone_line.role             = ROLE_CLI;
    lone_line.input            = "compact";
    lone_line.ok               = true;
    const std::string  line_id = lone.entries[conversation_append(lone, std::nullopt, lone_line)].id;
    conversation_entry lone_reply;
    lone_reply.role            = ROLE_ASSISTANT;
    lone_reply.compaction      = true;
    lone_reply.error           = "Nothing to compact";
    const std::string reply_id = lone.entries[conversation_append(lone, lone.leaf, lone_reply)].id;
    conversation_drop(lone, line_id);
    library_create(lone, {});
    httplib::Request  retry_req;
    httplib::Response retry_res;
    retry_req.body = "{\"id\":\"" + lone.id + "\",\"entry\":\"" + reply_id + "\"}";
    sync_action(retry_req, retry_res, "retry");
    law(retry_res.status == 400 && retry_res.body.find("follows no entry") != std::string::npos &&
            !agent_busy(lone.id) && waited(lone.id).size() == 1,
        "a retry of a summary that follows no entry is refused: " + std::to_string(retry_res.status) + " " +
            retry_res.body);
    // a summary the output limit cuts fails its turn, never a boundary
    conversation cut;
    cut.id = conversation_uuid();
    conversation_entry asked_cut;
    asked_cut.text = "cut";
    conversation_append(cut, std::nullopt, asked_cut);
    conversation_append(cut, cut.leaf, first_reply);
    library_create(cut, {});
    law(agent_command(cut.id, "compact").empty(), "a line whose summary is cut starts");
    after = waited(cut.id);
    law(after.back().compaction && after.back().rounds.empty() &&
            after.back().error == std::optional<std::string>("Endpoint e cut the summary at its output limit"),
        "a summary the output limit cuts fails its turn, never a boundary");
    // the model asks a compaction through its config tool: the history folds
    // before its next request
    conversation folding;
    folding.id = conversation_uuid();
    conversation_entry fold;
    fold.text                = "fold";
    const std::string folded = folding.entries[conversation_append(folding, std::nullopt, fold)].id;
    library_create(folding, {});
    law(agent_answer(folding.id, [&](conversation &) { return folded; }).empty(), "a turn that folds starts");
    after = waited(folding.id);
    law(after.size() == 2 && after[1].rounds.size() == 3 && after[1].rounds[0].calls.size() == 1 &&
            after[1].rounds[0].calls[0].ok && after[1].rounds[1].text == "S" && after[1].rounds[1].kept == 1 &&
            after[1].rounds[2].text == "done" && !after[1].error,
        "a batch of the model that compacts folds the history before its next request");
    engine_run("set models compaction-preserve-steps e/m 2", ENGINE_ADMIN);

    const auto known = [](const std::string & secret) {
        const words_t all = keychain_known();
        return std::find(all.begin(), all.end(), secret) != all.end();
    };
    engine_run("set endpoints key t9 sk-dropped-1234", ENGINE_ADMIN);
    const bool held = known("sk-dropped-1234");
    engine_run("no endpoints t9", ENGINE_ADMIN);
    law(held && !engine_get("endpoints key", "t9") && known("sk-dropped-1234"),
        "a secret the configuration drops stays sealed in what still holds it");

    conversation gone;
    gone.id = conversation_uuid();
    conversation_entry vanish;
    vanish.text = "vanish";
    conversation_append(gone, std::nullopt, vanish);
    library_create(gone, {});
    const std::string asked_gone = gone.entries[0].id;
    law(agent_answer(gone.id, [&](conversation &) { return asked_gone; }).empty(), "a turn that vanishes starts");
    for (int i = 0; i < 1000 && agent_busy(gone.id); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    bool found = false;
    library_each([&](const conversation & x) { found = found || x.id == gone.id; });
    law(!agent_busy(gone.id) && !found, "a conversation erased once its round streamed stops its turn");

    // a stop ends at once a turn whose endpoint took the request and answers
    // nothing yet
    conversation mute;
    mute.id = conversation_uuid();
    conversation_entry hush;
    hush.text = "silent";
    conversation_append(mute, std::nullopt, hush);
    library_create(mute, {});
    const std::string asked_mute = mute.entries[0].id;
    law(agent_answer(mute.id, [&](conversation &) { return asked_mute; }).empty(),
        "a turn sent to a silent endpoint starts");
    for (int i = 0; i < 500 && !g_silent; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto stopping = std::chrono::steady_clock::now();
    agent_stop(mute.id);
    for (int i = 0; i < 300 && agent_busy(mute.id); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(g_silent && !agent_busy(mute.id) && std::chrono::steady_clock::now() - stopping < std::chrono::seconds(1),
        "a stop ends at once a turn whose endpoint answers nothing yet");

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT)
    // a stop ends at once a turn whose endpoint takes the connection and
    // never answers its TLS handshake: a socket that listens, its first
    // byte of ClientHello read to know the turn is in the handshake
    const auto  shy = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in where{};
    where.sin_family      = AF_INET;
    where.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size        = sizeof(where);
    bind(shy, reinterpret_cast<sockaddr *>(&where), sizeof(where));
    listen(shy, 1);
    getsockname(shy, reinterpret_cast<sockaddr *>(&where), &size);
    engine_run("set endpoints url shy https://127.0.0.1:" + std::to_string(ntohs(where.sin_port)) +
                   "/v1\nset chat model shy/x",
               ENGINE_ADMIN);
    conversation shaking;
    shaking.id = conversation_uuid();
    conversation_entry hello;
    hello.text = "hello";
    conversation_append(shaking, std::nullopt, hello);
    library_create(shaking, {});
    const std::string asked_shaking = shaking.entries[0].id;
    law(agent_answer(shaking.id, [&](conversation &) { return asked_shaking; }).empty(),
        "a turn sent to an endpoint that never answers its TLS handshake starts");
    const auto peer      = accept(shy, nullptr, nullptr);
    char       client[1] = {};
    const bool shaken    = recv(peer, client, 1, 0) == 1;
    const auto halting   = std::chrono::steady_clock::now();
    agent_stop(shaking.id);
    for (int i = 0; i < 300 && agent_busy(shaking.id); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(shaken && !agent_busy(shaking.id) && std::chrono::steady_clock::now() - halting < std::chrono::seconds(1),
        "a stop ends at once a turn whose endpoint never answers its TLS handshake");
#    if defined(_WIN32)
    closesocket(peer);
    closesocket(shy);
#    else
    close(peer);
    close(shy);
#    endif
    engine_run("set chat model e/m\nno endpoints shy", ENGINE_ADMIN);
#endif

    // a request whose stop came before it goes out never connects: a socket
    // that listens is asked for no connection
    {
        const auto  idle = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in at{};
        at.sin_family      = AF_INET;
        at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length   = sizeof(at);
        bind(idle, reinterpret_cast<sockaddr *>(&at), sizeof(at));
        listen(idle, 1);
        getsockname(idle, reinterpret_cast<sockaddr *>(&at), &length);
        remote r;
        r.url     = "http://127.0.0.1:" + std::to_string(ntohs(at.sin_port)) + "/v1";
        r.timeout = 30;
        api_stop early;
        early.stop();
        std::string path;
        const auto  client = api_client(r, early, path);
        const auto  res    = client->Get(path);
        fd_set      ready;
        timeval     none{ 0, 0 };
        FD_ZERO(&ready);
        FD_SET(idle, &ready);
        const bool asked = select((int) idle + 1, &ready, nullptr, nullptr, &none) > 0;
        law(!res && res.error() == httplib::Error::Canceled && !asked,
            "a request whose stop came before it goes out never connects");
#if defined(_WIN32)
        closesocket(idle);
#else
        close(idle);
#endif
    }

    // a stop ends at once a request whose body an endpoint stops reading,
    // its client blocked in a send: the endpoint reads the head and a part
    // of the body, then nothing more
    {
        const auto  deaf = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in at{};
        at.sin_family      = AF_INET;
        at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length   = sizeof(at);
        bind(deaf, reinterpret_cast<sockaddr *>(&at), sizeof(at));
        listen(deaf, 1);
        getsockname(deaf, reinterpret_cast<sockaddr *>(&at), &length);
        remote r;
        r.url     = "http://127.0.0.1:" + std::to_string(ntohs(at.sin_port)) + "/v1";
        r.timeout = 30;
        api_stop          sending;
        std::atomic<bool> sent_out{ false };
        std::thread       uploading([&] {
            std::string path;
            const auto  client = api_client(r, sending, path);
            client->Post(path, std::string(64u << 20, 'x'), "application/octet-stream");
            sent_out = true;
        });
        const auto        peer = accept(deaf, nullptr, nullptr);
#if defined(_WIN32)
        const DWORD wait = 5000;
        setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&wait), sizeof(wait));
#else
        const timeval wait{ 5, 0 };
        setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
#endif
        size_t heard = 0;
        char   piece[65536];
        while (heard < (1u << 20)) {
            const auto n = recv(peer, piece, sizeof(piece), 0);
            if (n <= 0) {
                break;
            }
            heard += (size_t) n;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto halting = std::chrono::steady_clock::now();
        sending.stop();
        for (int i = 0; i < 100 && !sent_out; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        law(heard >= (1u << 20) && sent_out && std::chrono::steady_clock::now() - halting < std::chrono::seconds(1),
            "a stop ends at once a request whose body the endpoint stops reading");
#if defined(_WIN32)
        closesocket(peer);
        closesocket(deaf);
#else
        close(peer);
        close(deaf);
#endif
        uploading.join();
    }

    // what the sources serve: the models of each endpoint, the tools of this
    // server, of the pages and of each MCP server, a source that fails
    // telling why, shown and completed as on the page
    const auto has = [](const words_t & all, const std::string & w) {
        return std::find(all.begin(), all.end(), w) != all.end();
    };
    law(engine_run("show models", ENGINE_ADMIN).text ==
            "! endpoints e\nset models compaction-trigger-tokens e/m 100\nset models compaction-preserve-steps e/m 2",
        "show models lists the models every endpoint serves, by endpoint");
    const std::string tools = engine_run("show tools", ENGINE_ADMIN).text;
    const size_t      kiss  = tools.find("! KiSS\n");
    law(kiss != std::string::npos && tools.find("set tools use config on", kiss) < tools.find("! pages", kiss) &&
            tools.find("! pages\nset tools use client_config consent") != std::string::npos &&
            tools.find("! mcp twin\nset tools use twin_config consent") != std::string::npos &&
            tools.find("! mcp down http://") != std::string::npos,
        "show tools lists the tools of this server, of the pages and of each MCP server, one that fails telling why");
    law(engine_run("show models e/", ENGINE_ADMIN).text ==
            "set models compaction-trigger-tokens e/m 100\nset models compaction-preserve-steps e/m 2",
        "show names a model an endpoint serves by a prefix");
    law(has(engine_suggest("set chat model "), "e/m"), "chat model completes with the models the endpoints serve");
    const words_t use = engine_suggest("set tools use ");
    law(has(use, "view") && has(use, "client_config") && has(use, "config") && has(use, "twin_config"),
        "tools use completes with the tools served");
    law(has(engine_suggest("set files shell "), "bash"), "files shell completes with the shells");

    // completion never waits on a source: it answers with what the sources
    // served last, and the answer of a slow one completes the words typed next
    engine_run("set endpoints url slow " + at + "/slow/v1", ENGINE_ADMIN);
    const auto started = std::chrono::steady_clock::now();
    engine_suggest("set chat model ");
    law(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500),
        "completion answers at once while a source is slow");
    const auto setting = std::chrono::steady_clock::now();
    engine_run("set models seed e/m 1\nset tools max-rounds 10", ENGINE_ADMIN);
    law(std::chrono::steady_clock::now() - setting < std::chrono::milliseconds(500),
        "a set of an item held or of a key without one asks no source");
    bool late = false;
    for (int i = 0; i < 100 && !late; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        late = has(engine_suggest("set chat model "), "slow/z");
    }
    law(late, "completion offers what a slow source serves once it answers");

    // the process ends at once while a source hangs: the refresh in flight,
    // once the one before it answered, stopped and its thread joined
    engine_run("set endpoints url hung " + at + "/hung/v1", ENGINE_ADMIN);
    for (int i = 0; i < 500 && !g_hanging; i++) {
        engine_suggest("set chat model ");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto ending = std::chrono::steady_clock::now();
    quit_all();
    law(g_hanging && std::chrono::steady_clock::now() - ending < std::chrono::seconds(1),
        "the process ends at once while a source hangs");
    g_released = true;

    server.stop();
    listening.join();
    fs::remove_all(root);
    printf("[Laws] agent_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
