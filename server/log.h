#pragma once
// log.h: the log of the server, one line per event, [Module] Line, on stderr;
// the console of the interactive mode prints between its lines

#include <functional>
#include <string>

void log_info(const char * module, const char * fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

// a line only the verbose log prints: every request, every change
void log_verbose(const char * module, const char * fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void log_verbosity(bool verbose);

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
