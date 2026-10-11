// near.cpp: the typo a word may be of a known word

#include "engine/near.h"

#include <algorithm>

// a typo is a word within one edit per this many letters of a known word
static const size_t TYPO_LETTERS_PER_EDIT = 3;

// the edits turning a into b: inserting, deleting or changing a letter, or
// swapping two letters side by side, each one edit
static size_t distance(const std::string & a, const std::string & b) {
    std::vector<size_t> before, prev(b.size() + 1), row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) {
        prev[j] = j;
    }
    for (size_t i = 1; i <= a.size(); i++) {
        row[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            row[j]            = std::min({ prev[j] + 1, row[j - 1] + 1, prev[j - 1] + cost });
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                row[j] = std::min(row[j], before[j - 2] + 1);
            }
        }
        before = prev;
        prev   = row;
    }
    return prev[b.size()];
}

std::string did_you_mean(const std::string & word, const words_t & candidates, const std::string & head) {
    const size_t max  = std::max<size_t>(1, word.size() / TYPO_LETTERS_PER_EDIT);
    size_t       best = max + 1;
    std::string  near;
    for (const std::string & c : candidates) {
        const size_t d = distance(lower(word), lower(c));
        if (d < best) {
            best = d;
            near = c;
        }
    }
    return near.empty() ? "" : ", did you mean " + head + near;
}
