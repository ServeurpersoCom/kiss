// config.cpp: the config tool, a batch of CLI lines on the engine, as
// client_config runs them on the page

#include "engine/schema.h"
#include "engine/text.h"
#include "tools.h"
#include "yyjson.h"

// a batch that writes a secret without its value asks the user to type it,
// telling where each one goes, and one that gives an item a new url asks the
// user to let it, the item reached at <module>/<item> of this server; it runs
// again with what they answered
static tool_result call(yyjson_val * args, const engine_typed & typed, const tool_tick &) {
    yyjson_val *        lines = yyjson_obj_get(args, "lines");
    const engine_result r = engine_run(std::string(yyjson_get_str(lines), yyjson_get_len(lines)), ENGINE_MCP, typed);
    if (!r.asks.empty()) {
        words_t keys, told;
        for (const engine_ask & a : r.asks) {
            const auto parts = unstore(a.key);
            keys.push_back(a.key);
            if (a.yes) {
                told.push_back("let " + parts.first.substr(0, parts.first.find(' ')) + "/" + parts.second + " proxy " +
                               a.to + (a.along.empty() ? "" : ", " + join(a.along, ", ") + " with it"));
            } else {
                told.push_back("type " + (a.to.empty() ? a.key : a.key + " sent to " + a.to));
            }
        }
        std::string text = join(told, ", ") + " for the KiSS server " + engine_get("server hostname").value_or("");
        text[0]          = (char) toupper((unsigned char) text[0]);
        tool_result asked(text, false);
        asked.asks    = r.asks;
        asked.outcome = "asks the user for " + join(keys, ", ");
        return asked;
    }
    return { r.text, !r.ok };
}

static std::vector<tool> config_tool(const engine_reader & config) {
    words_t modules;
    for (const engine_module & m : schema_modules()) {
        words_t keys;
        for (const engine_key & k : m.keys) {
            keys.push_back(k.name);
        }
        modules.push_back(m.name + " (" + join(keys, " ") + "): " + m.role);
    }
    tool t;
    t.suffix      = "config";
    t.description = "Runs lines of the CLI of the KiSS server " + config.get("server hostname").value_or("") + " on " +
                    engine_platform() +
                    ", one command per line, as one batch that applies whole or not at all, the way Cisco IOS "
                    "reads them: set <module> <key> [item] <value>, no <module> <key> [item], show <module>, "
                    "show running-config [all], show startup-config, show version, show logging, "
                    "copy running-config startup-config or write memory. Its modules, with their keys: " +
                    join(modules, "; ") +
                    ". Every default fits as it is, so set only what differs. A secret never shows: show writes "
                    "! <module> <key> [item] is set for one that is set, and no line for one that is not. To set a "
                    "secret, write its line without the value, set proxy key <name>: the user types it in a field "
                    "you never see, so never ask for it in the chat. A url no configuration holds yet asks the user "
                    "to let it with a card of the page as the line runs: run the line, never ask in the chat first.";
    t.input = R"({"type":"object","properties":{)"
              R"("description":{"type":"string","description":"Why I'm configuring the server, written first"},)"
              R"("lines":{"type":"string","description":"CLI lines, one per line"}},)"
              R"("required":["description","lines"],"additionalProperties":false})";
    t.call  = call;
    return { t };
}

SERVER_TOOLS(config_tool);
