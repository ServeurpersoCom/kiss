// create-file.cpp: the create_file tool: a file written whole with the text
// given, empty or not, its parent directories made, a file already there
// written over

#include "file.h"
#include "output.h"
#include "tools.h"
#include "yyjson.h"

#include <algorithm>

namespace fs = std::filesystem;

static tool_result create_file(const std::string & cwd, yyjson_val * args) {
    yyjson_val *      named = yyjson_obj_get(args, "path");
    yyjson_val *      body  = yyjson_obj_get(args, "file_text");
    const std::string given(yyjson_get_str(named), yyjson_get_len(named));
    const std::string text(yyjson_get_str(body), yyjson_get_len(body));
    if (!file_refused(given).empty()) {
        return { file_refused(given), true };
    }

    const fs::path    path = file_path(cwd, given);
    std::error_code   ec;
    const bool        there  = fs::exists(path, ec);
    const std::string failed = file_write(path, text);
    if (!failed.empty()) {
        return { failed, true };
    }
    const long long lines = std::count(text.begin(), text.end(), '\n') + (!text.empty() && text.back() != '\n');
    return { std::string(there ? "Overwrote " : "Created ") + given + ", " + output_counted(lines, "line", "lines") +
                 ", " + output_counted((long long) text.size(), "byte", "bytes"),
             false };
}

static std::vector<tool> create_file_tool(const engine_reader & config) {
    const std::string cwd = config.get("server working-directory").value_or("");

    tool t;
    t.suffix      = "create_file";
    t.description = "Create a new file with content on " + config.get("server hostname").value_or("") +
                    ", its directories made, a file already there written over.";
    t.input =
        R"({"type":"object","properties":{)"
        R"("description":{"type":"string","minLength":1,"description":"Why I'm creating this file, written first"},)"
        R"("path":{"type":"string","minLength":1,"description":"The file to create, written second"},)"
        R"("file_text":{"type":"string","description":"The content of the file, written last"}},)"
        R"("required":["description","path","file_text"],"additionalProperties":false})";
    t.call = [cwd](yyjson_val * args, const engine_typed &, const tool_tick &) {
        return create_file(cwd, args);
    };
    return { t };
}

SERVER_TOOLS(create_file_tool);
