#pragma once
// console.h: the console of the interactive mode, as on a Cisco router: CLI
// lines typed with Tab and ? run as batches of the administrator, the log
// passing above the line being typed

// opens the console when stdin is a terminal; any other stdin, /dev/null
// under systemd or a service, leaves the server a daemon
void console_start();
