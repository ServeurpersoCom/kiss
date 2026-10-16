#pragma once
// remote.h: a server an item of a module names, the twin of remote.ts of the
// page: its url, the header that carries its key, its headers, its seconds,
// and the rules every such module holds

#include "engine/engine.h"
#include "httplib.h"

#include <string>

// an item as it names its server
struct remote {
    std::string name;
    std::string url;
    // the header that carries the key, when the item has one
    bool        keyed = false;
    std::string header;
    std::string value;
    // Name: value pairs split by ;
    std::string headers;
    double      timeout = 0;
};

// the item of a module, its key placed as its auth-header says, else as
// fallback says
remote remote_of(const engine_reader & config,
                 const std::string &   module,
                 const std::string &   name,
                 const std::string &   fallback);

// why the items of a module break its rules: a url that opens with http:// or
// https://, an auth-header that is Header or Header: Scheme, headers that are
// Name: value pairs, none of them one the client writes itself nor the header
// of the key; empty when they hold
std::string remote_rules(const engine_reader &                                   config,
                         const std::string &                                     module,
                         const std::function<std::string(const std::string &)> & fallback);

// the headers of an item, then its key
httplib::Headers remote_headers(const remote & r);

// https://host:port/path as httplib takes it: the part it connects to, then
// the path of every request
void remote_split(const std::string & url, std::string & host, std::string & path);
