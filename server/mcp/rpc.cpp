// rpc.cpp: the JSON-RPC answers of the MCP

#include "mcp/rpc.h"

#include <cstdlib>

static std::string text(yyjson_mut_doc * doc) {
    size_t            len  = 0;
    char *            json = yyjson_mut_write(doc, 0, &len);
    const std::string out  = json ? std::string(json, len) : "{}";
    free(json);
    yyjson_mut_doc_free(doc);
    return out;
}

static void send(httplib::Response & res, int status, yyjson_mut_doc * doc) {
    res.status = status;
    res.set_content(text(doc), "application/json");
}

static yyjson_mut_val * response(yyjson_mut_doc * doc, yyjson_val * id) {
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_val(doc, root, "id", id ? yyjson_val_mut_copy(doc, id) : yyjson_mut_null(doc));
    return root;
}

void rpc_error(httplib::Response & res,
               int                 status,
               yyjson_val *        id,
               int                 code,
               const std::string & message,
               const rpc_build &   data) {
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

static yyjson_mut_doc * result(yyjson_val * id, bool modern, const rpc_build & body) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val * root = response(doc, id);
    yyjson_mut_val * out  = yyjson_mut_obj_add_obj(doc, root, "result");
    if (modern) {
        yyjson_mut_obj_add_str(doc, out, "resultType", "complete");
    }
    body(doc, out);
    return doc;
}

void rpc_result(httplib::Response & res, yyjson_val * id, bool modern, const rpc_build & body) {
    send(res, 200, result(id, modern, body));
}

std::string rpc_result_json(yyjson_val * id, bool modern, const rpc_build & body) {
    return text(result(id, modern, body));
}
