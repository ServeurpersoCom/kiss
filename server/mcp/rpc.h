#pragma once
// rpc.h: the JSON-RPC answers of the MCP: a result or an error to a request

#include "httplib.h"
#include "yyjson.h"

#include <functional>
#include <string>

constexpr int RPC_PARSE_ERROR         = -32700;
constexpr int RPC_INVALID_REQUEST     = -32600;
constexpr int RPC_METHOD_NOT_FOUND    = -32601;
constexpr int RPC_INVALID_PARAMS      = -32602;
constexpr int RPC_HEADER_MISMATCH     = -32020;
constexpr int RPC_UNSUPPORTED_VERSION = -32022;

using rpc_build = std::function<void(yyjson_mut_doc *, yyjson_mut_val *)>;

// an error to the request id, null when the request carries none, with its
// data when there is any
void rpc_error(httplib::Response & res,
               int                 status,
               yyjson_val *        id,
               int                 code,
               const std::string & message,
               const rpc_build &   data = nullptr);

// a result; one of the 2026-07-28 era says it is complete, the server never
// asking the client for input in the middle of a request
void rpc_result(httplib::Response & res, yyjson_val * id, bool modern, const rpc_build & body);
