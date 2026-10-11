#pragma once
// tools.h: the tools the server serves over MCP, each named after the server,
// <server name>_<suffix>; a file in tools/ registers its source by existing,
// which gives its tools from the configuration it reads

#include "engine/engine.h"

#include <functional>
#include <string>
#include <vector>

struct yyjson_val;

struct tool_result {
    std::string text;
    bool        error;
};

// called every second while a tool runs, false once the client is gone
using tool_tick = std::function<bool()>;

struct tool {
    std::string                                                           suffix;
    std::string                                                           description;
    // the JSON schema of the arguments
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
