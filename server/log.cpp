// log.cpp: the log of the server

#include "log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

static std::mutex        g_mutex;
static std::atomic<bool> g_verbose{ false };

static void line(const char * module, const char * fmt, va_list args) {
    std::lock_guard<std::mutex> lock(g_mutex);
    fprintf(stderr, "[%s] ", module);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    fflush(stderr);
}

void log_info(const char * module, const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    line(module, fmt, args);
    va_end(args);
}

void log_verbose(const char * module, const char * fmt, ...) {
    if (!g_verbose) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    line(module, fmt, args);
    va_end(args);
}

void log_verbosity(bool verbose) {
    g_verbose = verbose;
}

void log_console(const std::string & text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    fputs(text.c_str(), stdout);
    fflush(stdout);
}
