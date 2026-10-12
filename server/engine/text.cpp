// text.cpp: the words of the engine

#include "engine/text.h"

#include <algorithm>
#include <stdexcept>

std::string comment(const std::string & text) {
    return std::string(COMMENT_PREFIX) + " " + text;
}

std::string lower(std::string s) {
    for (char & c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = (char) (c - 'A' + 'a');
        }
    }
    return s;
}

bool starts_with(const std::string & s, const std::string & prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string join(const words_t & words, const char * sep) {
    std::string out;
    for (const std::string & w : words) {
        if (!out.empty()) {
            out += sep;
        }
        out += w;
    }
    return out;
}

words_t split(const std::string & text, char sep) {
    words_t out;
    size_t  start = 0;
    for (;;) {
        const size_t end = text.find(sep, start);
        out.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            return out;
        }
        start = end + 1;
    }
}

std::string trim(const std::string & s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) {
        return "";
    }
    const size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

[[noreturn]] void fail(const std::string & message) {
    throw std::runtime_error(message);
}

std::string beyond(const words_t & head, const words_t & rest) {
    return "nothing goes after " + join(head, " ") + ": " + join(rest, " ");
}
