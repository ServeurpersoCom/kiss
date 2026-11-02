#pragma once
// http.h: the routes of the server and the checks every request passes

#include "httplib.h"

#include <string>

// the file the site configuration of the page lives in
void http_open(const std::string & site);

// the routes of a server listening on host and port, its requests checked
// against that address
void http_routes(httplib::Server & server, const std::string & host, int port);
