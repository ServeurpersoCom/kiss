// chat.cpp: the chat module: the model the agent of this server answers with
// and its system prompt, the twin of the chat module of the page

#include "engine/engine.h"
#include "engine/text.h"

// a model set names an endpoint of the configuration, the model itself left
// to the endpoint
static std::string validate(const engine_reader & config) {
    const std::string value = config.get("chat model").value_or("");
    if (value.empty()) {
        return "";
    }
    for (const std::string & name : config.names("endpoints")) {
        if (starts_with(value, name + "/")) {
            return "";
        }
    }
    return "chat model " + value + " names no endpoint";
}

static engine_module make_chat() {
    // endpoint/model; empty: the one model of the one endpoint
    engine_key model("model", ENGINE_STRING);
    model.reset = "";

    // sent first in every request, none when empty
    engine_key system("system-prompt", ENGINE_STRING);
    system.reset = "";

    engine_module m;
    m.name     = "chat";
    m.keys     = { model, system };
    m.validate = validate;
    return m;
}

ENGINE_MODULE(make_chat);
