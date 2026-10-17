#pragma once
// output.h: the output of a process as a tool returns it: the head and the
// tail within a budget, the bytes between them counted, every byte read so
// the process never blocks on a full pipe, and valid UTF-8 in the end

#include <cstddef>
#include <string>

// the text a run of bytes that is not UTF-8 stands for, empty when it stands
// for none
typedef std::string (*output_decode)(const std::string & run);

struct output_buffer {
    // the head keeps the first half of the budget, the tail the last half;
    // decode reads the runs that are not UTF-8, none when null
    explicit output_buffer(size_t budget, output_decode decode = nullptr);

    void add(const char * data, size_t size);

    // the head, a line that counts the bytes hidden when there are, and the
    // tail, as valid UTF-8
    std::string text() const;

    size_t total() const { return count; }

    bool truncated() const { return count > head_max + tail.size(); }

  private:
    size_t        head_max;
    size_t        tail_max;
    output_decode decode;
    std::string   head;
    // the last bytes read, a ring that starts at start once full
    std::string   tail;
    size_t        start = 0;
    size_t        count = 0;
};

// a count with its noun as a result tells it: 1 line, 3 lines
std::string output_counted(long long n, const char * one, const char * many);
