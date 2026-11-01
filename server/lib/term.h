#pragma once
// term.h: the terminal of the console: raw mode, one byte at a time, its width

// puts the terminal in raw mode, until term_restore; false when stdin is no
// terminal, or one the process runs in the background of
bool term_raw();

// the terminal as term_raw found it, once the console is done with it
void term_restore();

// the next byte typed, -1 once stdin ends
int term_read();

// the columns of the terminal
int term_width();
