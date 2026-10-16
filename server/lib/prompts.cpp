// prompts.cpp: the texts of prompts.json, read once; a tool or a module asking
// for a text prompts.json lacks stops the server

#include "lib/prompts.h"

#include "engine/schema.h"
#include "lib/log.h"
#include "prompts.json.hpp"
#include "yyjson.h"

#include <cstdlib>
#include <memory>

static yyjson_val * root() {
    static const std::shared_ptr<yyjson_doc> doc(
        yyjson_read(reinterpret_cast<const char *>(prompts_json), prompts_json_len, 0), yyjson_doc_free);
    return yyjson_doc_get_root(doc.get());
}

static yyjson_val * entry(const char * group, const std::string & name) {
    yyjson_val * v = yyjson_obj_get(yyjson_obj_get(root(), group), name.c_str());
    if (!v) {
        log_error("Prompts", "prompts.json has no %s %s", group, name.c_str());
        std::abort();
    }
    return v;
}

static std::string text(yyjson_val * v) {
    return std::string(yyjson_get_str(v), yyjson_get_len(v));
}

std::string prompts_description(const std::string & tool, const prompt_marks & marks) {
    std::string out = text(yyjson_obj_get(entry("tools", tool), "description"));
    for (const auto & [mark, value] : marks) {
        const std::string spelled = "{" + mark + "}";
        for (size_t at = out.find(spelled); at != std::string::npos; at = out.find(spelled, at + value.size())) {
            out.replace(at, spelled.size(), value);
        }
    }
    return out;
}

std::string prompts_input(const std::string & tool) {
    size_t                      len = 0;
    const std::shared_ptr<char> json(yyjson_val_write(yyjson_obj_get(entry("tools", tool), "input"), 0, &len), free);
    return std::string(json.get(), len);
}

std::string prompts_module(const std::string & module) {
    return text(entry("modules", module));
}

std::string prompts_check() {
    words_t missing;
    for (const engine_module & m : schema_modules()) {
        if (!yyjson_obj_get(yyjson_obj_get(root(), "modules"), m.name.c_str())) {
            missing.push_back(m.name);
        }
    }
    return missing.empty() ? "" : "prompts.json has no module " + join(missing, ", ");
}
