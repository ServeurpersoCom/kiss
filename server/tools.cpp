// tools.cpp: the tools the server serves over MCP

#include "tools.h"

#include "engine.h"
#include "yyjson.h"

#include <cstring>

// config: a batch of CLI lines on the engine, as the config tool of KiSS runs
// them on the page
static tool_result config_call(yyjson_val * args) {
    yyjson_val * lines = yyjson_obj_get(args, "lines");
    if (!yyjson_is_str(lines)) {
        return { "lines is a string of CLI lines, one per line", true };
    }
    const engine_result r = engine_run(std::string(yyjson_get_str(lines), yyjson_get_len(lines)), ENGINE_MCP);
    return { r.text, !r.ok };
}

static const char * const CONFIG_SUFFIX = "config";

std::vector<tool> tools_list(const std::string & server) {
    tool config;
    config.suffix      = CONFIG_SUFFIX;
    config.description = "Runs lines of the CLI of the KiSS server " + server + " on " + engine_platform() +
                         ", one command per line, as one batch that applies whole or not at all, the way Cisco IOS "
                         "reads them: set <module> <key> [item] <value>, no <module> <key> [item], show <module>, "
                         "show running-config [all], show startup-config, copy running-config startup-config. "
                         "A set or no line may open with linux, macos or windows to hold on that platform only. "
                         "Secrets are written in server.conf, never here.";
    config.input =
        R"({"type":"object","properties":{"lines":{"type":"string","description":"CLI lines, one per line"}},)"
        R"("required":["lines"]})";
    config.call = config_call;
    return { config };
}

size_t tools_suffix_max() {
    return strlen(CONFIG_SUFFIX);
}
