// term-posix.cpp: the terminal of the console on Linux and macOS, termios

#if !defined(_WIN32)

#    include "term.h"

#    include <sys/ioctl.h>
#    include <termios.h>
#    include <unistd.h>

#    include <cstdlib>

static termios g_saved;

static void restore() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
}

// bytes as typed, no echo and no line discipline; Ctrl-C reaches the console
// as a byte, output keeps its newline translation
bool term_raw() {
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &g_saved) != 0) {
        return false;
    }
    termios raw = g_saved;
    raw.c_iflag &= ~(tcflag_t) (BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~(tcflag_t) (ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return false;
    }
    atexit(restore);
    return true;
}

int term_read() {
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

// 80 columns, the width of a terminal by default, when it tells none
int term_width() {
    winsize ws;
    return ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 ? ws.ws_col : 80;
}

#endif
