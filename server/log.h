#pragma once
// log.h: the log of the server, one line per event, [Module] Line, on stderr;
// the console of the interactive mode prints between its lines

#include <functional>
#include <string>

// the levels of a line, each printing those before it: what fails, what
// happens, and the detail of it
enum log_level { LOG_ERROR, LOG_INFO, LOG_DEBUG };

// their names, as server log takes them
constexpr const char * LOG_LEVELS[] = { "errors", "informational", "debugging" };

#if defined(__GNUC__)
#    define LOG_FORMAT __attribute__((format(printf, 2, 3)))
#else
#    define LOG_FORMAT
#endif

void log_error(const char * module, const char * fmt, ...) LOG_FORMAT;
void log_info(const char * module, const char * fmt, ...) LOG_FORMAT;
void log_debug(const char * module, const char * fmt, ...) LOG_FORMAT;

// the most detailed level the log prints
void log_level_set(log_level level);

// the bytes of the last lines the log keeps for show logging, as IOS keeps
// them with logging buffered; 0 keeps none
void log_buffer_set(size_t bytes);

// the lines the log keeps, oldest first
std::string log_buffered();

// text of the console, on stdout, never in the middle of a log line, above the
// line the console edits
void log_console(const std::string & text);

// the line the console edits: cleared before anything prints, drawn again
// after, so the log and the outputs pass above it
struct log_editor {
    void (*clear)();
    void (*draw)();
};

void log_attach(log_editor editor);

// runs under the lock of the log, so a change of the edited line never cuts a
// log line
void log_with(const std::function<void()> & f);
