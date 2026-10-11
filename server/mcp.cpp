// mcp.cpp: the MCP of the server over Streamable HTTP, stateless, in both eras
//
// A request that carries the 2026-07-28 envelope in _meta speaks that era:
// its MCP-Protocol-Version and Mcp-Method headers must match the body, and
// server/discover answers. A request without it speaks the 2025 era:
// initialize answers without Mcp-Session-Id, since the server keeps no state
// between requests. Notifications get 202.

#include "mcp.h"

#include "engine.h"
#include "tools.h"
#include "yyjson.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

static const char * const MODERN_VERSIONS[] = { "2026-07-28" };
static const char * const LEGACY_VERSIONS[] = { "2025-11-25", "2025-06-18", "2025-03-26" };

static const char * const PROTOCOL_VERSION_META = "io.modelcontextprotocol/protocolVersion";
static const char * const SERVER_INFO_META      = "io.modelcontextprotocol/serverInfo";

static const int PARSE_ERROR         = -32700;
static const int INVALID_REQUEST     = -32600;
static const int METHOD_NOT_FOUND    = -32601;
static const int INVALID_PARAMS      = -32602;
static const int HEADER_MISMATCH     = -32020;
static const int UNSUPPORTED_VERSION = -32022;

// a result the server does not keep: the tools follow the configuration, so a
// client asks again rather than caching
static const int TTL_MS = 0;

using build_t = std::function<void(yyjson_mut_doc *, yyjson_mut_val *)>;

static bool listed(const char * const * list, size_t n, const std::string & v) {
    for (size_t i = 0; i < n; i++) {
        if (v == list[i]) {
            return true;
        }
    }
    return false;
}

static void send(httplib::Response & res, int status, yyjson_mut_doc * doc) {
    char * json = yyjson_mut_write(doc, 0, nullptr);
    res.status  = status;
    res.set_content(json ? json : "{}", "application/json");
    free(json);
    yyjson_mut_doc_free(doc);
}

// a JSON-RPC response to the request id, null when the request carries none
static yyjson_mut_val * response(yyjson_mut_doc * doc, yyjson_val * id) {
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_val(doc, root, "id", id ? yyjson_val_mut_copy(doc, id) : yyjson_mut_null(doc));
    return root;
}

static void error(httplib::Response & res,
                  int                 status,
                  yyjson_val *        id,
                  int                 code,
                  const std::string & message,
                  const build_t &     data = nullptr) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * root = response(doc, id);
    yyjson_mut_val * err  = yyjson_mut_obj_add_obj(doc, root, "error");
    yyjson_mut_obj_add_int(doc, err, "code", code);
    yyjson_mut_obj_add_strncpy(doc, err, "message", message.c_str(), message.size());
    if (data) {
        data(doc, yyjson_mut_obj_add_obj(doc, err, "data"));
    }
    send(res, status, doc);
}

// a result; one of the 2026-07-28 era says it is complete, the server never
// asking the client for input in the middle of a request
static void result(httplib::Response & res, yyjson_val * id, bool modern, const build_t & body) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * root = response(doc, id);
    yyjson_mut_val * out  = yyjson_mut_obj_add_obj(doc, root, "result");
    if (modern) {
        yyjson_mut_obj_add_str(doc, out, "resultType", "complete");
    }
    body(doc, out);
    send(res, 200, doc);
}

static void server_info(yyjson_mut_doc *    doc,
                        yyjson_mut_val *    info,
                        const std::string & name,
                        const std::string & version) {
    yyjson_mut_obj_add_strncpy(doc, info, "name", name.c_str(), name.size());
    yyjson_mut_obj_add_str(doc, info, "title", "KiSS server");
    yyjson_mut_obj_add_strncpy(doc, info, "version", version.c_str(), version.size());
}

