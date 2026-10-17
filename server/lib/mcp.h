#pragma once
// mcp.h: the client of the MCP servers the agent of the server calls, the
// twin of mcp.ts of the page: Streamable HTTP in the era each server speaks,
// one session per server, kept
// with its tool list while its url, key and headers stay; a request that
// fails drops it, and the next use connects again

#include "lib/api.h"
#include "lib/conversation.h"
#include "lib/remote.h"

#include <functional>
#include <string>
#include <vector>

// a tool an MCP server serves: its server, its name, what it does, the JSON
// schema of its arguments
struct mcp_tool {
    std::string server;
    std::string name;
    std::string description;
    std::string input;
};

// what a call answered: whether it went, its text, its images
struct mcp_outcome {
    bool                            ok = false;
    std::string                     text;
    std::vector<conversation_image> images;
};

// the tools a server serves; throws why it serves none
std::vector<mcp_tool> mcp_tools(const remote & r, api_stop & stop);

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
