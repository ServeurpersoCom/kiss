#pragma once
// mcp.h: the MCP of the server over Streamable HTTP, stateless, in both eras

#include "httplib.h"

#include <string>

// answers one POST /mcp: a request of the 2026-07-28 era, which carries its
// envelope in _meta, or of the 2025 era, initialize answered without session
void mcp_post(const httplib::Request & req, httplib::Response & res, const std::string & version);
