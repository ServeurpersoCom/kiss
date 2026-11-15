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

// the line that counts the bytes a cut hides
static std::string hidden(size_t n) {
    return "Long output with " + std::to_string(n) + " bytes hidden from context\n";
}

// the whole lines at the front of a text that hold n bytes or more, taken
// off it, all of it when no line ends there: the bytes taken
static size_t drop_front(std::string & s, size_t n) {
    const size_t at   = s.find('\n', n - 1);
    const size_t gone = at == std::string::npos ? s.size() : at + 1;
    s.erase(0, gone);
    return gone;
}

// the whole lines at the back of a text that hold n bytes or more, taken off
// it: the bytes taken
static size_t drop_back(std::string & s, size_t n) {
    const size_t room = s.size() > n ? s.size() - n : 0;
    const size_t at   = room == 0 ? std::string::npos : s.rfind('\n', room - 1);
    const size_t keep = at == std::string::npos ? 0 : at + 1;
    const size_t gone = s.size() - keep;
    s.resize(keep);
    return gone;
}

std::string output_buffer::text() const {
    const std::string last = tail.substr(start) + tail.substr(0, start);
    if (!truncated()) {
        return output_utf8(head + last, decode);
    }
    // both cuts fall on line ends when the kept bytes hold one, so no line and
    // no UTF-8 sequence comes out split
    const size_t end   = head.rfind('\n');
    const size_t begin = last.find('\n');
    std::string  first =
        end == std::string::npos ? output_utf8(head, decode) + "\n" : output_utf8(head.substr(0, end + 1), decode);
    std::string  final       = output_utf8(begin == std::string::npos ? last : last.substr(begin + 1), decode);
    // the bytes of the output the head and the tail still show
    size_t       shown_first = end == std::string::npos ? head.size() : end + 1;
    size_t       shown_final = begin == std::string::npos ? last.size() : last.size() - begin - 1;
    // the head, the line and the tail fit the budget together: whole lines
    // go from the larger of the head and the tail, the head on a tie, so the
    // end of the output, and the line that closes it, stays
    const size_t budget      = head_max + tail_max;
    for (;;) {
        const std::string line = hidden(count - shown_first - shown_final);
        const size_t      size = first.size() + line.size() + final.size();
        if (size <= budget || (first.empty() && final.empty())) {
            return first + line + final;
        }
        if (!first.empty() && first.size() >= final.size()) {
            const size_t gone = drop_back(first, size - budget);
            shown_first       = first.empty() ? 0 : shown_first - std::min(gone, shown_first);
        } else {
            const size_t gone = drop_front(final, size - budget);
            shown_final       = final.empty() ? 0 : shown_final - std::min(gone, shown_final);
        }
    }
}

std::string output_buffer::closed(const std::string & line) {
    const char last = count == 0 ? '\n' : tail.empty() ? head.back() : tail[(start + tail.size() - 1) % tail.size()];
    if (last != '\n') {
        add("\n", 1);
    }
    add(line.data(), line.size());
    return text();
}

std::string output_bounded(const std::string & text, size_t budget) {
    output_buffer b(budget);
    b.add(text.data(), text.size());
    return b.text();
}

output_lines::output_lines(size_t most, output_decode runs) : max(most), decode(runs) {}

std::string output_lines::add(const char * data, size_t size) {
    if (full) {
        return "";
    }
    pending.append(data, size);
    const size_t room = max - shown;
    const size_t end  = pending.rfind('\n');
    // the whole lines that fit, the line that ends the show when the rest
    // does not: a line past the room, or one still open that already fills it
    const size_t fits = end == std::string::npos ? std::string::npos :
                        end < room               ? end :
                        room == 0                ? std::string::npos :
                                                   pending.rfind('\n', room - 1);
    std::string  out  = fits == std::string::npos ? "" : output_utf8(pending.substr(0, fits + 1), decode);
    if (fits != std::string::npos) {
        shown += fits + 1;
        pending.erase(0, fits + 1);
    }
    if ((end != std::string::npos && fits != end) || pending.size() > max - shown) {
        full = true;
        pending.clear();
        out += OUTPUT_SHOWN;
    }
    return out;
}

std::string output_counted(long long n, const char * one, const char * many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
