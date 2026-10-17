#pragma once
// tools.h: the tools the server serves over MCP, each named after the server
// and its role, <server hostname>_server_<suffix>; a file in tools/ registers its source by existing,
// which gives its tools from the configuration it reads

#include "engine/engine.h"
#include "lib/log.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

struct yyjson_val;
struct yyjson_mut_doc;
struct yyjson_mut_val;

// what a call answers: whether it went, its text, and an image as base64 with
// its MIME type
struct tool_outcome {
    tool_outcome(bool o, std::string t) : ok(o), text(std::move(t)) {}

    tool_outcome(std::string data, std::string type) : ok(true), image(std::move(data)), mime(std::move(type)) {}

    bool                    ok;
    std::string             text;
    std::string             image;
    std::string             mime;
    // how the call ended as the log tells it, when the tool says more than
    // done or failed, and the level of that line
    std::string             outcome;
    log_level               level = LOG_INFO;
    // what the user is to answer before the call is asked again with it, its
    // text telling what
    std::vector<engine_ask> asks;
};

// called every second while a tool runs, false once the client is gone
using tool_tick = std::function<bool()>;

struct tool {
    std::string                                                                                        suffix;
    std::string                                                                                        description;
    // the JSON schema of the arguments, description first, so a model writes
    // why before what and a folded call shows it
    std::string                                                                                        input;
    // typed holds the secrets the user typed when the call is asked again
    std::function<tool_outcome(yyjson_val * args, const engine_typed & typed, const tool_tick & tick)> run;
};

// a tool name stays within what every model API takes
constexpr size_t TOOL_NAME_MAX = 64;

// the name a client sees a tool of this server by, its role between the
// hostname and the tool: forge_server_bash; the agent of this server names it
// bare, bash, as a router names its own interfaces
std::string tools_served(const std::string & hostname, const tool & t);

using tool_source = std::vector<tool> (*)(const engine_reader & config);

bool tools_register(tool_source source);

#define SERVER_TOOLS(source) static const bool server_tools_##source = tools_register(source)

// every tool the configuration serves
std::vector<tool> tools_list(const engine_reader & config);

// the arguments against the schema of the tool, as JSON Schema reads them: no
// key outside its properties, every required key present, a string of a
// string property, never shorter than its minLength; the message of the first
// failure, empty when they hold
std::string tools_check(const tool & t, yyjson_val * args);

// the form a call that asks the user shows them, as an MCP input request
// fills input: elicitation/create in form mode, its message the text of the
// call, a text field per secret to type and a yes field per url to let, by
// stored key
void tools_form(yyjson_mut_doc * doc, yyjson_mut_val * input, const tool_outcome & asked);

// what the user typed and let in their response to that form, by stored key,
// a yes as true; false unless they accepted it
bool tools_typed(yyjson_val * response, engine_typed & typed);
