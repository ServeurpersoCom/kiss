// agent.cpp: the laws of the agent of the server, run as the build ends, a
// turn against an endpoint and MCP servers of the test: every round tells
// the model what the turn left out, under the note of prompts.json, as the
// agent of the page does; a server that fails sits out the rest of the turn,
// asked once; a tool served twice stays with the first, the server itself;
// a line the user types runs as the config tool runs it, its output after
// the thread, a url it asks to let shown as a form, the line running again
// once the user lets it

#include "lib/agent.h"

#include "engine/engine.h"
#include "httplib.h"
#include "lib/conversation.h"
#include "lib/library.h"
#include "lib/prompts.h"
#include "server.conf.hpp"
#include "yyjson.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
        fprintf(stderr, "agent_test: %s\n", what.c_str());
    }
}

// what the endpoint of the test saw: the system prompt and the tools of each
// request, and how often the server that fails was asked to open a session
static std::mutex               g_mutex;
static std::vector<std::string> g_systems;
static std::vector<std::string> g_tools;
static std::atomic<int>         g_down{ 0 };

static std::string json(const std::string & s) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return text.get();
}

int main() {
    httplib::Server server;
    // the endpoint: its first answer calls bash, every next one answers text
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
        size_t first;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_systems.push_back(
                yyjson_get_str(yyjson_obj_get(yyjson_arr_get(yyjson_obj_get(root, "messages"), 0), "content")));
            g_tools.push_back(tools);
            first = g_systems.size() == 1;
        }
        const std::string delta =
            first ? "{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"bash\",\"arguments\":"
                    "\"{\\\"description\\\":\\\"x\\\",\\\"command\\\":\\\"echo hi\\\"}\"}}]}" :
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
                                        << "set mcp url down " << at << "/down\n"
                                        << "set mcp url twin " << at << "/twin\n";
    engine_factory(std::string(reinterpret_cast<const char *>(server_conf), server_conf_len));
    engine_start((root / "server.conf").u8string(), "test");
    library_open((root / "conversations").u8string());

    conversation c;
    c.id = conversation_uuid();
    conversation_entry user;
    user.text = "go";
    conversation_append(c, std::nullopt, user);
    library_create(c, {});
    law(agent_answer(c.id, c.entries[0].id).empty(), "a turn starts");
    for (int i = 0; i < 1000 && agent_busy(c.id); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(!agent_busy(c.id), "the turn ends");

    const std::string           note = "be brief\n\n" + prompts_note("problems") + "\n";
    std::lock_guard<std::mutex> lock(g_mutex);
    law(g_systems.size() == 2, "the turn takes two rounds, a call then an answer");
    g_systems.resize(2);
    g_tools.resize(2);
    for (size_t r = 0; r < 2; r++) {
        law(g_systems[r].rfind(note, 0) == 0 && g_systems[r].find("\nmcp down: ") != std::string::npos &&
                g_systems[r].find("\nmcp twin: tool config left out, KiSS serves it") != std::string::npos,
            "round " + std::to_string(r + 1) + " tells the model what the turn left out: " + json(g_systems[r]));
    }
    law(g_down == 1, "a server that fails is asked once in the turn: " + std::to_string(g_down.load()));
    law(g_tools[0].find("bash") != std::string::npos &&
            g_tools[0].find("bash", g_tools[0].find("bash") + 1) == std::string::npos,
        "a tool served twice goes once: " + g_tools[0]);

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
    law(agent_command(c.id, "set endpoints url t9 https://example.com/v1").empty(), "a line that asks starts");
    for (int i = 0;
         i < 1000 && !agent_fill(c.id, "{\"action\":\"accept\",\"content\":{\"endpoints url t9\":true}}").empty();
         i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    law(settled() && entries().back().input == "set endpoints url t9 https://example.com/v1" &&
            entries().back().output == "+ set endpoints url t9 https://example.com/v1" &&
            engine_get("endpoints url t9").value_or("") == "https://example.com/v1",
        "a url a line asks to let runs once the user lets it: " + entries().back().output);

    server.stop();
    listening.join();
    fs::remove_all(root);
    printf("agent_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
