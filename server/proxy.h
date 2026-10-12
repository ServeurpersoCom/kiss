#pragma once
// proxy.h: the relay of the servers the proxy module names: a request to
// proxy/<name>/<rest> goes to the URL of that server then rest, its query
// kept, as a new request of the same method and body, its stream coming back
// as it arrives

#include "httplib.h"

#include <string>

// the request relayed to the route it names, rest after its url, the answer
// streamed back
void proxy(const httplib::Request & req, httplib::Response & res, const std::string & route, const std::string & rest);
