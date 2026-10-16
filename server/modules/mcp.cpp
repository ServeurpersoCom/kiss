// mcp.cpp: the mcp module: the MCP servers whose tools the agent of this
// server calls, the twin of the mcp module of the page: the Streamable HTTP
// url, the key and the header that carries it, more headers, and the seconds
// a call may take

#include "engine/engine.h"
#include "engine/keychain.h"
#include "lib/remote.h"

static std::string validate(const engine_reader & config) {
    return remote_rules(config, "mcp", [](const std::string &) { return std::string(BEARER); });
}

static engine_module make_mcp() {
    engine_key url("url", ENGINE_STRING);
    url.named = true;

    engine_key key("key", ENGINE_SECRET);
    key.named = true;

    // the header that carries the key, Header or Header: Scheme
    engine_key auth("auth-header", ENGINE_STRING);
    auth.named = true;
    auth.reset = BEARER;

    // sent with every request, Name: value pairs split by ;
    engine_key headers("headers", ENGINE_STRING);
    headers.named = true;

    // seconds a call of a tool may take
    engine_key timeout("timeout", ENGINE_NUMBER);
    timeout.named = true;
    timeout.min   = 0.1;
    timeout.reset = "310";

    engine_module m;
    m.name     = "mcp";
    m.keys     = { url, key, auth, headers, timeout };
    m.validate = validate;
    return m;
}

ENGINE_MODULE(make_mcp);
