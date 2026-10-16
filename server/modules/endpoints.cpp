// endpoints.cpp: the endpoints module: the LLM servers the agent of this
// server talks to, the twin of the endpoints module of the page: the URL, the
// protocol it speaks, the key and the header that carries it, more headers,
// the seconds it has, and how a reply is asked again

#include "engine/engine.h"
#include "lib/api.h"
#include "lib/remote.h"

// the protocol an item speaks, as its key falls back to the header of it
static std::string fallback(const engine_reader & config, const std::string & name) {
    const std::string speaks = config.get("endpoints protocol", name).value_or("");
    for (const protocol * p : api_protocols()) {
        if (speaks == p->name) {
            return p->key;
        }
    }
    return api_protocols()[0]->key;
}

static std::string validate(const engine_reader & config) {
    return remote_rules(config, "endpoints", [&](const std::string & name) { return fallback(config, name); });
}

static engine_module make_endpoints() {
    engine_key url("url", ENGINE_STRING);
    url.named = true;

    // how the endpoint speaks, a file of protocols/ by its name
    engine_key speaks("protocol", ENGINE_ENUM);
    speaks.named = true;
    for (const protocol * p : api_protocols()) {
        speaks.values.push_back(p->name);
    }
    speaks.reset = speaks.values[0];

    engine_key key("key", ENGINE_SECRET);
    key.named = true;

    // the header that carries the key, Header or Header: Scheme, the one of
    // the protocol unless set
    engine_key auth("auth-header", ENGINE_STRING);
    auth.named = true;

    // sent with every request, Name: value pairs split by ;
    engine_key headers("headers", ENGINE_STRING);
    headers.named = true;

    // seconds the endpoint may stay silent
    engine_key timeout("timeout", ENGINE_NUMBER);
    timeout.named = true;
    timeout.min   = 0.1;
    timeout.reset = "120";

    // the times a reply is asked again while the endpoint answers a status
    // that passes, 0 never, and the seconds between two of them
    engine_key retries("max-retries", ENGINE_NUMBER);
    retries.named   = true;
    retries.min     = 0;
    retries.integer = true;
    retries.reset   = "1800";

    engine_key pause("retry-pause", ENGINE_NUMBER);
    pause.named = true;
    pause.min   = 0;
    pause.reset = "2";

    engine_module m;
    m.name     = "endpoints";
    m.keys     = { url, speaks, key, auth, headers, timeout, retries, pause };
    m.validate = validate;
    return m;
}

ENGINE_MODULE(make_endpoints);
