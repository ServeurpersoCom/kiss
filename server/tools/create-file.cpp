// create-file.cpp: the create_file tool: a file written whole with the text
// given, empty or not, its parent directories made, a file already there
// written over, on the place of the tools on files

#include "lib/file.h"
#include "lib/json.h"
#include "lib/output.h"
#include "lib/place.h"
#include "lib/prompts.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <algorithm>

static const char * const NAME = "create_file";

static tool_outcome create_file(const place & at, yyjson_val * args, const tool_tick & tick) {
    yyjson_val *      named = yyjson_obj_get(args, "path");
    yyjson_val *      body  = yyjson_obj_get(args, "file_text");
    const std::string given = json_text(named);
    const std::string text  = json_text(body);
    if (!file_refused(given).empty()) {
        return { false, file_refused(given) };
    }

    bool              there  = false;
    const std::string failed = place_write(at, given, text, there, tick);
    if (!failed.empty()) {
        return { false, failed };
    }
    const long long lines = std::count(text.begin(), text.end(), '\n') + (!text.empty() && text.back() != '\n');
    return { true, std::string(there ? "Overwrote " : "Created ") + given + ", " +
                       output_counted(lines, "line", "lines") + ", " +
                       output_counted((long long) text.size(), "byte", "bytes") };
}

static std::vector<tool> create_file_tool(const engine_reader & config) {
    const place at = place_of(config);

    tool t;
    t.suffix      = NAME;
    t.description = prompts_description(NAME, {
                                                  { "hostname", config.get("server hostname").value_or("") }
    });
    t.input       = prompts_input(NAME);
    t.run         = [at](yyjson_val * args, const engine_typed &, const tool_tick & tick) {
        return create_file(at, args, tick);
    };
    return { t };
}

SERVER_TOOLS(create_file_tool);
