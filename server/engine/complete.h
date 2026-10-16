#pragma once
// complete.h: what may follow a line being typed, as the console completes it

#include "engine/text.h"

#include <string>

// what may follow the words, the partial word filtering the candidates: path
// words while the path goes on, then the arguments the command proposes; a
// candidate in <> names what to type and shows only before a word is started
words_t complete(const words_t & words, const std::string & partial);

// the word a line ends on, after its last blank or pipe
std::string partial_word(const std::string & text);

// the line completed as far as the candidates agree: the whole word and a
// blank for one candidate, their common start for several
std::string extend(const std::string & text, const words_t & found);
