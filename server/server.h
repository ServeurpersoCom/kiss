#pragma once
// server.h: what the modules reach of the running server

#include <string>

// whether an address names this machine only: 127.0.0.0/8, ::1, localhost
bool server_loopback(const std::string & host);

// whether the server can listen on host and port, true where it listens now;
// why it cannot otherwise
bool server_bindable(const std::string & host, int port, std::string & why);

// the address the server listens on: a change moves the listener
void server_listen(const std::string & host, int port);
