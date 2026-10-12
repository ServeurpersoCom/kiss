#pragma once
// near.h: the typo a word may be of a known word

#include "engine/text.h"

// ", did you mean x" when a candidate is close enough to be a typo of the
// word, else nothing; head, the words before the candidate, makes the
// suggestion a whole one
std::string did_you_mean(const std::string & word, const words_t & candidates, const std::string & head = "");

// the word a typed word names in a closed set: itself, else the only word it
// starts, case aside; empty when it starts none
std::string pick(const std::string & word, const words_t & words, const std::string & what);

// the word a typed word names in a closed set, or why it names none
std::string choose(const std::string & word, const words_t & words, const std::string & what);
