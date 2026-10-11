#pragma once
// text.h: the words of the engine: the constants of the grammar, the notes a
// line prints and the errors it raises

#include <string>
#include <vector>

using words_t = std::vector<std::string>;

constexpr const char * COMMENT_PREFIX = "!";
constexpr const char * ERROR_PREFIX   = "% ";
constexpr const char * PIPE           = "|";
constexpr const char * RUNNING_CONFIG = "running-config";
constexpr const char * STARTUP_CONFIG = "startup-config";
constexpr const char * ALL            = "all";
constexpr const char * PLATFORMS[]    = { "linux", "macos", "windows" };

// a command that misses words: a lone line lists what may follow instead of
// failing
struct incomplete {};

[[noreturn]] void fail(const std::string & message);

// a line of output that is not a command: a heading, a note, a warning
std::string comment(const std::string & text);

std::string lower(std::string s);
bool        starts_with(const std::string & s, const std::string & prefix);
std::string join(const words_t & words, const char * sep);
words_t     split(const std::string & text, char sep);
std::string trim(const std::string & s);

std::string beyond(const words_t & head, const words_t & rest);

// the word a typed word names: itself, else the only word it starts
std::string pick(const std::string & word, const words_t & words, const std::string & what);
