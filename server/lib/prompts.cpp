// prompts.cpp: the texts of prompts.json and of the prompts.json of the page,
// each read once; a tool or a module asking for a text they lack stops the
// server

#include "lib/prompts.h"

#include "engine/schema.h"
#include "lib/json.h"
#include "lib/log.h"
#include "lib/tools.h"
#include "server/prompts.json.hpp"
#include "webui/src/prompts.json.hpp"

#include <cstdlib>
#include <memory>

static json_doc read(const unsigned char * json, unsigned int len) {
    return json_read(std::string(reinterpret_cast<const char *>(json), len));
}

static yyjson_val * root() {
    static const json_doc doc = read(server_prompts_json, server_prompts_json_len);
    return yyjson_doc_get_root(doc.get());
}

static yyjson_val * page() {
    static const json_doc doc = read(webui_src_prompts_json, webui_src_prompts_json_len);
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
    return json_write(yyjson_obj_get(tool, "input"));
}

std::string prompts_description(const std::string & tool, const prompt_marks & marks) {
    return marked(json_text(yyjson_obj_get(entry(root(), "tools", tool), "description")), marks);
}

std::string prompts_input(const std::string & tool) {
    return schema(entry(root(), "tools", tool));
}

std::string prompts_page_description(const prompt_marks & marks) {
    return marked(json_text(yyjson_obj_get(entry(page(), "tools", TOOL_CONFIG), "description")), marks);
}

std::string prompts_page_input() {
    return schema(entry(page(), "tools", TOOL_CONFIG));
}

std::string prompts_module(const std::string & module) {
    return json_text(entry(root(), "modules", module));
}

std::string prompts_compaction(const std::string & name) {
    return json_text(entry(page(), "compaction", name));
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
