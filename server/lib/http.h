#pragma once
// http.h: the routes of the server and the checks every request passes

#include "httplib.h"

#include <string>

// the file the site configuration of the page lives in
void http_open(const std::string & site);

void http_routes(httplib::Server & server);
