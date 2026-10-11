// console.cpp: the console of the interactive mode, as on a Cisco router: a
// line editor with its history, Tab to complete a word and ? to list what may
// follow, the log passing above the line being typed; exit, quit, Ctrl-C and
// Ctrl-D on an empty line stop the server

#include "console.h"

#include "engine/engine.h"
#include "engine/text.h"
#include "log.h"
#include "term.h"

#include <algorithm>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

// the line being edited, written under the lock of the log since every log
// line draws it again: whether it shows, its prompt and its text, the byte
// the cursor stands at, and the row of the cursor below the first one
static bool        g_shown = false;
static std::string g_prompt;
static std::string g_text;
static size_t      g_cursor = 0;
static size_t      g_row    = 0;

// the keys an escape sequence names
enum {
    KEY_UP = 256,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

static const int CTRL_A    = 1;
static const int CTRL_C    = 3;
static const int CTRL_D    = 4;
static const int CTRL_E    = 5;
static const int CTRL_H    = 8;
static const int TAB       = 9;
static const int CTRL_K    = 11;
static const int ENTER     = 13;
static const int CTRL_U    = 21;
static const int CTRL_V    = 22;
static const int ESCAPE    = 27;
static const int BACKSPACE = 127;

// the words that stop the server, the console's own: the engine never reads
// them, so no MCP client stops it
static const words_t QUIT = { "exit", "quit" };

// the prompt names the server, as a router names its host: forge#
static std::string prompt() {
    return engine_get("server hostname").value_or("") + "# ";
}

// the columns of UTF-8 text, one per character
static size_t columns(const std::string & s) {
    size_t n = 0;
    for (unsigned char c : s) {
        n += (c & 0xC0) != 0x80;
    }
    return n;
}

static bool continuation(size_t i) {
    return i < g_text.size() && ((unsigned char) g_text[i] & 0xC0) == 0x80;
}

// the byte of the character before i, and of the one after
static size_t before(size_t i) {
    do {
        i--;
    } while (i > 0 && continuation(i));
    return i;
}

static size_t after(size_t i) {
    do {
        i++;
    } while (continuation(i));
    return i;
}

// from the row of the cursor back to the start of the line, then down to the
// end of the screen
static void clear() {
    if (!g_shown) {
        return;
    }
    std::string out = "\r";
    if (g_row > 0) {
        out += "\x1b[" + std::to_string(g_row) + "A";
    }
    out += "\x1b[J";
    fputs(out.c_str(), stdout);
    fflush(stdout);
}

// the line, then the cursor back to its row and column; a line that fills
// its last row leaves the cursor at the start of the next one
static void draw() {
    if (!g_shown) {
        return;
    }
    const size_t width = (size_t) term_width();
    const size_t total = columns(g_prompt) + columns(g_text);
    const size_t at    = columns(g_prompt) + columns(g_text.substr(0, g_cursor));
    std::string  out   = g_prompt + g_text;
    if (total % width == 0) {
        out += "\n";
    }
    g_row = at / width;
    if (total / width > g_row) {
        out += "\x1b[" + std::to_string(total / width - g_row) + "A";
    }
    out += "\r";
    if (at % width > 0) {
        out += "\x1b[" + std::to_string(at % width) + "C";
    }
    fputs(out.c_str(), stdout);
    fflush(stdout);
}

// a change of the line, drawn again once made
template <typename F> static void edit(F change) {
    log_with([&]() {
        clear();
        change();
        draw();
    });
}

// the line as it stands, then the text below it, the line drawn again under
static void below(const std::string & text) {
    log_with([&]() {
        clear();
        fputs((g_prompt + g_text + "\n" + text + "\n").c_str(), stdout);
        draw();
    });
}

// the line left as typed, the console moving to the next one
static void leave(const std::string & mark) {
    log_with([&]() {
        clear();
        fputs((g_prompt + g_text + mark + "\n").c_str(), stdout);
        fflush(stdout);
        g_shown = false;
    });
}

static void show(const std::string & text) {
    const std::string p = prompt();
    log_with([&]() {
        g_prompt = p;
        g_text   = text;
        g_cursor = text.size();
        g_shown  = true;
        draw();
    });
}

// the next key, its bytes read one at a time: an escape sequence names a key,
// a UTF-8 sequence comes whole
static int key(std::string & bytes) {
    const int c = term_read();
    bytes       = c < 0 ? "" : std::string(1, (char) c);
    if (c == ESCAPE) {
        const int open = term_read();
        if (open != '[' && open != 'O') {
            return ESCAPE;
        }
        std::string params;
        int         last = term_read();
        while (last >= '0' && last <= '9') {
            params += (char) last;
            last = term_read();
        }
        switch (last) {
            case 'A':
                return KEY_UP;
            case 'B':
                return KEY_DOWN;
            case 'C':
                return KEY_RIGHT;
            case 'D':
                return KEY_LEFT;
            case 'H':
                return KEY_HOME;
            case 'F':
                return KEY_END;
            case '~':
                return params == "1" || params == "7" ? KEY_HOME :
                       params == "4" || params == "8" ? KEY_END :
                       params == "3"                  ? KEY_DELETE :
                                                        ESCAPE;
            default:
                return ESCAPE;
        }
    }
    if (c >= 0xC0) {
        const int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        for (int i = 0; i < more; i++) {
            const int d = term_read();
            if (d < 0) {
                return -1;
            }
            bytes += (char) d;
        }
    }
    return c;
}

// the line before the cursor split on its last word: the words the engine
// reads, and the one being typed
static void split_word(std::string & head, std::string & word) {
    const std::string typed = g_text.substr(0, g_cursor);
    const size_t      cut   = typed.find_last_of(" |");
    head                    = cut == std::string::npos ? "" : typed.substr(0, cut + 1);
    word                    = typed.substr(head.size());
}

// the words that may follow which start as the word typed, no <hint>
static words_t matching(const words_t & next, const std::string & word) {
    words_t out;
    for (const std::string & w : next) {
        if (w[0] != '<' && starts_with(lower(w), lower(word))) {
            out.push_back(w);
        }
    }
    return out;
}

// what may follow the words before the cursor: the words of the engine, and
// at the start of the line the words that stop the server
static words_t next_of(const std::string & head) {
    words_t next = engine_next(head);
    if (trim(head).empty()) {
        next.insert(next.end(), QUIT.begin(), QUIT.end());
    }
    return next;
}

// Tab: the word the only match spells, else the start every match shares,
// else the matches listed
static void complete() {
    std::string head, word;
    split_word(head, word);
    const words_t hits = matching(next_of(head), word);
    if (hits.empty()) {
        return;
    }
    std::string common = hits[0];
    for (const std::string & h : hits) {
        size_t n = 0;
        while (n < common.size() && n < h.size() && common[n] == h[n]) {
            n++;
        }
        common.resize(n);
    }
    if (hits.size() == 1) {
        common += " ";
    }
    if (common.size() <= word.size()) {
        below(join(hits, "  "));
        return;
    }
    edit([&]() {
        g_text   = head + common + g_text.substr(g_cursor);
        g_cursor = head.size() + common.size();
    });
}

// ?: what may follow the line, or the words the word typed starts
static void help() {
    std::string head, word;
    split_word(head, word);
    const words_t next  = next_of(head);
    const words_t words = word.empty() ? next : matching(next, word);
    below(words.empty() ? std::string(ERROR_PREFIX) + "nothing may follow" : join(words, "  "));
}

static void editor() {
    log_attach({ clear, draw });
    std::vector<std::string> history;
    size_t                   at = 0;
    std::string              draft;
    show("");
    for (;;) {
        std::string bytes;
        const int   c = key(bytes);
        if (c < 0) {
            return;
        }
        if (c == ENTER || c == '\n') {
            const std::string line = g_text;
            leave("");
            if (std::find(QUIT.begin(), QUIT.end(), lower(trim(line))) != QUIT.end()) {
                raise(SIGTERM);
                return;
            }
            if (!trim(line).empty()) {
                if (history.empty() || history.back() != line) {
                    history.push_back(line);
                }
                log_console(engine_run(line, ENGINE_ADMIN).text + "\n");
            }
            at = history.size();
            show("");
        } else if (c == TAB) {
            complete();
        } else if (c == '?') {
            help();
        } else if (c == CTRL_C || (c == CTRL_D && g_text.empty())) {
            leave(c == CTRL_C ? "^C" : "");
            raise(SIGTERM);
            return;
        } else if (c == KEY_UP && at > 0) {
            if (at == history.size()) {
                draft = g_text;
            }
            at--;
            edit([&]() {
                g_text   = history[at];
                g_cursor = g_text.size();
            });
        } else if (c == KEY_DOWN && at < history.size()) {
            at++;
            edit([&]() {
                g_text   = at == history.size() ? draft : history[at];
                g_cursor = g_text.size();
            });
        } else if (c == KEY_LEFT && g_cursor > 0) {
            edit([]() { g_cursor = before(g_cursor); });
        } else if (c == KEY_RIGHT && g_cursor < g_text.size()) {
            edit([]() { g_cursor = after(g_cursor); });
        } else if (c == KEY_HOME || c == CTRL_A) {
            edit([]() { g_cursor = 0; });
        } else if (c == KEY_END || c == CTRL_E) {
            edit([]() { g_cursor = g_text.size(); });
        } else if ((c == BACKSPACE || c == CTRL_H) && g_cursor > 0) {
            edit([]() {
                const size_t from = before(g_cursor);
                g_text.erase(from, g_cursor - from);
                g_cursor = from;
            });
        } else if ((c == KEY_DELETE || c == CTRL_D) && g_cursor < g_text.size()) {
            edit([]() { g_text.erase(g_cursor, after(g_cursor) - g_cursor); });
        } else if (c == CTRL_U) {
            edit([]() {
                g_text.erase(0, g_cursor);
                g_cursor = 0;
            });
        } else if (c == CTRL_K) {
            edit([]() { g_text.erase(g_cursor); });
        } else if (c == CTRL_V || (c >= ' ' && c != BACKSPACE && c < KEY_UP)) {
            // Ctrl-V types the next key as it is, a ? among them
            if (c == CTRL_V && key(bytes) < 0) {
                return;
            }
            edit([&]() {
                g_text.insert(g_cursor, bytes);
                g_cursor += bytes.size();
            });
        }
    }
}

void console_start() {
    if (term_raw()) {
        std::thread(editor).detach();
    }
}
