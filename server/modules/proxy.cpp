// proxy.cpp: the proxy module: the servers this server proxies, MCP servers and
// LLM endpoints alike, each at proxy/<name> of this server: the URL of the
// server, the key sent to it and the header that carries it, more headers, and
// the seconds the server may stay silent

#include "engine/engine.h"
#include "engine/keychain.h"
#include "engine/text.h"
#include "log.h"

// the seconds a proxied server may stay silent, ten less than the 310 the page
// waits on a call of an MCP server
static const char * const PROXY_TIMEOUT = "300";

// the headers the proxy writes itself, which a route never sets
static const char * const OWN_HEADERS[] = { "host", "content-length", "transfer-encoding", "connection" };

// a route is a path of this server, so its name is a word a path takes
static bool route_word(const std::string & s) {
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return !s.empty();
}

// why the headers of a route do not read, Name: value; Name: value, none of
// them the header of its key, empty when they do
static std::string header_problem(const std::string & headers, const std::string & keyed) {
    for (const std::string & pair : split(headers, ';')) {
        const std::string h     = trim(pair);
        const size_t      colon = h.find(':');
        if (h.empty()) {
            continue;
        }
        if (colon == std::string::npos || trim(h.substr(0, colon)).empty()) {
            return "\"" + h + "\" is no Name: value";
        }
        const std::string name = lower(trim(h.substr(0, colon)));
        for (const char * own : OWN_HEADERS) {
            if (name == own) {
                return name + " is written by the proxy";
            }
        }
        if (name == lower(keyed)) {
            return trim(h.substr(0, colon)) + " is the header proxy auth writes";
        }
    }
    return "";
}

static std::string validate(const engine_reader & config) {
    for (const std::string & route : config.names("proxy")) {
        if (!route_word(route)) {
            return "proxy " + route + ": a route name takes a-z, 0-9, _ and - only, as a path does";
        }
        const auto url = config.get("proxy url", route);
        if (!url) {
            return "proxy url " + route + " is not set";
        }
        if (!starts_with(*url, "http://") && !starts_with(*url, "https://")) {
            return "proxy url " + route + " " + *url + " opens with http:// or https://";
        }
        const std::string auth = config.get("proxy auth", route).value_or(BEARER);
        std::string       keyed, scheme;
        if (!placement(auth, keyed, scheme)) {
            return "proxy auth " + route + ": \"" + auth + "\" is not Header or Header: Scheme";
        }
        const std::string problem = header_problem(config.get("proxy headers", route).value_or(""), keyed);
        if (!problem.empty()) {
            return "proxy headers " + route + ": " + problem;
        }
    }
    return "";
}

static void apply(const engine_reader & config) {
    for (const std::string & route : config.names("proxy")) {
        log_info("Proxy", "Route proxy/%s proxies %s", route.c_str(),
                 config.get("proxy url", route).value_or("").c_str());
    }
}

// where a client reaches a route
static words_t notes(const engine_reader & config, const std::string & route) {
    return { comment("proxy/" + route + " proxies " + config.get("proxy url", route).value_or("")) };
}

static engine_module make_proxy() {
    engine_key url("url", ENGINE_STRING);
    url.named = true;

    engine_key key("key", ENGINE_SECRET);
    key.named = true;

    // the header that carries the key, Header or Header: Scheme
    engine_key auth("auth", ENGINE_STRING);
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
    m.name = "proxy";
    m.role =
        "the servers this server proxies, MCP servers and LLM endpoints alike, each at proxy/<name> beside its own "
        "mcp, its key going to its url alone, in the header its auth names, Header or Header: Scheme, and every secret "
        "of this server filtered from the answer; a client reaches one there with the "
        "key of this server, the page by set mcp proxy or set endpoints proxy <name> <this server>, and a route opens "
        "nothing before a client does";
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

ENGINE_MODULE(make_proxy);
