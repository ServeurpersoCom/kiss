// parse.cpp: the grammar of a line

#include "engine/parse.h"

#include <cctype>
#include <cstdio>
#include <cstring>

words_t split_lines(const std::string & text) {
    words_t lines;
    for (const std::string & raw : split(text, '\n')) {
        const std::string line = trim(raw);
        if (!line.empty() && !starts_with(line, COMMENT_PREFIX)) {
            lines.push_back(line);
        }
    }
    return lines;
}

static void put_utf8(std::string & out, unsigned cp) {
    if (cp < 0x80) {
        out += (char) cp;
    } else if (cp < 0x800) {
        out += (char) (0xC0 | (cp >> 6));
        out += (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char) (0xE0 | (cp >> 12));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    } else {
        out += (char) (0xF0 | (cp >> 18));
        out += (char) (0x80 | ((cp >> 12) & 0x3F));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
}

static bool hex4(const std::string & s, size_t at, unsigned & out) {
    if (at + 4 > s.size()) {
        return false;
    }
    out = 0;
    for (size_t i = at; i < at + 4; i++) {
        const char c = s[i];
        out <<= 4;
        if (c >= '0' && c <= '9') {
            out |= (unsigned) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            out |= (unsigned) (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            out |= (unsigned) (c - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

static bool blank(char c) {
    return c == ' ' || c == '\t';
}

words_t tokenize(const std::string & line) {
    words_t tokens;
    size_t  i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (blank(c)) {
            i++;
            continue;
        }
        if (c == '"') {
            std::string token;
            size_t      j = i + 1;
            for (; j < line.size() && line[j] != '"'; j++) {
                if (line[j] != '\\' || j + 1 >= line.size()) {
                    token += line[j];
                    continue;
                }
                const char e  = line[++j];
                unsigned   cp = 0;
                if (e == 'u' && hex4(line, j + 1, cp)) {
                    j += 4;
                    unsigned low = 0;
                    if (cp >= 0xD800 && cp < 0xDC00 && line.compare(j + 1, 2, "\\u") == 0 && hex4(line, j + 3, low) &&
                        low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        j += 6;
                    }
                    put_utf8(token, cp);
                    continue;
                }
                switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        token += e;
                        break;
                    case 'b':
                        token += '\b';
                        break;
                    case 'f':
                        token += '\f';
                        break;
                    case 'n':
                        token += '\n';
                        break;
                    case 'r':
                        token += '\r';
                        break;
                    case 't':
                        token += '\t';
                        break;
                    default:
                        token += '\\';
                        token += e;
                }
            }
            if (j >= line.size()) {
                fail("unterminated \"");
            }
            tokens.push_back(token);
            i = j + 1;
            continue;
        }
        if (c == '\'') {
            const size_t j = line.find('\'', i + 1);
            if (j == std::string::npos) {
                fail("unterminated '");
            }
            tokens.push_back(line.substr(i + 1, j - i - 1));
            i = j + 1;
            continue;
        }
        size_t j = i;
        while (j < line.size() && !blank(line[j])) {
            j++;
        }
        tokens.push_back(line.substr(i, j - i));
        i = j;
    }
    return tokens;
}

std::pair<std::string, std::optional<std::string>> split_filter(const std::string & line) {
    char quote = 0;
    for (size_t i = 0; i < line.size(); i++) {
        const char c = line[i];
        if (quote) {
            if (c == '\\' && quote == '"') {
                i++;
            } else if (c == quote) {
                quote = 0;
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == PIPE[0]) {
            return { trim(line.substr(0, i)), line.substr(i + 1) };
        }
    }
    return { trim(line), std::nullopt };
}

// a value that reads back as one token without quotes, a pipe opening a filter
static bool bare(const std::string & v) {
    if (v.empty() || v[0] == '"' || v[0] == '\'') {
        return false;
    }
    for (char c : v) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == PIPE[0]) {
            return false;
        }
    }
    return true;
}

std::string quote(const std::string & v) {
    if (bare(v)) {
        return v;
    }
    std::string out = "\"";
    for (unsigned char c : v) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char) c;
                }
        }
    }
    return out + "\"";
}

bool item_name(const std::string & s) {
    if (s.empty() || !isalnum((unsigned char) s[0])) {
        return false;
    }
    for (char c : s) {
        if (!isalnum((unsigned char) c) && !strchr("_.:/-", c)) {
            return false;
        }
    }
    return true;
}
