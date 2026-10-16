// complete.cpp: what may follow a line being typed

#include "engine/complete.h"

#include "engine/registry.h"

#include <algorithm>
#include <set>

static bool hint(const std::string & word) {
    return word[0] == '<';
}

words_t complete(const words_t & words, const std::string & partial) {
    const walk_t w = walk(words);
    if (!w.ambiguous.empty()) {
        return {};
    }
    std::set<std::string> found;
    if (w.consumed == words.size()) {
        for (const command * c : w.pool) {
            if (c->path.size() > w.consumed) {
                found.insert(c->path[w.consumed]);
            }
        }
    }
    if (w.found && w.found->complete && (w.consumed == words.size() || w.depth == w.consumed)) {
        for (const std::string & a : w.found->complete(words_t(words.begin() + (long) w.depth, words.end()))) {
            found.insert(a);
        }
    }
    words_t out;
    for (const std::string & f : found) {
        if (hint(f) ? partial.empty() : starts_with(lower(f), lower(partial))) {
            out.push_back(f);
        }
    }
    std::stable_partition(out.begin(), out.end(), [](const std::string & s) { return !hint(s); });
    return out;
}

std::string partial_word(const std::string & text) {
    const size_t cut = text.find_last_of(" |");
    return cut == std::string::npos ? text : text.substr(cut + 1);
}

std::string extend(const std::string & text, const words_t & found) {
    words_t words;
    std::copy_if(found.begin(), found.end(), std::back_inserter(words), [](const std::string & f) { return !hint(f); });
    if (words.empty()) {
        return text;
    }
    const std::string partial = partial_word(text);
    const std::string head    = text.substr(0, text.size() - partial.size());
    if (words.size() == 1) {
        return head + words[0] + " ";
    }
    std::string common = words[0];
    for (const std::string & w : words) {
        size_t n = 0;
        while (n < common.size() && n < w.size() && common[n] == w[n]) {
            n++;
        }
        common.resize(n);
    }
    return common.size() > partial.size() ? head + common : text;
}
