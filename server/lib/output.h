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
    // tail, as valid UTF-8, the three within the budget
    std::string text() const;

    // the text with a closing line after the output, on a line of its own,
    // within the budget as the output is
    std::string closed(const std::string & line);

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

// a text as a tool returns it within a budget: whole when it fits, else its
// head, the line that counts the bytes hidden, and its tail, the three within
// the budget, so a text cut already comes back as it is
std::string output_bounded(const std::string & text, size_t budget);

// the line that ends what an output shows while it runs once it shows max
// bytes, the rest showing once the call ends
constexpr const char * OUTPUT_SHOWN = "The rest of the output shows once the call ends\n";

// the whole lines of an output as they come, for whoever shows it while it
// runs: at most max bytes of them, then the line OUTPUT_SHOWN, then nothing
struct output_lines {
    explicit output_lines(size_t max, output_decode decode = nullptr);

    // the lines the bytes complete, as valid UTF-8, empty when none
    std::string add(const char * data, size_t size);

    std::string add(const std::string & text) { return add(text.data(), text.size()); }

  private:
    size_t        max;
    output_decode decode;
    // the bytes after the last line given
    std::string   pending;
    size_t        shown = 0;
    bool          full  = false;
};

// a count with its noun as a result tells it: 1 line, 3 lines
std::string output_counted(long long n, const char * one, const char * many);
