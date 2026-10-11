#pragma once
// tools.h: the tools the server serves over MCP, each named after the server:
// <server name>_<suffix>

#include <string>
#include <vector>

struct yyjson_val;

struct tool_result {
    std::string text;
    bool        error;
};

struct tool {
    std::string suffix;
    std::string description;
    // the JSON schema of the arguments
    std::string input;
    tool_result (*call)(yyjson_val * args);
};

// a tool name stays within what every model API takes
static const size_t TOOL_NAME_MAX = 64;

// the tools the running configuration serves
std::vector<tool> tools_list(const std::string & server);

// the longest suffix a tool takes, which bounds the server name
size_t tools_suffix_max();
