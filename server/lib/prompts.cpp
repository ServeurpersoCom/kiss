// prompts.cpp: the texts of prompts.json and of the prompts.json of the page,
// each read once; a tool or a module asking for a text they lack stops the
// server

#include "lib/prompts.h"

#include "engine/schema.h"
#include "lib/log.h"
#include "server/prompts.json.hpp"
#include "webui/src/prompts.json.hpp"
#include "yyjson.h"

#include <cstdlib>
#include <memory>

static std::shared_ptr<yyjson_doc> read(const unsigned char * json, unsigned int len) {
    return std::shared_ptr<yyjson_doc>(yyjson_read(reinterpret_cast<const char *>(json), len, 0), yyjson_doc_free);
}

static yyjson_val * root() {
    static const std::shared_ptr<yyjson_doc> doc = read(server_prompts_json, server_prompts_json_len);
    return yyjson_doc_get_root(doc.get());
}

static yyjson_val * page() {
    static const std::shared_ptr<yyjson_doc> doc = read(webui_src_prompts_json, webui_src_prompts_json_len);
    return yyjson_doc_get_root(doc.get());
}

static yyjson_val * entry(yyjson_val * from, const char * group, const std::string & name) {
    yyjson_val * v = yyjson_obj_get(yyjson_obj_get(from, group), name.c_str());
    if (!v) {
        log_error("Prompts", "prompts.json%s has no %s %s", from == page() ? " of the page" : "", group, name.c_str());
        std::abort();
    }
    return v;
}

static std::string marked(std::string out, const prompt_marks & marks) {
    for (const auto & [mark, value] : marks) {
        const std::string spelled = "{" + mark + "}";
        for (size_t at = out.find(spelled); at != std::string::npos; at = out.find(spelled, at + value.size())) {
            out.replace(at, spelled.size(), value);
        }
    }
    return out;
}

static std::string schema(yyjson_val * tool) {
    size_t                      len = 0;
    const std::shared_ptr<char> json(yyjson_val_write(yyjson_obj_get(tool, "input"), 0, &len), free);
    return std::string(json.get(), len);
}

static std::string text(yyjson_val * v) {
    return std::string(yyjson_get_str(v), yyjson_get_len(v));
}

std::string prompts_description(const std::string & tool, const prompt_marks & marks) {
    return marked(text(yyjson_obj_get(entry(root(), "tools", tool), "description")), marks);
}

std::string prompts_input(const std::string & tool) {
    return schema(entry(root(), "tools", tool));
}

std::string prompts_page_description(const prompt_marks & marks) {
    return marked(text(yyjson_obj_get(entry(page(), "tools", "config"), "description")), marks);
}

std::string prompts_page_input() {
    return schema(entry(page(), "tools", "config"));
}

std::string prompts_module(const std::string & module) {
    return text(entry(root(), "modules", module));
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
