#pragma once
// parse.h: the grammar of a line: lines, tokens, the filter after a pipe, and
// a value as a line writes it

#include "engine/text.h"

#include <optional>
#include <utility>

// one command per line; blank lines and comments are skipped
words_t split_lines(const std::string & text);

// splits a line on blanks; a token opening with a double quote takes JSON
// escapes and one opening with a single quote is literal, so every value
// printed by the schema reads back unchanged
words_t tokenize(const std::string & line);

// splits a line at its first pipe outside quotes, as IOS does: the command,
// then the filter, the rest of the line as written, pipes included
std::pair<std::string, std::optional<std::string>> split_filter(const std::string & line);

// a value as a line writes it: bare when it reads back so, else a JSON string
std::string quote(const std::string & v);

// a name of an item: a word of its own, as a server writes the name of a tool
bool item_name(const std::string & s);
