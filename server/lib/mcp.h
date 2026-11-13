#pragma once
// mcp.h: the client of the MCP servers the agent of the server calls, the
// twin of mcp.ts of the page: Streamable HTTP in the era each server speaks,
// one session per server, kept
// with its tool list while its url, key and headers stay; a request that
// fails closes it, its tool list kept, and the next use connects again

#include "engine/text.h"
#include "lib/api.h"
#include "lib/conversation.h"
#include "lib/remote.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

// a tool an MCP server serves: its server, its name, what it does, the JSON
// schema of its arguments
struct mcp_tool {
    std::string server;
    std::string name;
    std::string description;
    std::string input;
    // the name the model sees it by, its own or prefixed by its server
    std::string served;
    // the item of tools that sets it, <server>/<name>, so a setting stays with
    // its server whatever name the model sees
    std::string item;
};

// what a call answered: whether it went, its text, its images
struct mcp_outcome {
    bool                            ok = false;
    std::string                     text;
    std::vector<conversation_image> images;
};

// the item of tools that sets a tool of a server: the server, then the name
// it gives the tool, exa/search
std::string mcp_item(const std::string & server, const std::string & name);

// the name of a tool of a server prefixed by its server
std::string mcp_prefixed(const std::string & server, const std::string & name);

// the name the model sees a tool of a server by, the names before it taken:
// its own, or prefixed once one before holds it, which it takes in turn;
// none once that one is taken too, or is no name a model API takes: letters,
// digits, _ and -, TOOL_NAME_MAX at most
std::string mcp_unique(std::set<std::string> & taken, const std::string & server, const std::string & name);

// the tools a server serves; throws why it serves none
std::vector<mcp_tool> mcp_tools(const remote & r, api_stop & stop);

// the tools a server listed last while its url, key and headers stay, none
// from a server never listed
std::vector<mcp_tool> mcp_kept(const remote & r);

// the request a server sends for the user to answer, and the one mode of it
// KiSS answers; what the response of the user says, with its content or without
constexpr const char * MCP_ELICITATION = "elicitation/create";
constexpr const char * MCP_FORM_MODE   = "form";
constexpr const char * MCP_ACCEPT      = "accept";
constexpr const char * MCP_CANCEL      = "cancel";

// forgets every server the configuration no longer names, its tools with it
void mcp_forget(const words_t & named);

// what the user answers to an input a server asks for before a call goes on,
// the input as JSON, a form whose fields are text or yes: their response as
// JSON, {"action":"accept","content":{...}} or {"action":"cancel"}
using mcp_answer = std::function<std::string(const std::string & input)>;

// a call of a tool of a server, its arguments a JSON object, answer giving
// what the user answers each time the server asks them; throws why it did not
// answer
mcp_outcome mcp_call(const remote &      r,
                     const std::string & name,
                     const std::string & args,
                     api_stop &          stop,
                     const mcp_answer &  answer);
