// mcp.cpp: the MCP of the server over Streamable HTTP, stateless, in both eras
//
// A request that carries the 2026-07-28 envelope in _meta speaks that era:
// its MCP-Protocol-Version and Mcp-Method headers must match the body, and
// server/discover answers. A request without it speaks the 2025 era:
// initialize answers without Mcp-Session-Id, since the server keeps no state
// between requests. Notifications get 202.

#include "mcp/mcp.h"

#include "lib/log.h"
#include "mcp/jsonrpc.h"
#include "mcp/methods.h"

#include <memory>

static const char * const PROTOCOL_VERSION_META = "io.modelcontextprotocol/protocolVersion";
static const char * const CLIENT_INFO_META      = "io.modelcontextprotocol/clientInfo";

static bool modern_version(const std::string & v) {
    for (const char * m : MODERN_VERSIONS) {
        if (v == m) {
            return true;
        }
    }
    return false;
}

// the checks of a request of the 2026-07-28 era, its headers against its body;
// false once an error is answered
static bool modern_request(const httplib::Request & req,
                           httplib::Response &      res,
                           yyjson_val *             id,
                           const std::string &      method,
                           yyjson_val *             params) {
    if (req.get_header_value("MCP-Protocol-Version").empty()) {
        jsonrpc_error(res, 400, id, JSONRPC_HEADER_MISMATCH, "a request of this era carries MCP-Protocol-Version");
        return false;
    }
    if (req.get_header_value("Mcp-Method") != method) {
        jsonrpc_error(res, 400, id, JSONRPC_HEADER_MISMATCH, "Mcp-Method disagrees with the method " + method);
        return false;
    }
    if (method == "tools/call") {
        yyjson_val *      name   = yyjson_obj_get(params, "name");
        const std::string called = yyjson_is_str(name) ? yyjson_get_str(name) : "";
        if (req.get_header_value("Mcp-Name") != called) {
            jsonrpc_error(res, 400, id, JSONRPC_HEADER_MISMATCH, "Mcp-Name disagrees with the tool " + called);
            return false;
        }
    }
    return true;
}

// a client that opens its connection, as its clientInfo names it
static void connected(const httplib::Request & req, yyjson_val * info) {
    yyjson_val * name    = yyjson_obj_get(info, "name");
    yyjson_val * version = yyjson_obj_get(info, "version");
    log_info("MCP", "Client %s %s connects from %s", yyjson_is_str(name) ? yyjson_get_str(name) : "unnamed",
             yyjson_is_str(version) ? yyjson_get_str(version) : "", req.remote_addr.c_str());
}

void mcp_post(const httplib::Request & req, httplib::Response & res, const std::string & version) {
    const std::shared_ptr<yyjson_doc> body(yyjson_read(req.body.c_str(), req.body.size(), 0), yyjson_doc_free);
    if (!body) {
        jsonrpc_error(res, 400, nullptr, JSONRPC_PARSE_ERROR, "the body is no JSON");
        return;
    }

    yyjson_val * root = yyjson_doc_get_root(body.get());
    if (!yyjson_is_obj(root)) {
        jsonrpc_error(res, 400, nullptr, JSONRPC_INVALID_REQUEST, "the body is one JSON-RPC request or notification");
        return;
    }
    yyjson_val * method_val = yyjson_obj_get(root, "method");
    yyjson_val * id         = yyjson_obj_get(root, "id");
    if (!yyjson_is_str(method_val)) {
        jsonrpc_error(res, 400, id, JSONRPC_INVALID_REQUEST, "the server sends no request, so it takes no response");
        return;
    }
    const std::string method = yyjson_get_str(method_val);
    yyjson_val *      params = yyjson_obj_get(root, "params");
    yyjson_val *      claim  = yyjson_obj_get(yyjson_obj_get(params, "_meta"), PROTOCOL_VERSION_META);
    const std::string header = req.get_header_value("MCP-Protocol-Version");
    const bool        modern = claim != nullptr;
    const std::string era    = yyjson_is_str(claim) ? yyjson_get_str(claim) : header.empty() ? "2025" : header;
    log_debug("MCP", "%s from %s, %s", method.c_str(), req.remote_addr.c_str(), era.c_str());

    if (modern) {
        if (!yyjson_is_str(claim)) {
            jsonrpc_error(res, 400, id, JSONRPC_INVALID_PARAMS, std::string(PROTOCOL_VERSION_META) + " is a string");
            return;
        }
        const std::string requested = yyjson_get_str(claim);
        if (!header.empty() && header != requested) {
            jsonrpc_error(res, 400, id, JSONRPC_HEADER_MISMATCH,
                          "MCP-Protocol-Version " + header + " disagrees with the envelope " + requested);
            return;
        }
        if (!modern_version(requested)) {
            jsonrpc_error(res, 400, id, JSONRPC_UNSUPPORTED_VERSION, "unsupported protocol version " + requested,
                          [&](yyjson_mut_doc * doc, yyjson_mut_val * data) {
                              yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, data, "supported");
                              for (const char * v : MODERN_VERSIONS) {
                                  yyjson_mut_arr_add_str(doc, list, v);
                              }
                              yyjson_mut_obj_add_strncpy(doc, data, "requested", requested.c_str(), requested.size());
                          });
            return;
        }
    } else if (modern_version(header)) {
        jsonrpc_error(res, 400, id, JSONRPC_INVALID_PARAMS,
                      "MCP-Protocol-Version " + header + " needs the envelope in params._meta");
        return;
    }

    if (!id) {
        res.status = 202;
        return;
    }
    if (modern && !modern_request(req, res, id, method, params)) {
        return;
    }

    if (method == "server/discover" || method == "initialize") {
        connected(req, modern ? yyjson_obj_get(yyjson_obj_get(params, "_meta"), CLIENT_INFO_META) :
                                yyjson_obj_get(params, "clientInfo"));
    }
    if (method == "ping") {
        jsonrpc_result(res, id, modern, [](yyjson_mut_doc *, yyjson_mut_val *) {});
    } else if (modern && method == "server/discover") {
        method_discover(res, id, version);
    } else if (!modern && method == "initialize") {
        method_initialize(res, id, params, version);
    } else if (method == "tools/list") {
        method_tools_list(res, id, modern);
    } else if (method == "tools/call") {
        method_tools_call(req, res, body, id, modern, params);
    } else if (method == "resources/list") {
        method_resources_list(res, id, modern);
    } else {
        jsonrpc_error(res, modern ? 404 : 200, id, JSONRPC_METHOD_NOT_FOUND, "unknown method " + method);
    }
}
