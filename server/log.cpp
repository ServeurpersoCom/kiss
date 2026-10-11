// log.cpp: the log of the server

#include "log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

static std::mutex        g_mutex;
static std::atomic<bool> g_verbose{ false };
static log_editor        g_editor{ nullptr, nullptr };

static void clear() {
    if (g_editor.clear) {
        g_editor.clear();
    }
}

static void draw() {
    if (g_editor.draw) {
        g_editor.draw();
    }
}

static void line(const char * module, const char * fmt, va_list args) {
    std::lock_guard<std::mutex> lock(g_mutex);
    clear();
    fprintf(stderr, "[%s] ", module);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    fflush(stderr);
    draw();
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
    clear();
    fputs(text.c_str(), stdout);
    fflush(stdout);
    draw();
}

void log_attach(log_editor editor) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_editor = editor;
}

void log_with(const std::function<void()> & f) {
    std::lock_guard<std::mutex> lock(g_mutex);
    f();
}
