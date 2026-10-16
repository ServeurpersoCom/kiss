// mcp.cpp: the laws of the client of the MCP servers, run as the build ends
// against a server of the test: the session it opens carries its id and the
// version of the protocol, the tool list comes page by page, a stream of
// events reads whatever its line ends, a call gives its text, its images and
// its error flag, and a server that answers what does not read serves nothing

#include "lib/mcp.h"

#include "httplib.h"
#include "yyjson.h"

#include <cstdio>
#include <memory>
#include <thread>

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "mcp_test: %s\n", what.c_str());
    }
}

// what the server of the test saw: the session id and protocol version of
// every request after the handshake, and the arguments of the last call
static bool        g_session = true;
static std::string g_args;

// an answer as one event, its lines ending in CR LF
static std::string event(const std::string & json) {
    return "event: message\r\ndata: " + json + "\r\n\r\n";
}

int main() {
    httplib::Server server;
    server.Post("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
        yyjson_val *                      root   = yyjson_doc_get_root(doc.get());
        const std::string                 method = yyjson_get_str(yyjson_obj_get(root, "method"));
        if (method == "initialize") {
            res.set_header("Mcp-Session-Id", "s1");
            res.set_content("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"protocolVersion\":\"2025-06-18\"}}",
                            "application/json");
            return;
        }
        g_session = g_session && req.get_header_value("Mcp-Session-Id") == "s1" &&
                    req.get_header_value("MCP-Protocol-Version") == "2025-06-18";
        if (method == "notifications/initialized") {
            res.status = 202;
            return;
        }
        yyjson_val * params = yyjson_obj_get(root, "params");
        if (method == "tools/list") {
            const bool second = yyjson_obj_get(params, "cursor") != nullptr;
            res.set_content(
                event(
                    second ?
                        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"tools\":[{\"name\":\"b\",\"inputSchema\":{}}]}}" :
                        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"tools\":[{\"name\":\"a\",\"description\":"
                        "\"does a\",\"inputSchema\":{\"type\":\"object\"}}],\"nextCursor\":\"p2\"}}"),
                "text/event-stream");
            return;
        }
        const std::shared_ptr<char> args(yyjson_val_write(yyjson_obj_get(params, "arguments"), 0, nullptr), free);
        g_args = args.get();
        res.set_content(event("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{}}") +
                            event("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"isError\":true,\"content\":["
                                  "{\"type\":\"text\",\"text\":\"one\"},{\"type\":\"image\",\"mimeType\":"
                                  "\"image/png\",\"data\":\"AAAA\"},{\"type\":\"text\",\"text\":\"two\"}]}}"),
                        "text/event-stream");
    });
    server.Post("/bad",
                [](const httplib::Request &, httplib::Response & res) { res.set_content("<html>", "text/html"); });
    const int   port = server.bind_to_any_port("127.0.0.1");
    std::thread listening([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    remote r;
    r.name    = "t";
    r.url     = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    r.timeout = 5;
    api_stop                    stop;
    const std::vector<mcp_tool> tools = mcp_tools(r, stop);
    law(tools.size() == 2 && tools[0].name == "a" && tools[0].description == "does a" &&
            tools[0].input == "{\"type\":\"object\"}" && tools[1].name == "b" && tools[0].server == "t",
        "the tool list comes page by page, each tool with its server, description and schema");
    const mcp_outcome out = mcp_call(r, "a", "{\"x\":1}", stop);
    law(g_args == "{\"x\":1}", "a call carries its arguments as an object");
    law(!out.ok && out.text == "one\ntwo" && out.images.size() == 1 && out.images[0].mime == "image/png" &&
            out.images[0].data == "AAAA",
        "a call gives its text, its images and its error flag, past an event that is no answer");
    law(g_session, "every request after the handshake carries the session and the version");

    remote bad = r;
    bad.name   = "bad";
    bad.url    = "http://127.0.0.1:" + std::to_string(port) + "/bad";
    bool threw = false;
    try {
        mcp_tools(bad, stop);
    } catch (const std::exception &) {
        threw = true;
    }
    law(threw, "a server that answers what does not read serves nothing");

    server.stop();
    listening.join();
    printf("mcp_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
