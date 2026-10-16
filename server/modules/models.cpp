// models.cpp: the models module: every setting of a model, the model named as
// the item endpoint/model as chat model takes it, the parameters of an
// OpenAI compatible request under their own names, each sent only when set;
// the twin of the models module of the page

#include "engine/engine.h"
#include "engine/text.h"

// every model is one of an endpoint
static std::string validate(const engine_reader & config) {
    for (const std::string & item : config.names("models")) {
        bool named = false;
        for (const std::string & name : config.names("endpoints")) {
            named = named || starts_with(item, name + "/");
        }
        if (!named) {
            return "models " + item + " names no endpoint -> no models " + item;
        }
    }
    return "";
}

static engine_key number(const char * name, double min, std::optional<double> max, bool integer) {
    engine_key k(name, ENGINE_NUMBER);
    k.named   = true;
    k.min     = min;
    k.max     = max;
    k.integer = integer;
    return k;
}

static engine_module make_models() {
    engine_key seed("seed", ENGINE_NUMBER);
    seed.named   = true;
    seed.integer = true;

    // as the template of the model reads it: low, medium, high or its own
    engine_key effort("reasoning_effort", ENGINE_STRING);
    effort.named = true;

    engine_module m;
    m.name     = "models";
    m.keys     = { number("temperature", 0, std::nullopt, false),
                   number("top_p", 0, 1, false),
                   number("top_k", 0, std::nullopt, true),
                   number("min_p", 0, 1, false),
                   number("max_tokens", 1, std::nullopt, true),
                   number("presence_penalty", -2, 2, false),
                   number("frequency_penalty", -2, 2, false),
                   seed,
                   effort };
    m.validate = validate;
    return m;
}

ENGINE_MODULE(make_models);
