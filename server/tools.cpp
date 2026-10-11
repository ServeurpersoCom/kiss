// tools.cpp: the sources of the tools, each registered by its file

#include "tools.h"

#include "yyjson.h"

#include <memory>

static std::vector<tool_source> & sources() {
    static std::vector<tool_source> list;
    return list;
}

bool tools_register(tool_source source) {
    sources().push_back(source);
    return true;
}

std::vector<tool> tools_list(const engine_reader & config) {
    std::vector<tool> out;
    for (tool_source source : sources()) {
        for (tool & t : source(config)) {
            out.push_back(std::move(t));
        }
    }
    return out;
}

// the key as a message opens with it: command -> Command
static std::string capital(const char * key) {
    std::string s = key;
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = (char) (s[0] - 'a' + 'A');
    }
    return s;
}

std::string tools_check(const tool & t, yyjson_val * args) {
    const std::shared_ptr<yyjson_doc> schema(yyjson_read(t.input.c_str(), t.input.size(), 0), yyjson_doc_free);
    yyjson_val *                      root       = yyjson_doc_get_root(schema.get());
    yyjson_val *                      properties = yyjson_obj_get(root, "properties");
    if (args && !yyjson_is_obj(args)) {
        return "Arguments are an object";
    }
    std::string unknown;
    size_t      i, n;
    yyjson_val *key, *value;
    yyjson_obj_foreach(args, i, n, key, value) {
        if (!yyjson_obj_get(properties, yyjson_get_str(key))) {
            unknown += (unknown.empty() ? "" : ", ") + std::string(yyjson_get_str(key));
        }
    }
    if (!unknown.empty()) {
        return "Unknown argument " + unknown;
    }
    yyjson_val * required;
    yyjson_arr_foreach(yyjson_obj_get(root, "required"), i, n, required) {
        const char * name   = yyjson_get_str(required);
        yyjson_val * given  = yyjson_obj_get(args, name);
        const bool   string = yyjson_equals_str(yyjson_obj_get(yyjson_obj_get(properties, name), "type"), "string");
        if (!given || (string && (!yyjson_is_str(given) || yyjson_get_len(given) == 0))) {
            return capital(name) + " argument required";
        }
    }
    return "";
}
