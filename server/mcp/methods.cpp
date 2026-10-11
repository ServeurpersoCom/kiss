// methods.cpp: the methods the server answers

#include "mcp/methods.h"

#include "engine/engine.h"
#include "log.h"
#include "mcp/rpc.h"
#include "tools.h"

static const char * const SERVER_INFO_META = "io.modelcontextprotocol/serverInfo";

// a result the server does not keep: the tools follow the configuration, so a
// client asks again rather than caching
static const int TTL_MS = 0;

static void server_info(yyjson_mut_doc *    doc,
                        yyjson_mut_val *    info,
                        const std::string & name,
                        const std::string & version) {
    yyjson_mut_obj_add_strncpy(doc, info, "name", name.c_str(), name.size());
    yyjson_mut_obj_add_str(doc, info, "title", "KiSS server");
    yyjson_mut_obj_add_strncpy(doc, info, "version", version.c_str(), version.size());
}

// the tools and the server name of one same configuration
static std::vector<tool> listed(std::string & server) {
    std::vector<tool> tools;
    engine_with([&](const engine_reader & config) {
        server = config.get("server name").value_or("");
        tools  = tools_list(config);
    });
    return tools;
}

void method_discover(httplib::Response & res, yyjson_val * id, const std::string & version) {
    const std::string server = engine_get("server name").value_or("");
    rpc_result(res, id, true, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, out, "supportedVersions");
        for (const char * v : MODERN_VERSIONS) {
            yyjson_mut_arr_add_str(doc, list, v);
        }
        yyjson_mut_obj_add_obj(doc, yyjson_mut_obj_add_obj(doc, out, "capabilities"), "tools");
        yyjson_mut_obj_add_int(doc, out, "ttlMs", TTL_MS);
        yyjson_mut_obj_add_str(doc, out, "cacheScope", "private");
        yyjson_mut_val * meta = yyjson_mut_obj_add_obj(doc, out, "_meta");
        server_info(doc, yyjson_mut_obj_add_obj(doc, meta, SERVER_INFO_META), server, version);
    });
}

// a version the server does not speak gets its latest, as the protocol states
void method_initialize(httplib::Response & res, yyjson_val * id, yyjson_val * params, const std::string & version) {
    const std::string server = engine_get("server name").value_or("");
    yyjson_val *      asked  = yyjson_obj_get(params, "protocolVersion");
    const std::string want   = yyjson_is_str(asked) ? yyjson_get_str(asked) : "";
    std::string       chosen = LEGACY_VERSIONS[0];
    for (const char * v : LEGACY_VERSIONS) {
        if (want == v) {
            chosen = v;
        }
    }
    rpc_result(res, id, false, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_obj_add_strncpy(doc, out, "protocolVersion", chosen.c_str(), chosen.size());
        yyjson_mut_val * caps = yyjson_mut_obj_add_obj(doc, out, "capabilities");
        yyjson_mut_obj_add_bool(doc, yyjson_mut_obj_add_obj(doc, caps, "tools"), "listChanged", false);
        server_info(doc, yyjson_mut_obj_add_obj(doc, out, "serverInfo"), server, version);
    });
}

void method_tools_list(httplib::Response & res, yyjson_val * id, bool modern) {
    std::string             server;
    const std::vector<tool> tools = listed(server);
    rpc_result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, out, "tools");
        for (const tool & t : tools) {
            const std::string name = server + "_" + t.suffix;
            yyjson_mut_val *  item = yyjson_mut_arr_add_obj(doc, list);
            yyjson_mut_obj_add_strncpy(doc, item, "name", name.c_str(), name.size());
            yyjson_mut_obj_add_strncpy(doc, item, "description", t.description.c_str(), t.description.size());
            yyjson_doc * schema = yyjson_read(t.input.c_str(), t.input.size(), 0);
            yyjson_mut_obj_add_val(doc, item, "inputSchema", yyjson_val_mut_copy(doc, yyjson_doc_get_root(schema)));
            yyjson_doc_free(schema);
        }
        if (modern) {
            yyjson_mut_obj_add_int(doc, out, "ttlMs", TTL_MS);
            yyjson_mut_obj_add_str(doc, out, "cacheScope", "private");
        }
    });
}

void method_tools_call(httplib::Response & res, yyjson_val * id, bool modern, yyjson_val * params) {
    yyjson_val *      name   = yyjson_obj_get(params, "name");
    const std::string called = yyjson_is_str(name) ? yyjson_get_str(name) : "";
    std::string       server;
    for (const tool & t : listed(server)) {
        if (called != server + "_" + t.suffix) {
            continue;
        }
        const tool_result r = t.call(yyjson_obj_get(params, "arguments"));
        log_verbose("Mcp", "Call %s, %s", called.c_str(), r.error ? "error" : "done");
        rpc_result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
            yyjson_mut_val * content = yyjson_mut_obj_add_arr(doc, out, "content");
            yyjson_mut_val * block   = yyjson_mut_arr_add_obj(doc, content);
            yyjson_mut_obj_add_str(doc, block, "type", "text");
            yyjson_mut_obj_add_strncpy(doc, block, "text", r.text.c_str(), r.text.size());
            yyjson_mut_obj_add_bool(doc, out, "isError", r.error);
        });
        return;
    }
    rpc_error(res, 200, id, RPC_INVALID_PARAMS, "unknown tool \"" + called + "\"");
}