static void tools_body(yyjson_mut_doc * doc, yyjson_mut_val * out, const std::string & server) {
    yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, out, "tools");
    for (const tool & t : tools_list(server)) {
        const std::string name = server + "_" + t.suffix;
        yyjson_mut_val *  item = yyjson_mut_arr_add_obj(doc, list);
        yyjson_mut_obj_add_strncpy(doc, item, "name", name.c_str(), name.size());
        yyjson_mut_obj_add_strncpy(doc, item, "description", t.description.c_str(), t.description.size());
        yyjson_doc * schema = yyjson_read(t.input.c_str(), t.input.size(), 0);
        yyjson_mut_obj_add_val(doc, item, "inputSchema", yyjson_val_mut_copy(doc, yyjson_doc_get_root(schema)));
        yyjson_doc_free(schema);
    }
}

void mcp_post(const httplib::Request & req, httplib::Response & res, const std::string & version) {
    yyjson_doc * body = yyjson_read(req.body.c_str(), req.body.size(), 0);
    if (!body) {
        error(res, 400, nullptr, PARSE_ERROR, "the body is no JSON");
        return;
    }
    std::unique_ptr<yyjson_doc, void (*)(yyjson_doc *)> owner(body, yyjson_doc_free);

    yyjson_val * root = yyjson_doc_get_root(body);
    if (!yyjson_is_obj(root)) {
        error(res, 400, nullptr, INVALID_REQUEST, "the body is one JSON-RPC request or notification");
        return;
    }
    yyjson_val * method_val = yyjson_obj_get(root, "method");
    yyjson_val * id         = yyjson_obj_get(root, "id");
    if (!yyjson_is_str(method_val)) {
        error(res, 400, id, INVALID_REQUEST, "the server sends no request, so it takes no response");
        return;
    }
    const std::string method = yyjson_get_str(method_val);
    yyjson_val *      params = yyjson_obj_get(root, "params");
    yyjson_val *      meta   = yyjson_obj_get(params, "_meta");
    yyjson_val *      claim  = yyjson_obj_get(meta, PROTOCOL_VERSION_META);

    const std::string header   = req.get_header_value("MCP-Protocol-Version");
    const bool        modern   = claim != nullptr;
    const size_t      n_modern = sizeof(MODERN_VERSIONS) / sizeof(MODERN_VERSIONS[0]);

    if (modern) {
        if (!yyjson_is_str(claim)) {
            error(res, 400, id, INVALID_PARAMS, std::string(PROTOCOL_VERSION_META) + " is a string");
            return;
        }
        const std::string requested = yyjson_get_str(claim);
        if (!header.empty() && header != requested) {
            error(res, 400, id, HEADER_MISMATCH,
                  "MCP-Protocol-Version " + header + " disagrees with the envelope " + requested);
            return;
        }
        if (!listed(MODERN_VERSIONS, n_modern, requested)) {
            error(res, 400, id, UNSUPPORTED_VERSION, "unsupported protocol version " + requested,
                  [&](yyjson_mut_doc * doc, yyjson_mut_val * data) {
                      yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, data, "supported");
                      for (const char * v : MODERN_VERSIONS) {
                          yyjson_mut_arr_add_str(doc, list, v);
                      }
                      yyjson_mut_obj_add_strncpy(doc, data, "requested", requested.c_str(), requested.size());
                  });
            return;
        }
    } else if (listed(MODERN_VERSIONS, n_modern, header)) {
        error(res, 400, id, INVALID_PARAMS, "MCP-Protocol-Version " + header + " needs the envelope in params._meta");
        return;
    }

    if (!id) {
        res.status = 202;
        return;
    }

    if (modern) {
        if (header.empty()) {
            error(res, 400, id, HEADER_MISMATCH, "a request of this era carries MCP-Protocol-Version");
            return;
        }
        if (req.get_header_value("Mcp-Method") != method) {
            error(res, 400, id, HEADER_MISMATCH, "Mcp-Method disagrees with the method " + method);
            return;
        }
        if (method == "tools/call") {
            yyjson_val *      name   = yyjson_obj_get(params, "name");
            const std::string called = yyjson_is_str(name) ? yyjson_get_str(name) : "";
            if (req.get_header_value("Mcp-Name") != called) {
                error(res, 400, id, HEADER_MISMATCH, "Mcp-Name disagrees with the tool " + called);
                return;
            }
        }
    }

    const std::string server = engine_get("server name").value_or("");

    if (method == "ping") {
        result(res, id, modern, [](yyjson_mut_doc *, yyjson_mut_val *) {});
        return;
    }

    if (modern && method == "server/discover") {
        result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
            yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, out, "supportedVersions");
            for (const char * v : MODERN_VERSIONS) {
                yyjson_mut_arr_add_str(doc, list, v);
            }
            yyjson_mut_obj_add_obj(doc, yyjson_mut_obj_add_obj(doc, out, "capabilities"), "tools");
            yyjson_mut_obj_add_int(doc, out, "ttlMs", TTL_MS);
            yyjson_mut_obj_add_str(doc, out, "cacheScope", "private");
            yyjson_mut_val * m = yyjson_mut_obj_add_obj(doc, out, "_meta");
            server_info(doc, yyjson_mut_obj_add_obj(doc, m, SERVER_INFO_META), server, version);
        });
        return;
    }

    if (!modern && method == "initialize") {
        yyjson_val *      asked  = yyjson_obj_get(params, "protocolVersion");
        const std::string want   = yyjson_is_str(asked) ? yyjson_get_str(asked) : "";
        const size_t      n      = sizeof(LEGACY_VERSIONS) / sizeof(LEGACY_VERSIONS[0]);
        // a version the server does not speak gets its latest, as the protocol states
        const std::string chosen = listed(LEGACY_VERSIONS, n, want) ? want : LEGACY_VERSIONS[0];
        result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
            yyjson_mut_obj_add_strncpy(doc, out, "protocolVersion", chosen.c_str(), chosen.size());
            yyjson_mut_val * caps = yyjson_mut_obj_add_obj(doc, out, "capabilities");
            yyjson_mut_obj_add_bool(doc, yyjson_mut_obj_add_obj(doc, caps, "tools"), "listChanged", false);
            server_info(doc, yyjson_mut_obj_add_obj(doc, out, "serverInfo"), server, version);
        });
        return;
    }

    if (method == "tools/list") {
        result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
            tools_body(doc, out, server);
            if (modern) {
                yyjson_mut_obj_add_int(doc, out, "ttlMs", TTL_MS);
                yyjson_mut_obj_add_str(doc, out, "cacheScope", "private");
            }
        });
        return;
    }

    if (method == "tools/call") {
        yyjson_val *      name   = yyjson_obj_get(params, "name");
        const std::string called = yyjson_is_str(name) ? yyjson_get_str(name) : "";
        yyjson_val *      args   = yyjson_obj_get(params, "arguments");
        for (const tool & t : tools_list(server)) {
            if (called != server + "_" + t.suffix) {
                continue;
            }
            const tool_result r = t.call(args);
            fprintf(stderr, "[server] %s: %s\n", called.c_str(), r.error ? "error" : "ok");
            result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
                yyjson_mut_val * content = yyjson_mut_obj_add_arr(doc, out, "content");
                yyjson_mut_val * block   = yyjson_mut_arr_add_obj(doc, content);
                yyjson_mut_obj_add_str(doc, block, "type", "text");
                yyjson_mut_obj_add_strncpy(doc, block, "text", r.text.c_str(), r.text.size());
                yyjson_mut_obj_add_bool(doc, out, "isError", r.error);
            });
            return;
        }
        error(res, 200, id, INVALID_PARAMS, "unknown tool \"" + called + "\"");
        return;
    }

    error(res, modern ? 404 : 200, id, METHOD_NOT_FOUND, "unknown method " + method);
}
