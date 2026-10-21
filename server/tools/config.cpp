// config.cpp: the config tool, a batch of CLI lines on the engine, as
// the config tool of the page runs them there

#include "engine/schema.h"
#include "engine/text.h"
#include "lib/prompts.h"
#include "lib/tools.h"
#include "yyjson.h"

// a batch that writes a secret without its value asks the user to type it,
// telling where each one goes, one that gives an item a new url asks the user
// to let it, the item reached at <module>/<item> of this server, and one that
// writes or erases a configuration asks their yes; it runs again with what
// they answered
tool_outcome tools_config(yyjson_val * args, const engine_typed & typed, bool conversation) {
    yyjson_val *        lines = yyjson_obj_get(args, "lines");
    const engine_result r =
        engine_run(std::string(yyjson_get_str(lines), yyjson_get_len(lines)), ENGINE_MCP, typed, conversation);
    if (!r.asks.empty()) {
        words_t keys, told;
        for (const engine_ask & a : r.asks) {
            const auto parts = unstore(a.key);
            keys.push_back(a.key);
            if (!a.question.empty()) {
                told.push_back(a.question);
            } else if (a.yes) {
                const std::string module = parts.first.substr(0, parts.first.find(' '));
                const std::string with   = a.along.empty() ? "" : ", " + join(a.along, ", ") + " with it";
                told.push_back(module == "proxies" ? "let proxy/" + parts.second + " proxy " + a.to + with :
                                                     "let " + module + " " + parts.second + " reach " + a.to + with);
            } else {
                told.push_back("type " + (a.to.empty() ? a.key : a.key + " sent to " + a.to));
            }
        }
        std::string text = join(told, ", ") + " for the KiSS server " + engine_get("server hostname").value_or("");
        text[0]          = (char) toupper((unsigned char) text[0]);
        tool_outcome asked(true, text);
        asked.asks    = r.asks;
        asked.outcome = "asks the user for " + join(keys, ", ");
        return asked;
    }
    tool_outcome out(r.ok, r.text);
    out.compact = r.compact;
    return out;
}

static std::vector<tool> config_tool(const engine_reader & config) {
    words_t modules;
    for (const engine_module & m : schema_modules()) {
        words_t keys;
        for (const engine_key & k : m.keys) {
            keys.push_back(k.name);
        }
        modules.push_back(m.name + " (" + join(keys, " ") + "): " + prompts_module(m.name));
    }
    tool t;
    t.suffix      = TOOL_CONFIG;
    t.description = prompts_description("config", {
                                                      { "hostname", config.get("server hostname").value_or("") },
                                                      { "platform", engine_platform() },
                                                      { "modules", join(modules, "; ") }
    });
    t.input       = prompts_input("config");
    t.run         = [](yyjson_val * args, const engine_typed & typed, const tool_tick &) {
        return tools_config(args, typed, false);
    };
    return { t };
}

SERVER_TOOLS(config_tool);
