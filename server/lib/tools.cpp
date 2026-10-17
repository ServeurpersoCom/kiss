// tools.cpp: the sources of the tools, each registered by its file

#include "lib/tools.h"

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
        if (!yyjson_obj_get(args, yyjson_get_str(required))) {
            return capital(yyjson_get_str(required)) + " argument required";
        }
    }
    yyjson_obj_foreach(args, i, n, key, value) {
        yyjson_val * property = yyjson_obj_get(properties, yyjson_get_str(key));
        if (!yyjson_equals_str(yyjson_obj_get(property, "type"), "string")) {
            continue;
        }
        if (!yyjson_is_str(value)) {
            return capital(yyjson_get_str(key)) + " argument is a string";
        }
        if (yyjson_get_len(value) < yyjson_get_uint(yyjson_obj_get(property, "minLength"))) {
            return capital(yyjson_get_str(key)) + " argument required";
        }
    }
    return "";
}

void tools_form(yyjson_mut_doc * doc, yyjson_mut_val * input, const tool_outcome & asked) {
    yyjson_mut_obj_add_str(doc, input, "method", "elicitation/create");
    yyjson_mut_val * params = yyjson_mut_obj_add_obj(doc, input, "params");
    yyjson_mut_obj_add_str(doc, params, "mode", "form");
    yyjson_mut_obj_add_strncpy(doc, params, "message", asked.text.c_str(), asked.text.size());
    yyjson_mut_val * schema = yyjson_mut_obj_add_obj(doc, params, "requestedSchema");
    yyjson_mut_obj_add_str(doc, schema, "type", "object");
    yyjson_mut_val * properties = yyjson_mut_obj_add_obj(doc, schema, "properties");
    yyjson_mut_val * required   = yyjson_mut_obj_add_arr(doc, schema, "required");
    for (const engine_ask & a : asked.asks) {
        yyjson_mut_val * field = yyjson_mut_obj_add_obj(doc, properties, a.key.c_str());
        yyjson_mut_obj_add_str(doc, field, "type", a.yes ? "boolean" : "string");
        yyjson_mut_arr_add_strcpy(doc, required, a.key.c_str());
    }
}

bool tools_typed(yyjson_val * response, engine_typed & typed) {
    yyjson_val * action = yyjson_obj_get(response, "action");
    if (!yyjson_is_str(action) || std::string(yyjson_get_str(action)) != "accept") {
        return false;
    }
    size_t       i, n;
    yyjson_val * key;
    yyjson_val * value;
    yyjson_obj_foreach(yyjson_obj_get(response, "content"), i, n, key, value) {
        if (yyjson_is_str(value)) {
            typed[yyjson_get_str(key)] = yyjson_get_str(value);
        } else if (yyjson_is_true(value)) {
            typed[yyjson_get_str(key)] = "true";
        }
    }
    return true;
}
