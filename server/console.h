#pragma once
// console.h: the console of the interactive mode, as on a Cisco router: CLI
// lines typed with Tab and ? run as batches of the administrator, the log
// passing above the line being typed

// opens the console when stdin is a terminal the server runs in the
// foreground of; any other stdin, /dev/null under systemd or a service, or a
// start with & from a shell, leaves the server a daemon
void console_start();
