#pragma once
// json.h: the JSON the server reads and writes, over yyjson: a document that
// frees itself, a string value as a text, a value as the JSON that writes it

#include "yyjson.h"

#include <cstdlib>
#include <memory>
#include <string>

using json_doc = std::shared_ptr<yyjson_doc>;
using json_mut = std::shared_ptr<yyjson_mut_doc>;

// an object that holds nothing, and a list of nothing
constexpr const char * JSON_OBJECT = "{}";
constexpr const char * JSON_LIST   = "[]";

// the document a text reads as, none when it is no JSON
inline json_doc json_read(const std::string & text) {
    return json_doc(yyjson_read(text.data(), text.size(), 0), yyjson_doc_free);
}

// a document to build
inline json_mut json_new() {
    return json_mut(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
}

// the text of a string value, empty for any other value and for none
inline std::string json_text(yyjson_val * v) {
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : "";
}

// a text under a key of an object being built
inline void json_put(yyjson_mut_doc * d, yyjson_mut_val * o, const char * key, const std::string & text) {
    yyjson_mut_obj_add_strncpy(d, o, key, text.data(), text.size());
}

// a JSON text as a value of a document being built, null when it reads as none
inline yyjson_mut_val * json_value(yyjson_mut_doc * d, const std::string & text) {
    const json_doc v = json_read(text);
    return v ? yyjson_val_mut_copy(d, yyjson_doc_get_root(v.get())) : yyjson_mut_null(d);
}

// the JSON that writes a value, empty for none
inline std::string json_write(yyjson_val * v) {
    const std::shared_ptr<char> text(yyjson_val_write(v, 0, nullptr), free);
    return text ? text.get() : "";
}

// the JSON that writes a document built, empty when it holds no root
inline std::string json_write(yyjson_mut_doc * d) {
    const std::shared_ptr<char> text(yyjson_mut_write(d, 0, nullptr), free);
    return text ? text.get() : "";
}

inline std::string json_write(const json_mut & d) {
    return json_write(d.get());
}

// the JSON string that writes a text
inline std::string json_quote(const std::string & s) {
    const json_mut doc = json_new();
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    return json_write(doc);
}
