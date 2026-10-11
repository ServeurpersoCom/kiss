#pragma once
// tools.h: the tools the server serves over MCP, each named after the server,
// <server hostname>_<suffix>; a file in tools/ registers its source by existing,
// which gives its tools from the configuration it reads

#include "engine/engine.h"
#include "log.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

struct yyjson_val;

// the text of a result, or an image as base64 with its MIME type
struct tool_result {
    tool_result(std::string t, bool e) : text(std::move(t)), error(e) {}

    tool_result(std::string data, std::string type) : error(false), image(std::move(data)), mime(std::move(type)) {}

    std::string text;
    bool        error;
    std::string image;
    std::string mime;
    // how the call ended as the log tells it, when the tool says more than
    // done or failed, and the level of that line
    std::string outcome;
    log_level   level = LOG_INFO;
};

// called every second while a tool runs, false once the client is gone
using tool_tick = std::function<bool()>;

struct tool {
    std::string                                                           suffix;
    std::string                                                           description;
    // the JSON schema of the arguments, description first, so a model writes
    // why before what and a folded call shows it
    std::string                                                           input;
    std::function<tool_result(yyjson_val * args, const tool_tick & tick)> call;
};

// a tool name stays within what every model API takes
constexpr size_t TOOL_NAME_MAX = 64;

using tool_source = std::vector<tool> (*)(const engine_reader & config);

bool tools_register(tool_source source);

#define SERVER_TOOLS(source) static const bool server_tools_##source = tools_register(source)

// every tool the configuration serves
std::vector<tool> tools_list(const engine_reader & config);

// the arguments against the schema of the tool: no key outside its
// properties, every required key present, a required string not empty;
// the message of the first failure, empty when they hold
std::string tools_check(const tool & t, yyjson_val * args);
