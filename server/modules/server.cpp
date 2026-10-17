// server.cpp: the server module: where the server listens, its hostname,
// which its tools carry, the key a client sends, the working directory of the
// tools, how much the log says and how much of it show logging keeps

#include "engine/engine.h"
#include "lib/listener.h"
#include "lib/log.h"
#include "lib/tools.h"

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#else
#    include <unistd.h>
#endif

// the host name of the machine, lowercased, cut at its first dot, every
// character outside [a-z0-9] made _, which every model API takes in a tool name
static std::string host_name() {
    char buf[256] = { 0 };
#if defined(_WIN32)
    DWORD size = sizeof(buf);
    GetComputerNameA(buf, &size);
#else
    gethostname(buf, sizeof(buf) - 1);
#endif
    std::string name;
    for (const char * p = buf; *p && *p != '.'; p++) {
        const char c = (char) tolower((unsigned char) *p);
        name += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : '_';
    }
    return name;
}

// the directory the server starts in
static std::string start_directory() {
    return std::filesystem::current_path().u8string();
}

static bool tool_word(const std::string & s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static std::string validate(const engine_reader & config) {
    const std::string name = config.get("server hostname").value_or("");
    if (!tool_word(name)) {
        return "server hostname \"" + name + "\" takes a-z, 0-9 and _ only, as a tool name does";
    }
    for (const tool & t : tools_list(config)) {
        if (tools_served(name, t).size() > TOOL_NAME_MAX) {
            return "server hostname \"" + name + "\" makes " + tools_served(name, t) + " longer than " +
                   std::to_string(TOOL_NAME_MAX);
        }
    }
    const auto host = config.get("server listen-address");
    const auto port = config.get("server listen-port");
    if (!host || !port) {
        return "server listen-address or server listen-port is not set";
    }
    if (!listener_loopback(*host) && !config.get("server key")) {
        return "listening on " + *host + " needs a key -> set server key";
    }
    std::string why;
    if (!listener_bindable(*host, std::stoi(*port), why)) {
        return "cannot listen on " + *host + " port " + *port + ": " + why;
    }
    const std::string cwd = config.get("server working-directory").value_or("");
    std::error_code   ec;
    if (!std::filesystem::is_directory(std::filesystem::u8path(cwd), ec)) {
        return "server working-directory " + cwd + " is not a directory";
    }
    return "";
}

static void apply(const engine_reader & config) {
    const std::string level = config.get("server log-level").value_or("");
    for (int l = LOG_ERROR; l <= LOG_DEBUG; l++) {
        if (level == LOG_LEVELS[l]) {
            log_level_set((log_level) l);
        }
    }
    log_buffer_set((size_t) std::stoull(config.get("server log-buffer").value_or("0")));
    listener_move(*config.get("server listen-address"), std::stoi(*config.get("server listen-port")));
}

static engine_module make_server() {
    engine_key address("listen-address", ENGINE_STRING);
    address.reset = "127.0.0.1";

    engine_key port("listen-port", ENGINE_NUMBER);
    port.min     = 1;
    port.max     = 65535;
    port.integer = true;
    port.reset   = "8089";

    engine_key hostname("hostname", ENGINE_STRING);
    hostname.reset = host_name();

    engine_key cwd("working-directory", ENGINE_STRING);
    cwd.reset = start_directory();

    engine_key level("log-level", ENGINE_ENUM);
    level.values = { std::begin(LOG_LEVELS), std::end(LOG_LEVELS) };
    level.reset  = "debugging";

    // the bytes of the last lines of the log show logging keeps, 0 none
    engine_key buffer("log-buffer", ENGINE_NUMBER);
    buffer.min     = 0;
    buffer.integer = true;
    buffer.reset   = "65536";

    engine_module m;
    m.name = "server";
    m.keys.push_back(address);
    m.keys.push_back(port);
    m.keys.push_back(hostname);
    m.keys.push_back(engine_key("key", ENGINE_SECRET));
    m.keys.push_back(cwd);
    m.keys.push_back(level);
    m.keys.push_back(buffer);
    m.validate = validate;
    m.apply    = apply;
    return m;
}

ENGINE_MODULE(make_server);
