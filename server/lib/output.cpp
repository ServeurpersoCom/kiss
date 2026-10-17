// output.cpp: the output of a process as a tool returns it

#include "lib/output.h"

#include <algorithm>

// the length of the UTF-8 sequence that starts at s[i], 0 when it is invalid
static size_t sequence(const std::string & s, size_t i) {
    const unsigned char c   = (unsigned char) s[i];
    size_t              n   = 0;
    unsigned            min = 0, cp = 0;
    if (c < 0x80) {
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        n = 2, min = 0x80, cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3, min = 0x800, cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4, min = 0x10000, cp = c & 0x07;
    } else {
        return 0;
    }
    if (i + n > s.size()) {
        return 0;
    }
    for (size_t k = 1; k < n; k++) {
        const unsigned char d = (unsigned char) s[i + k];
        if ((d & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (d & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) {
        return 0;
    }
    return n;
}

// the text with every run of invalid UTF-8 bytes replaced by what decode
// reads in it, or by one U+FFFD when it reads nothing, so binary output stays
// close to the size read
static std::string output_utf8(const std::string & text, output_decode decode) {
    std::string out;
    out.reserve(text.size());
    std::string run;
    for (size_t i = 0; i <= text.size();) {
        const size_t n = i < text.size() ? sequence(text, i) : 1;
        if (n == 0) {
            run += text[i++];
            continue;
        }
        if (!run.empty()) {
            const std::string read = decode ? decode(run) : "";
            out += read.empty() ? "\xEF\xBF\xBD" : read;
            run.clear();
        }
        if (i < text.size()) {
            out.append(text, i, n);
        }
        i += n;
    }
    return out;
}

output_buffer::output_buffer(size_t budget, output_decode runs) :
    head_max(budget / 2),
    tail_max(budget - budget / 2),
    decode(runs) {}

void output_buffer::add(const char * data, size_t size) {
    count += size;
    const size_t to_head = std::min(size, head_max - head.size());
    head.append(data, to_head);
    data += to_head;
    size -= to_head;
    for (size_t i = 0; i < size && tail_max > 0; i++) {
        if (tail.size() < tail_max) {
            tail += data[i];
        } else {
            tail[start] = data[i];
            start       = (start + 1) % tail_max;
        }
    }
}

std::string output_buffer::text() const {
    const std::string last = tail.substr(start) + tail.substr(0, start);
    if (!truncated()) {
        return output_utf8(head + last, decode);
    }
    // both cuts fall on line ends when the kept bytes hold one, so no line and
    // no UTF-8 sequence comes out split
    const size_t      end   = head.rfind('\n');
    const size_t      begin = last.find('\n');
    const std::string first = end == std::string::npos ? head + "\n" : head.substr(0, end + 1);
    const std::string final = begin == std::string::npos ? last : last.substr(begin + 1);
    const size_t      left  = count - std::min(head.size(), first.size()) - final.size();
    return output_utf8(first, decode) + "Long output with " + std::to_string(left) + " bytes hidden from context\n" +
           output_utf8(final, decode);
}

std::string output_counted(long long n, const char * one, const char * many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
