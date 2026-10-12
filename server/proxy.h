#pragma once
// proxy.h: the relay of the MCP servers the mcp module names: a request to
// mcp/<name> goes to the URL of that server as a new request of the same
// method and body, its stream coming back as it arrives

#include "httplib.h"

#include <string>

// the request relayed to the route it names, the answer streamed back
void proxy(const httplib::Request & req, httplib::Response & res, const std::string & route);
