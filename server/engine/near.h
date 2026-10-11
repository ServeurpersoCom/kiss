#pragma once
// near.h: the typo a word may be of a known word

#include "engine/text.h"

// ", did you mean x" when a candidate is close enough to be a typo of the
// word, else nothing; head, the words before the candidate, makes the
// suggestion a whole one
std::string did_you_mean(const std::string & word, const words_t & candidates, const std::string & head = "");
