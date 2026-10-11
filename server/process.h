#pragma once
// process.h: a command run as one process group: the script on its stdin,
// stdout and stderr merged on one pipe, the whole group ended on a timeout, a
// cancel, or once the command exits, so a call leaves nothing running

#include <functional>
#include <string>
#include <vector>

struct process_result {
    std::string output;
    size_t      total     = 0;
    bool        truncated = false;
    int         exit_code = -1;
    bool        started   = false;
    bool        timed_out = false;
    bool        cancelled = false;
    // why the command did not start
    std::string error;
};

// the arguments of a command line: split on blanks, a double quote grouping
// until the next one
std::vector<std::string> process_argv(const std::string & line);

// the program a command line starts, as a path, empty when it is found nowhere
std::string process_find(const std::string & line);

// runs the command line in cwd with input on its stdin; tick is called every
// second, false meaning the client is gone; budget bounds the output kept
process_result process_run(const std::string &           line,
                           const std::string &           cwd,
                           const std::string &           input,
                           int                           timeout_s,
                           size_t                        budget,
                           const std::function<bool()> & tick);
