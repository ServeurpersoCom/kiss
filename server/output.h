#pragma once
// output.h: the output of a process as a tool returns it: the head and the
// tail within a budget, the bytes between them counted, every byte read so
// the process never blocks on a full pipe, and valid UTF-8 in the end

#include <cstddef>
#include <string>

struct output_buffer {
    // the head keeps the first half of the budget, the tail the last half
    explicit output_buffer(size_t budget);

    void add(const char * data, size_t size);

    // the head, a line that counts the bytes left out when there are, and the
    // tail, as valid UTF-8
    std::string text() const;

    size_t total() const { return count; }

    bool truncated() const { return count > head_max + tail.size(); }

  private:
    size_t      head_max;
    size_t      tail_max;
    std::string head;
    // the last bytes read, a ring that starts at start once full
    std::string tail;
    size_t      start = 0;
    size_t      count = 0;
};

// the text with every run of invalid UTF-8 bytes replaced by one U+FFFD, so
// binary output stays close to the size read
std::string output_utf8(const std::string & text);
