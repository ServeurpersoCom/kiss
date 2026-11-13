// term-windows.cpp: the terminal of the console on Windows, a console in
// virtual terminal mode

#if defined(_WIN32)

#    include "term.h"
#    include "utf8.h"

#    include <string>

static HANDLE g_in;
static HANDLE g_out;
static DWORD  g_in_saved;
static DWORD  g_out_saved;

// the UTF-8 bytes of the last character typed not read yet
static std::string g_pending;

static bool g_raw = false;

void term_restore() {
    if (g_raw) {
        SetConsoleMode(g_in, g_in_saved);
        SetConsoleMode(g_out, g_out_saved);
        g_raw = false;
    }
}

// keys as escape sequences, no echo and no line editing; Ctrl-C reaches the
// console as a byte; output reads escape sequences and keeps its newline
// translation
bool term_raw() {
    g_in  = GetStdHandle(STD_INPUT_HANDLE);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(g_in, &g_in_saved) || !GetConsoleMode(g_out, &g_out_saved)) {
        return false;
    }
    if (!SetConsoleMode(g_in, ENABLE_VIRTUAL_TERMINAL_INPUT)) {
        return false;
    }
    if (!SetConsoleMode(g_out, g_out_saved | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        SetConsoleMode(g_in, g_in_saved);
        return false;
    }
    g_raw = true;
    return true;
}

// one UTF-16 unit typed, 0 once the console ends
static wchar_t read_unit() {
    wchar_t c   = 0;
    DWORD   got = 0;
    return ReadConsoleW(g_in, &c, 1, &got, NULL) && got == 1 ? c : 0;
}

// the console handle signals any input, a focus or a mouse event too, which
// reads nothing: those are dropped, a key pressed that types a character
// waits for term_read
bool term_wait(int ms) {
    if (!g_pending.empty()) {
        return true;
    }
    if (WaitForSingleObject(g_in, (DWORD) ms) != WAIT_OBJECT_0) {
        return false;
    }
    for (;;) {
        INPUT_RECORD r;
        DWORD        n = 0;
        if (!PeekConsoleInputW(g_in, &r, 1, &n)) {
            return true;
        }
        if (n == 0) {
            return false;
        }
        if (r.EventType == KEY_EVENT && r.Event.KeyEvent.bKeyDown && r.Event.KeyEvent.uChar.UnicodeChar != 0) {
            return true;
        }
        ReadConsoleInputW(g_in, &r, 1, &n);
    }
}

int term_read() {
    if (g_pending.empty()) {
        wchar_t w[2] = { read_unit(), 0 };
        if (w[0] == 0) {
            return -1;
        }
        int units = 1;
        if (w[0] >= 0xD800 && w[0] <= 0xDBFF) {
            w[1]  = read_unit();
            units = 2;
        }
        char      buf[8];
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, units, buf, sizeof(buf), NULL, NULL);
        if (n <= 0) {
            return -1;
        }
        g_pending.assign(buf, (size_t) n);
    }
    const int c = (unsigned char) g_pending[0];
    g_pending.erase(0, 1);
    return c;
}

// 80 columns, the width of a terminal by default, when it tells none
int term_width() {
    CONSOLE_SCREEN_BUFFER_INFO info;
    return GetConsoleScreenBufferInfo(g_out, &info) ? info.srWindow.Right - info.srWindow.Left + 1 : TERM_WIDTH;
}

#endif
