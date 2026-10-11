// filter.cpp: the filter after a pipe, as IOS reads it

#include "engine/filter.h"

#include <regex>

// the filter after a pipe, as IOS reads it: show running-config | include
// port; every filter takes a pattern, the rest of the line as written, a
// regular expression that minds case; count tells how many lines match in the
// words of IOS, as a note
const words_t & filter_names() {
    static const words_t names = { "begin", "count", "exclude", "include" };
    return names;
}

filter_t compile_filter(const std::string & spec) {
    const words_t &   names   = filter_names();
    const std::string trimmed = trim(spec);
    const std::string word    = trimmed.substr(0, trimmed.find_first_of(" \t"));
    const std::string pattern = trim(trimmed.substr(word.size()));
    words_t           hits;
    for (const std::string & n : names) {
        if (starts_with(n, lower(word))) {
            hits.push_back(n);
        }
    }
    if (hits.size() != 1) {
        fail("unknown filter \"" + word + "\", use " + join(names, " "));
    }
    const std::string name = hits[0];
    if (pattern.empty()) {
        fail(name + " needs a pattern");
    }
    std::regex re;
    try {
        re = std::regex(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error &) {
        fail("\"" + pattern + "\" is not a pattern");
    }
    return [name, re](const words_t & lines) {
        words_t out;
        size_t  matches = 0;
        bool    begun   = false;
        for (const std::string & l : lines) {
            const bool hit = std::regex_search(l, re);
            matches += hit;
            begun = begun || hit;
            if ((name == "begin" && begun) || (name == "include" && hit) || (name == "exclude" && !hit)) {
                out.push_back(l);
            }
        }
        if (name == "count") {
            out = { comment("number of lines which match regexp = " + std::to_string(matches)) };
        }
        return out;
    };
}
