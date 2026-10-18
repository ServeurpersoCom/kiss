// mcp.cpp: the laws of the client of the MCP servers, run as the build ends
// against servers of the test: a server of the 2025 era gets the session it
// opens, its id and the version of the protocol on every request, the tool
// list comes page by page, a stream of events reads whatever its line ends,
// a call gives its text, its images and its error flag; a server of the
// 2026-07-28 era gets its envelope and headers, and a call it asks the user
// about comes again with their response, unless KiSS answers no such input;
// a server that answers what does not read serves nothing; a server that
// fails keeps the tools it listed last, each call failing

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
        fprintf(stderr, "[Laws] mcp_test broken: %s\n", what.c_str());
    }
}

// what the server of the test saw: the session id and protocol version of
// every request after the handshake, and the arguments of the last call
static bool        g_session = true;
// whether the server of the 2025 era fails every request
static bool        g_gone    = false;
static std::string g_args;
// whether every request of the 2026-07-28 era carried its envelope and headers
static bool        g_modern = true;

// an answer as one event, its lines ending in CR LF
static std::string event(const std::string & json) {
    return "event: message\r\ndata: " + json + "\r\n\r\n";
}

int main() {
    httplib::Server server;
    server.Post("/mcp", [](const httplib::Request & req, httplib::Response & res) {
        if (g_gone) {
            res.status = 503;
            return;
        }
        const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
        yyjson_val *                      root   = yyjson_doc_get_root(doc.get());
        const std::string                 method = yyjson_get_str(yyjson_obj_get(root, "method"));
        if (method == "server/discover") {
            res.set_content(
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32601,\"message\":\"no such "
                "method\"}}",
                "application/json");
            return;
        }
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
    server.Post("/modern", [](const httplib::Request & req, httplib::Response & res) {
        const std::shared_ptr<yyjson_doc> doc(yyjson_read(req.body.data(), req.body.size(), 0), yyjson_doc_free);
        yyjson_val *                      root    = yyjson_doc_get_root(doc.get());
        const std::string                 method  = yyjson_get_str(yyjson_obj_get(root, "method"));
        yyjson_val *                      params  = yyjson_obj_get(root, "params");
        yyjson_val *                      meta    = yyjson_obj_get(params, "_meta");
        yyjson_val *                      version = yyjson_obj_get(meta, "io.modelcontextprotocol/protocolVersion");
        yyjson_val *                      form    = yyjson_obj_get(
            yyjson_obj_get(yyjson_obj_get(meta, "io.modelcontextprotocol/clientCapabilities"), "elicitation"), "form");
        g_modern = g_modern && yyjson_is_str(version) && std::string(yyjson_get_str(version)) == "2026-07-28" && form &&
                   req.get_header_value("MCP-Protocol-Version") == "2026-07-28" &&
                   req.get_header_value("Mcp-Method") == method && !req.has_header("Mcp-Session-Id");
        if (method == "server/discover") {
            res.set_content(
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"resultType\":\"complete\","
                "\"supportedVersions\":[\"2026-07-28\"]}}",
                "application/json");
            return;
        }
        if (method == "tools/list") {
            res.set_content(
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"resultType\":\"complete\",\"tools\":[{"
                "\"name\":\"s\",\"inputSchema\":{}}]}}",
                "application/json");
            return;
        }
        g_modern           = g_modern && req.get_header_value("Mcp-Name") == "s";
        const bool   url   = yyjson_obj_get(yyjson_obj_get(params, "arguments"), "url") != nullptr;
        yyjson_val * typed = yyjson_obj_get(
            yyjson_obj_get(yyjson_obj_get(yyjson_obj_get(params, "inputResponses"), "user"), "content"), "k");
        yyjson_val * state = yyjson_obj_get(params, "requestState");
        if (!typed) {
            res.set_content(std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"resultType\":\"input_"
                                        "required\",\"requestState\":\"st\",\"inputRequests\":{\"user\":{"
                                        "\"method\":\"elicitation/create\",\"params\":{\"mode\":\"") +
                                (url ? "url" : "form") +
                                "\",\"message\":\"type k\",\"requestedSchema\":{\"type\":\"object\","
                                "\"properties\":{\"k\":{\"type\":\"string\"}}}}}}}}",
                            "application/json");
            return;
        }
        const std::string text =
            std::string("got ") + yyjson_get_str(typed) + " " + (yyjson_is_str(state) ? yyjson_get_str(state) : "");
        res.set_content(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"resultType\":\"complete\",\"content\":[{"
            "\"type\":\"text\",\"text\":\"" +
                text + "\"}]}}",
            "application/json");
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
    size_t           asked = 0;
    const mcp_answer none  = [&](const std::string &) {
        asked++;
        return std::string("{\"action\":\"cancel\"}");
    };
    const mcp_outcome out = mcp_call(r, "a", "{\"x\":1}", stop, none);
    law(g_args == "{\"x\":1}", "a call carries its arguments as an object");
    law(!out.ok && out.text == "one\ntwo" && out.images.size() == 1 && out.images[0].mime == "image/png" &&
            out.images[0].data == "AAAA",
        "a call gives its text, its images and its error flag, past an event that is no answer");
    law(g_session, "every request after the handshake carries the session and the version");

    remote modern = r;
    modern.name   = "modern";
    modern.url    = "http://127.0.0.1:" + std::to_string(port) + "/modern";
    std::string      input;
    const mcp_answer typed = [&](const std::string & in) {
        input = in;
        return std::string("{\"action\":\"accept\",\"content\":{\"k\":\"v\"}}");
    };
    const std::vector<mcp_tool> listed = mcp_tools(modern, stop);
    const mcp_outcome           got    = mcp_call(modern, "s", "{}", stop, typed);
    law(listed.size() == 1 && listed[0].name == "s" && g_modern,
        "a server of the 2026-07-28 era gets its envelope and its headers on every request, and no session");
    law(got.ok && got.text == "got v st" && input.find("\"message\":\"type k\"") != std::string::npos,
        "a call the server asks the user about comes again with their response and the state it gave");
    bool refused = false;
    try {
        mcp_call(modern, "s", "{\"url\":true}", stop, none);
    } catch (const std::exception & e) {
        refused = std::string(e.what()).find("which KiSS does not answer") != std::string::npos;
    }
    law(refused && asked == 0, "an input KiSS does not answer fails the call without asking the user");

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
    law(mcp_kept(bad).empty(), "a server never listed keeps no tools");

    g_gone      = true;
    bool failed = false;
    try {
        mcp_call(r, "a", "{}", stop, none);
    } catch (const std::exception &) {
        failed = true;
    }
    bool unlisted = false;
    try {
        mcp_tools(r, stop);
    } catch (const std::exception &) {
        unlisted = true;
    }
    law(failed && unlisted && mcp_kept(r).size() == 2, "a server that fails keeps the tools it listed last");
    remote moved = r;
    moved.url += "/moved";
    law(mcp_kept(moved).empty(), "a server named anew keeps none of the tools of its old name");

    server.stop();
    listening.join();
    printf("[Laws] mcp_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
