#pragma once
// schema.h: the keys every module declares, how a line names one, and the
// line that writes one back

#include "engine/engine.h"
#include "engine/text.h"

#include <utility>

// a stored key is the key, then the item name for a named key
std::string                         stored(const std::string & key, const std::string & item);
// the key and the item name of a stored key
std::pair<std::string, std::string> unstore(const std::string & s);

const std::vector<engine_module> & schema_modules();
const engine_module *              find_module(const std::string & name);
const engine_key *                 find_key(const std::string & key);
words_t                            module_names();
words_t                            key_names(const std::string & module);
bool                               collection(const std::string & module);
std::string                        schema_module(const std::string & word);
std::string                        schema_key(const std::string & module, const std::string & word);

// the value a key takes from what a line writes, checked against its kind
std::string schema_parse(const std::string & key, const std::string & raw);

struct key_words {
    std::string        key;
    const engine_key * def;
    std::string        module;
    std::string        item;
    words_t            rest;
};

// the key the words name, its item for a named key, and the words after it
key_words schema_read(const words_t & words);

// what may follow the words of a key: modules, keys, items, a value
words_t schema_next(const engine_reader & config, const words_t & words, bool value, bool items);

bool        secret(const std::string & s);
// the line that sets a key, a secret with its value
std::string set_line(const std::string & s, const std::string & v);
// the line that shows a key, a secret out of view
std::string show_line(const std::string & s, const std::string & v);
