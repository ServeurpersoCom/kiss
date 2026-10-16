// str-replace.cpp: the str_replace tool: the one place a text file holds a
// string, replaced by another, the file written in place

#include "lib/file.h"
#include "lib/prompts.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <algorithm>

namespace fs = std::filesystem;

struct replacer {
    std::string cwd;
    // the bytes of a file loaded whole
    size_t      load;
};

static tool_outcome str_replace(const replacer & r, yyjson_val * args) {
    const auto text_of = [args](const char * name) {
        yyjson_val * v = yyjson_obj_get(args, name);
        return std::string(yyjson_get_str(v), yyjson_get_len(v));
    };
    const std::string given = text_of("path");
    const std::string old   = text_of("old_str");
    const std::string fresh = text_of("new_str");
    if (!file_refused(given).empty()) {
        return { false, file_refused(given) };
    }

    const fs::path    path = file_path(r.cwd, given);
    std::string       text;
    const std::string why = file_load(path, r.load, text);
    if (!why.empty()) {
        return { false, why };
    }
    const file_found found = file_find(text, old);
    if (found.count == 0) {
        return { false, "Not found in " + given };
    }
    if (found.count > 1) {
        return { false, "Found " + std::to_string(found.count) + " times in " + given + ", old_str must be unique" };
    }
    const long long line = std::count(text.begin(), text.begin() + (long) found.at, '\n') + 1;
    text.replace(found.at, old.size(), fresh);
    const std::string failed = file_write(path, text);
    if (!failed.empty()) {
        return { false, failed };
    }
    return { true, "Replaced at line " + std::to_string(line) + " of " + given + ", " + std::to_string(old.size()) +
                       " -> " + std::to_string(fresh.size()) + " bytes" };
}

static std::vector<tool> str_replace_tool(const engine_reader & config) {
    replacer r;
    r.cwd  = config.get("server working-directory").value_or("");
    r.load = (size_t) std::stoull(config.get("files max-load").value_or("0"));

    tool t;
    t.suffix = "str_replace";
    t.description =
        prompts_description("str_replace", {
                                               { "hostname", config.get("server hostname").value_or("") }
    });
    t.input = prompts_input("str_replace");
    t.run   = [r](yyjson_val * args, const engine_typed &, const tool_tick &) {
        return str_replace(r, args);
    };
    return { t };
}

SERVER_TOOLS(str_replace_tool);
