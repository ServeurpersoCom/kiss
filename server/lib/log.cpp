// log.cpp: the log of the server

#include "lib/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>

static std::mutex       g_mutex;
static std::atomic<int> g_level{ LOG_DEBUG };
static log_editor       g_editor{ nullptr, nullptr };

// the last lines, their bytes within g_buffer; until the configuration sets
// it, every line stays, so show logging holds the start of the server
static std::deque<std::string> g_kept;
static size_t                  g_kept_bytes = 0;
static size_t                  g_buffer     = SIZE_MAX;

static void trim_kept() {
    while (!g_kept.empty() && g_kept_bytes > g_buffer) {
        g_kept_bytes -= g_kept.front().size();
        g_kept.pop_front();
    }
}

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

static void line(log_level level, const char * module, const char * fmt, va_list args) {
    if (level > g_level) {
        return;
    }
    va_list copy;
    va_copy(copy, args);
    const int   n = vsnprintf(nullptr, 0, fmt, copy);
    std::string text(n > 0 ? (size_t) n : 0, '\0');
    va_end(copy);
    vsnprintf(&text[0], text.size() + 1, fmt, args);
    text = "[" + std::string(module) + "] " + text + "\n";

    std::lock_guard<std::mutex> lock(g_mutex);
    clear();
    fputs(text.c_str(), stderr);
    fflush(stderr);
    draw();
    g_kept.push_back(text);
    g_kept_bytes += text.size();
    trim_kept();
}

#define LOG_AT(level)               \
    va_list args;                   \
    va_start(args, fmt);            \
    line(level, module, fmt, args); \
    va_end(args)

void log_error(const char * module, const char * fmt, ...) {
    LOG_AT(LOG_ERROR);
}

void log_info(const char * module, const char * fmt, ...) {
    LOG_AT(LOG_INFO);
}

void log_debug(const char * module, const char * fmt, ...) {
    LOG_AT(LOG_DEBUG);
}

void log_level_set(log_level level) {
    g_level = level;
}

void log_buffer_set(size_t bytes) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_buffer = bytes;
    trim_kept();
}

std::string log_buffered() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string                 out;
    for (const std::string & l : g_kept) {
        out += l;
    }
    return out;
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
