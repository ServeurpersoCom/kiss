// remote.cpp: a server an item of a module names, and the rules of such items

#include "lib/remote.h"

#include "engine/keychain.h"
#include "engine/text.h"

// the headers a client writes itself, which an item never sets
static const char * const OWN_HEADERS[] = { "host", "content-length", "transfer-encoding", "connection" };

// why the headers of an item do not read, Name: value; Name: value, none of
// them the header of its key, empty when they do
static std::string header_problem(const std::string & headers, const std::string & keyed, const std::string & module) {
    for (const std::string & pair : split(headers, ';')) {
        const std::string h     = trim(pair);
        const size_t      colon = h.find(':');
        if (h.empty()) {
            continue;
        }
        if (colon == std::string::npos || trim(h.substr(0, colon)).empty()) {
            return "\"" + h + "\" is no Name: value";
        }
        const std::string name = lower(trim(h.substr(0, colon)));
        for (const char * own : OWN_HEADERS) {
            if (name == own) {
                return name + " is written by the client";
            }
        }
        if (name == lower(keyed)) {
            return trim(h.substr(0, colon)) + " is the header " + module + " auth-header writes";
        }
    }
    return "";
}

std::string remote_rules(const engine_reader &                                   config,
                         const std::string &                                     module,
                         const std::function<std::string(const std::string &)> & fallback) {
    for (const std::string & name : config.names(module)) {
        const auto url = config.get(module + " url", name);
        if (!url) {
            return module + " url " + name + " is not set";
        }
        if (!starts_with(*url, "http://") && !starts_with(*url, "https://")) {
            return module + " url " + name + " " + *url + " opens with http:// or https://";
        }
        const std::string auth = config.get(module + " auth-header", name).value_or(fallback(name));
        std::string       keyed, scheme;
        if (!placement(auth, keyed, scheme)) {
            return module + " auth-header " + name + ": \"" + auth + "\" is not Header or Header: Scheme";
        }
        const std::string problem = header_problem(config.get(module + " headers", name).value_or(""), keyed, module);
        if (!problem.empty()) {
            return module + " headers " + name + ": " + problem;
        }
    }
    return "";
}

remote remote_of(const engine_reader & config,
                 const std::string &   module,
                 const std::string &   name,
                 const std::string &   fallback) {
    remote r;
    r.name    = name;
    r.url     = config.get(module + " url", name).value_or("");
    r.keyed   = credential(config, config.get(module + " auth-header", name).value_or(fallback), module, name, r.header,
                           r.value);
    r.headers = config.get(module + " headers", name).value_or("");
    r.timeout = std::stod(config.get(module + " timeout", name).value_or("0"));
    return r;
}

httplib::Headers remote_headers(const remote & r) {
    httplib::Headers headers;
    for (const std::string & pair : split(r.headers, ';')) {
        const size_t colon = pair.find(':');
        if (colon != std::string::npos) {
            headers.emplace(trim(pair.substr(0, colon)), trim(pair.substr(colon + 1)));
        }
    }
    if (r.keyed) {
        headers.erase(r.header);
        headers.emplace(r.header, r.value);
    }
    return headers;
}

void remote_split(const std::string & url, std::string & host, std::string & path) {
    const size_t slash = url.find('/', url.find("://") + 3);
    host               = url.substr(0, slash);
    path               = slash == std::string::npos ? "/" : url.substr(slash);
}
