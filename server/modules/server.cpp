// server.cpp: the server module: where the server listens, the name its
// tools carry, the key a client sends, the working directory of the tools and
// how much the log says

#include "engine/engine.h"
#include "listener.h"
#include "log.h"
#include "tools.h"

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
    static const std::string dir = std::filesystem::current_path().u8string();
    return dir;
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
    const std::string name = config.get("server name").value_or("");
    if (!tool_word(name)) {
        return "server name \"" + name + "\" takes a-z, 0-9 and _ only, as a tool name does";
    }
    for (const tool & t : tools_list(config)) {
        if (name.size() + 1 + t.suffix.size() > TOOL_NAME_MAX) {
            return "server name \"" + name + "\" makes " + name + "_" + t.suffix + " longer than " +
                   std::to_string(TOOL_NAME_MAX);
        }
    }
    const auto host = config.get("server host");
    const auto port = config.get("server port");
    if (!host || !port) {
        return "server host and server port are set";
    }
    if (!listener_loopback(*host) && !config.get("server key")) {
        return "listening on " + *host + " requires server key";
    }
    std::string why;
    if (!listener_bindable(*host, std::stoi(*port), why)) {
        return "cannot listen on " + *host + " port " + *port + ": " + why;
    }
    const std::string cwd = config.get("server cwd").value_or("");
    std::error_code   ec;
    if (!std::filesystem::is_directory(std::filesystem::u8path(cwd), ec)) {
        return "server cwd " + cwd + " is no directory";
    }
    return "";
}

static void apply(const engine_reader & config) {
    log_verbosity(config.get("server log") == std::string("verbose"));
    listener_move(*config.get("server host"), std::stoi(*config.get("server port")));
}

static engine_module make_server() {
    engine_key port("port", ENGINE_NUMBER);
    port.min     = 1;
    port.max     = 65535;
    port.integer = true;

    engine_key name("name", ENGINE_STRING);
    name.fallback = host_name;

    engine_key cwd("cwd", ENGINE_STRING);
    cwd.fallback = start_directory;

    engine_key log("log", ENGINE_ENUM);
    log.values = { "info", "verbose" };

    engine_module m;
    m.name = "server";
    m.keys.push_back(engine_key("host", ENGINE_STRING));
    m.keys.push_back(port);
    m.keys.push_back(name);
    m.keys.push_back(engine_key("key", ENGINE_SECRET));
    m.keys.push_back(cwd);
    m.keys.push_back(log);
    m.validate = validate;
    m.apply    = apply;
    return m;
}

ENGINE_MODULE(make_server);
