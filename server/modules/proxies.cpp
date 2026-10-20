// proxies.cpp: the proxies module: the servers this server proxies, MCP servers and
// LLM endpoints alike, each at proxy/<name> of this server: the URL of the
// server, the key sent to it and the header that carries it, more headers, and
// the seconds the server may stay silent

#include "engine/engine.h"
#include "engine/keychain.h"
#include "engine/text.h"
#include "lib/log.h"
#include "lib/remote.h"

// the seconds a proxied server may stay silent, ten less than the 310 the page
// waits on a call of an MCP server
static const char * const PROXY_TIMEOUT = "300";

// a route is a path of this server, so its name is a word a path takes
static bool route_word(const std::string & s) {
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return !s.empty();
}

static std::string validate(const engine_reader & config) {
    for (const std::string & route : config.names("proxies")) {
        if (!route_word(route)) {
            return "proxies " + route + ": a route name takes a-z, 0-9, _ and - only, as a path does";
        }
    }
    return remote_rules(config, "proxies", [](const std::string &) { return std::string(BEARER); });
}

static void apply(const engine_reader & config) {
    for (const std::string & route : config.names("proxies")) {
        log_info("Proxy", "Route proxy/%s proxies %s", route.c_str(),
                 config.get("proxies url", route).value_or("").c_str());
    }
}

// where a client reaches a route
static words_t notes(const engine_reader & config, const std::string & route) {
    return { comment("proxy/" + route + " proxies " + config.get("proxies url", route).value_or("")) };
}

static engine_module make_proxies() {
    engine_key url("url", ENGINE_STRING);
    url.named = true;

    engine_key key("key", ENGINE_SECRET);
    key.named = true;

    // the header that carries the key, Header or Header: Scheme
    engine_key auth("auth-header", ENGINE_STRING);
    auth.named = true;
    auth.reset = BEARER;

    engine_key headers("headers", ENGINE_STRING);
    headers.named = true;

    engine_key timeout("timeout", ENGINE_NUMBER);
    timeout.named   = true;
    timeout.min     = 1;
    timeout.integer = true;
    timeout.reset   = PROXY_TIMEOUT;

    engine_module m;
    m.name = "proxies";
    m.keys.push_back(url);
    m.keys.push_back(key);
    m.keys.push_back(auth);
    m.keys.push_back(headers);
    m.keys.push_back(timeout);
    m.validate = validate;
    m.apply    = apply;
    m.notes    = notes;
    return m;
}

ENGINE_MODULE(make_proxies);
