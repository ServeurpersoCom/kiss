// job.cpp: what the job layer shares between the platforms

#include "job.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
static const char         PATH_SEPARATOR = ';';
static const char * const EXECUTABLE[]   = { "", ".exe", ".com", ".bat", ".cmd" };
#else
static const char         PATH_SEPARATOR = ':';
static const char * const EXECUTABLE[]   = { "" };
#endif

std::vector<std::string> job_argv(const std::string & line) {
    std::vector<std::string> argv;
    std::string              word;
    bool                     quoted = false, open = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            open   = true;
        } else if ((c == ' ' || c == '\t') && !quoted) {
            if (open) {
                argv.push_back(word);
            }
            word.clear();
            open = false;
        } else {
            word += c;
            open = true;
        }
    }
    if (open) {
        argv.push_back(word);
    }
    return argv;
}

static bool runnable(const std::filesystem::path & p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

std::string job_find(const std::string & line) {
    const std::vector<std::string> argv = job_argv(line);
    if (argv.empty()) {
        return "";
    }
    const std::filesystem::path program = std::filesystem::u8path(argv[0]);
    if (program.has_parent_path()) {
        return runnable(program) ? program.u8string() : "";
    }
    const char * path = getenv("PATH");
    std::string  dirs = path ? path : "";
    size_t       from = 0;
    while (from <= dirs.size()) {
        const size_t      to  = dirs.find(PATH_SEPARATOR, from);
        const std::string dir = dirs.substr(from, to == std::string::npos ? std::string::npos : to - from);
        for (const char * ext : EXECUTABLE) {
            const std::filesystem::path candidate = std::filesystem::u8path(dir) / (argv[0] + ext);
            if (!dir.empty() && runnable(candidate)) {
                return candidate.u8string();
            }
        }
        if (to == std::string::npos) {
            break;
        }
        from = to + 1;
    }
    return "";
}
