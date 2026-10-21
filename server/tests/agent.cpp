// agent.cpp: the laws of the agent of the server, run as the build ends, a
// turn against an endpoint and MCP servers of the test: every round sends the
// system prompt alone; a server that fails is asked once in the turn; a tool
// served twice stays with the first, the server itself;
// a history compacts as the page compacts it, the very bodies sent;
// a line the user types runs as the config tool runs it, its output after
// the thread, a url it asks to let shown as a form, the line running again
// once the user lets it; the config tool of the pages is the model's while a
// page is attached, one for all of them, its call granted then run by the
// pages, the first answer winning, the last page that leaves answering that
// none is attached

#include "lib/agent.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "httplib.h"
#include "lib/conversation.h"
#include "lib/library.h"
#include "lib/tools.h"
#include "server/server.conf.hpp"
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
            const std::string usage   = summary       ? "{\"prompt_tokens\":20,\"completion_tokens\":5}" :
                                        text == "two" ? "{\"prompt_tokens\":130,\"completion_tokens\":10}" :
                                                        "";
            res.set_content("data: {\"choices\":[{\"delta\":{\"content\":\"" + content + "\"}}]}\n\n" +
                                (usage.empty() ? "" : "data: {\"choices\":[],\"usage\":" + usage + "}\n\n") +
                                "data: [DONE]\n\n",
                            "text/event-stream");
            return;
        }
        const std::string delta =
            text == "fold" ? "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"config\","
                             "\"arguments\":\"{\\\"description\\\":\\\"x\\\",\\\"lines\\\":\\\"compact\\\"}\"}}]}" :
            text == "go" ? "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"bash\",\"arguments\":"
                           "\"{\\\"description\\\":\\\"x\\\",\\\"command\\\":\\\"echo hi\\\"}\"}}]}" :
            text == "page" ?
                           "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"client_config\","
                           "\"arguments\":\"{\\\"description\\\":\\\"x\\\",\\\"lines\\\":\\\"show version\\\"}\"}}]}" :
                           "{\"content\":\"done\"}";
        res.set_content("data: {\"choices\":[{\"delta\":" + delta + "}]}\n\ndata: [DONE]\n\n", "text/event-stream");
    });
    // an MCP server of the 2025 era serving a tool of the name the server
    // serves itself on every machine, and one that fails every request
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
                "\"error\":{\"code\":-32601,\"message\":\"no such method\"}";
        res.set_content("{\"jsonrpc\":\"2.0\",\"id\":1," + result + "}", "application/json");
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
    engine_factory(std::string(reinterpret_cast<const char *>(server_server_conf), server_server_conf_len));
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
        law(g_tools[0].find("bash") != std::string::npos &&
                g_tools[0].find("bash", g_tools[0].find("bash") + 1) == std::string::npos,
            "a tool served twice goes once: " + g_tools[0]);
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
            tools_config(yyjson_doc_get_root(lines.get()), {}, true).compact &&
            !tools_config(yyjson_doc_get_root(lines.get()), {}, false).compact,
        "compact runs only in a conversation: " + console.text);
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
            after[3].role == ROLE_ASSISTANT && after[3].parent == after[2].id && after[3].rounds.size() == 1 &&
            after[3].rounds[0].text == "S" && after[3].rounds[0].kept == 1 && !after[3].error,
        "the summary is written alone after the line that asks it");
    law(agent_answer(typed.id, [&](conversation &) { return after[2].id; }).empty(),
        "a summary writes itself again after its line");
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

    server.stop();
    listening.join();
    fs::remove_all(root);
    printf("[Laws] agent_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
