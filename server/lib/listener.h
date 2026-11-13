#pragma once
// listener.h: the socket the server listens on, which a change of server
// listen-address or server listen-port moves at once

#include <string>

// whether an address names this machine only: 127.0.0.0/8, ::1, localhost
bool listener_loopback(const std::string & host);

// moves the listener to host and port, bound before the batch that moves it
// applies: the address served until the new one binds, or the port let go
// first when the two overlap; why it cannot, the listener then on the address
// it served
std::string listener_prepare(const std::string & host, int port);

// the address the server listens on now
void listener_address(std::string & host, int & port);

// the listener the server runs now, counted from the start: a stream that
// outlives no listener ends once this changes
unsigned listener_epoch();

// listens on the address of the running configuration, then on each one a
// batch moves it to, until listener_stop; false when the first address cannot
// be listened on. The requests in flight end with quit_all
bool listener_run();

// stops the listener, from a signal handler as well
void listener_stop();
