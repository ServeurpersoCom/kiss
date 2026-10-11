// config.cpp: the config tool, a batch of CLI lines on the engine, as the
// config tool of KiSS runs them on the page

#include "tools.h"
#include "yyjson.h"

static tool_result call(yyjson_val * args, const tool_tick &) {
    yyjson_val *        lines = yyjson_obj_get(args, "lines");
    const engine_result r     = engine_run(std::string(yyjson_get_str(lines), yyjson_get_len(lines)), ENGINE_MCP);
    return { r.text, !r.ok };
}

static std::vector<tool> config_tool(const engine_reader & config) {
    tool t;
    t.suffix      = "config";
    t.description = "Runs lines of the CLI of the KiSS server " + config.get("server name").value_or("") + " on " +
                    engine_platform() +
                    ", one command per line, as one batch that applies whole or not at all, the way Cisco IOS "
                    "reads them: set <module> <key> [item] <value>, no <module> <key> [item], show <module>, "
                    "show running-config [all], show startup-config, copy running-config startup-config. "
                    "A set or no line may open with linux, macos or windows to hold on that platform only. "
                    "Secrets are written in server.conf, never here.";
    t.input = R"({"type":"object","properties":{)"
              R"("description":{"type":"string","description":"Why I'm configuring the server"},)"
              R"("lines":{"type":"string","description":"CLI lines, one per line"}},)"
              R"("required":["description","lines"],"additionalProperties":false})";
    t.call  = call;
    return { t };
}

SERVER_TOOLS(config_tool);
