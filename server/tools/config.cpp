// config.cpp: the config tool, a batch of CLI lines on the engine, as the
// config tool of KiSS runs them on the page

#include "engine/text.h"
#include "tools.h"
#include "yyjson.h"

// a batch that writes a secret without its value asks the user to type it,
// telling where each one goes, then runs again with what they typed
static tool_result call(yyjson_val * args, const engine_typed & typed, const tool_tick &) {
    yyjson_val *        lines = yyjson_obj_get(args, "lines");
    const engine_result r = engine_run(std::string(yyjson_get_str(lines), yyjson_get_len(lines)), ENGINE_MCP, typed);
    if (!r.asks.empty()) {
        words_t keys, told;
        for (const engine_ask & a : r.asks) {
            keys.push_back(a.key);
            told.push_back(a.to.empty() ? a.key : a.key + " sent to " + a.to);
        }
        tool_result asked(
            "Type " + join(told, ", ") + " for the KiSS server " + engine_get("server hostname").value_or(""), false);
        asked.asks    = keys;
        asked.outcome = "asks the user for " + join(keys, ", ");
        return asked;
    }
    return { r.text, !r.ok };
}

static std::vector<tool> config_tool(const engine_reader & config) {
    tool t;
    t.suffix      = "config";
    t.description = "Runs lines of the CLI of the KiSS server " + config.get("server hostname").value_or("") + " on " +
                    engine_platform() +
                    ", one command per line, as one batch that applies whole or not at all, the way Cisco IOS "
                    "reads them: set <module> <key> [item] <value>, no <module> <key> [item], show <module>, "
                    "show running-config [all], show startup-config, show version, show logging, "
                    "copy running-config startup-config or write memory. "
                    "A secret takes no value here: set <module> <key> [item] alone asks the user to type it, out of "
                    "your sight.";
    t.input = R"({"type":"object","properties":{)"
              R"("description":{"type":"string","description":"Why I'm configuring the server, written first"},)"
              R"("lines":{"type":"string","description":"CLI lines, one per line"}},)"
              R"("required":["description","lines"],"additionalProperties":false})";
    t.call  = call;
    return { t };
}

SERVER_TOOLS(config_tool);
