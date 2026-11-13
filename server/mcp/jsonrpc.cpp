// jsonrpc.cpp: the JSON-RPC answers of the MCP

#include "mcp/jsonrpc.h"

#include "lib/http.h"
#include "lib/json.h"

#include <cstdlib>

static void send(httplib::Response & res, int status, const json_mut & made) {
    res.status = status;
    res.set_content(json_write(made), JSON_TYPE);
}

static yyjson_mut_val * response(yyjson_mut_doc * doc, yyjson_val * id) {
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_val(doc, root, "id", id ? yyjson_val_mut_copy(doc, id) : yyjson_mut_null(doc));
    return root;
}

void jsonrpc_error(httplib::Response &   res,
                   int                   status,
                   yyjson_val *          id,
                   int                   code,
                   const std::string &   message,
                   const jsonrpc_build & data) {
    const json_mut   made = json_new();
    yyjson_mut_doc * doc  = made.get();
    yyjson_mut_val * root = response(doc, id);
    yyjson_mut_val * err  = yyjson_mut_obj_add_obj(doc, root, "error");
    yyjson_mut_obj_add_int(doc, err, "code", code);
    json_put(doc, err, "message", message);
    if (data) {
        data(doc, yyjson_mut_obj_add_obj(doc, err, "data"));
    }
    send(res, status, made);
}

// a result, its resultType when the era has one
static json_mut result(yyjson_val * id, const char * type, const jsonrpc_build & body) {
    const json_mut   made = json_new();
    yyjson_mut_doc * doc  = made.get();
    yyjson_mut_val * root = response(doc, id);
    yyjson_mut_val * out  = yyjson_mut_obj_add_obj(doc, root, "result");
    if (type) {
        yyjson_mut_obj_add_str(doc, out, "resultType", type);
    }
    body(doc, out);
    return made;
}

void jsonrpc_result(httplib::Response & res, yyjson_val * id, bool modern, const jsonrpc_build & body) {
    send(res, httplib::StatusCode::OK_200, result(id, modern ? "complete" : nullptr, body));
}

std::string jsonrpc_result_json(yyjson_val * id, bool modern, const jsonrpc_build & body) {
    return json_write(result(id, modern ? "complete" : nullptr, body));
}

std::string jsonrpc_input_required_json(yyjson_val * id, const jsonrpc_build & body) {
    return json_write(result(id, "input_required", body));
}
