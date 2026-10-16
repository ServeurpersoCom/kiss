#pragma once
// listener.h: the socket the server listens on, which a change of server listen-address
// or server listen-port moves at once

#include <string>

// whether an address names this machine only: 127.0.0.0/8, ::1, localhost
bool listener_loopback(const std::string & host);

// whether the server can listen on host and port, true where it listens now;
// why it cannot otherwise
bool listener_bindable(const std::string & host, int port, std::string & why);

// the address to listen on: a change moves the listener
void listener_move(const std::string & host, int port);

// the address the server listens on now
void listener_address(std::string & host, int & port);

// the listener the server runs now, counted from the start: a stream that
// outlives no listener ends once this changes
unsigned listener_epoch();

// listens on the address of the running configuration, moved when it changes,
// until listener_stop; false when an address cannot be listened on
bool listener_run();

void listener_stop();
