// str-replace.cpp: the str_replace tool: the one place a text file holds a
// string, replaced by another, the file written in place

#include "file.h"
#include "tools.h"
#include "yyjson.h"

#include <algorithm>

namespace fs = std::filesystem;

struct replacer {
    std::string cwd;
    // the bytes of a file loaded whole
    size_t      load;
};

static tool_result str_replace(const replacer & r, yyjson_val * args) {
    const std::string given = yyjson_get_str(yyjson_obj_get(args, "path"));
    const std::string old   = yyjson_get_str(yyjson_obj_get(args, "old_str"));
    yyjson_val *      with  = yyjson_obj_get(args, "new_str");
    if (with && !yyjson_is_str(with)) {
        return { "New_str is a string", true };
    }
    const std::string fresh = with ? std::string(yyjson_get_str(with), yyjson_get_len(with)) : "";

    const fs::path    path = file_path(r.cwd, given);
    std::string       text;
    const std::string why = file_load(path, r.load, text);
    if (!why.empty()) {
        return { why, true };
    }
    const file_found found = file_find(text, old);
    if (found.count == 0) {
        return { "Not found in " + given, true };
    }
    if (found.count > 1) {
        return { "Found " + std::to_string(found.count) + " times in " + given + ", old_str must be unique", true };
    }
    const long long line = std::count(text.begin(), text.begin() + (long) found.at, '\n') + 1;
    text.replace(found.at, old.size(), fresh);
    const std::string failed = file_write(path, text);
    if (!failed.empty()) {
        return { failed, true };
    }
    return { "Replaced at line " + std::to_string(line) + " of " + given + ", " + std::to_string(old.size()) + " -> " +
                 std::to_string(fresh.size()) + " bytes",
             false };
}

static std::vector<tool> str_replace_tool(const engine_reader & config) {
    replacer r;
    r.cwd  = config.get("server working-directory").value_or("");
    r.load = (size_t) std::stoull(config.get("files max-load").value_or("0"));

    tool t;
    t.suffix      = "str_replace";
    t.description = "Replace a unique string in a file with another string on " +
                    config.get("server hostname").value_or("") +
                    ". The string to replace must appear exactly once in the file.";
    t.input =
        R"({"type":"object","properties":{)"
        R"("description":{"type":"string","description":"Why I'm making this edit, written first"},)"
        R"("path":{"type":"string","description":"The file to edit"},)"
        R"("old_str":{"type":"string","description":"The string to replace, unique in the file"},)"
        R"("new_str":{"type":"string","default":"","description":"The string to replace it with, empty to delete"}},)"
        R"("required":["description","path","old_str"],"additionalProperties":false})";
    t.call = [r](yyjson_val * args, const engine_typed &, const tool_tick &) {
        return str_replace(r, args);
    };
    return { t };
}

SERVER_TOOLS(str_replace_tool);
