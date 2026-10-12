#pragma once
// jsonrpc.h: the JSON-RPC answers of the MCP: a result or an error to a request

#include "httplib.h"
#include "yyjson.h"

#include <functional>
#include <string>

constexpr int JSONRPC_PARSE_ERROR         = -32700;
constexpr int JSONRPC_INVALID_REQUEST     = -32600;
constexpr int JSONRPC_METHOD_NOT_FOUND    = -32601;
constexpr int JSONRPC_INVALID_PARAMS      = -32602;
constexpr int JSONRPC_HEADER_MISMATCH     = -32020;
constexpr int JSONRPC_UNSUPPORTED_VERSION = -32022;

using jsonrpc_build = std::function<void(yyjson_mut_doc *, yyjson_mut_val *)>;

// an error to the request id, null when the request carries none, with its
// data when there is any
void jsonrpc_error(httplib::Response &   res,
                   int                   status,
                   yyjson_val *          id,
                   int                   code,
                   const std::string &   message,
                   const jsonrpc_build & data = nullptr);

// a result; one of the 2026-07-28 era says it is complete
void jsonrpc_result(httplib::Response & res, yyjson_val * id, bool modern, const jsonrpc_build & body);

// the same result as JSON text, for an answer sent as an SSE event
std::string jsonrpc_result_json(yyjson_val * id, bool modern, const jsonrpc_build & body);

// a result of the 2026-07-28 era that asks the client for input before the
// request is asked again, as JSON text
std::string jsonrpc_input_required_json(yyjson_val * id, const jsonrpc_build & body);
