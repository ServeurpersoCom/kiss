#pragma once
// methods.h: the methods the server answers: server/discover and initialize,
// which introduce it in each era, tools/list and tools/call

#include "httplib.h"
#include "yyjson.h"

#include <memory>
#include <string>

constexpr const char * MODERN_VERSIONS[] = { "2026-07-28" };
constexpr const char * LEGACY_VERSIONS[] = { "2025-11-25", "2025-06-18", "2025-03-26" };

void method_discover(httplib::Response & res, yyjson_val * id, const std::string & version);
void method_initialize(httplib::Response & res, yyjson_val * id, yyjson_val * params, const std::string & version);
void method_tools_list(httplib::Response & res, yyjson_val * id, bool modern);
void method_tools_call(const httplib::Request &            req,
                       httplib::Response &                 res,
                       const std::shared_ptr<yyjson_doc> & body,
                       yyjson_val *                        id,
                       bool                                modern,
                       yyjson_val *                        params);
