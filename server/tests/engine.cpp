// engine.cpp: the laws of no over the factory configuration, run as the build
// ends, twins of those of the page: no takes back what the running
// configuration sets, the factory value applying again, then what the factory
// configuration holds, as a no line of its own that write saves and the boot
// reads back; an item taken back goes, a set gives it back, an erase of the
// running configuration brings the factory configuration back whole, and a no
// line that takes back nothing stays out at boot

#include "engine/engine.h"

#include "server/server.conf.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] engine_test broken: %s\n", what.c_str());
    }
}

// what a batch of the administrator answers
static std::string admin(const std::string & lines) {
    return engine_run(lines, ENGINE_ADMIN).text;
}

// whether the running configuration holds an item of shells
static bool shell(const std::string & name) {
    bool found = false;
    engine_with([&](const engine_reader & config) {
        for (const std::string & n : config.names("shells")) {
            found = found || n == name;
        }
    });
    return found;
}

static std::string read(const fs::path & p) {
    std::ifstream     f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("engine_test." + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const fs::path conf = root / "server.conf";
    std::ofstream(conf) << "set shells command zsh \"bash -s\"\n";
    engine_factory(std::string(reinterpret_cast<const char *>(server_server_conf), server_server_conf_len));
    engine_start(conf.u8string(), "test");

    law(admin("no shells command zsh") == "- set shells command zsh \"bash -s\"\n+ set shells command zsh \"zsh -s\"",
        "no takes back what the running configuration sets, the factory value applying again");
    law(admin("no shells command zsh") == "- set shells command zsh \"zsh -s\"",
        "no takes back what the factory configuration holds");
    law(!shell("zsh"), "an item whose every key is taken back goes");
    law(admin("no shells command zsh") == "% unknown shells \"zsh\"", "an item taken back is no longer named");
    law(admin("no shells powershell").find("- set shells command powershell") == 0,
        "no takes back an item the factory configuration holds, every key of it");
    law(!shell("powershell"), "an item taken back whole goes");
    law(admin("show running-config") == "no shells command powershell\nno shells command zsh",
        "show running-config writes what it takes back as no lines");

    law(admin("write").find("copied") != std::string::npos, "write saves the no lines");
    law(read(conf) == "no shells command powershell\nno shells command zsh\n", "server.conf holds the no lines");
    engine_start(conf.u8string(), "test");
    law(!shell("zsh") && !shell("powershell") && shell("bash"), "the boot reads the no lines back");
    law(admin("show diff running-config startup-config") == "! no difference",
        "the running configuration booted is the startup-config");

    law(admin("set shells command zsh \"zsh -s\"") == "+ set shells command zsh \"zsh -s\"",
        "a set gives back an item taken back");
    law(admin("show running-config") == "no shells command powershell\nset shells command zsh \"zsh -s\"",
        "a set replaces the no line");

    law(admin("erase running-config").find("+ set shells command powershell") != std::string::npos,
        "an erase of the running configuration brings the factory configuration back");
    law(shell("zsh") && shell("powershell"), "every item of the factory configuration is back");
    law(admin("show running-config") == "! every key is at its default", "nothing is taken back after an erase");

    std::ofstream(conf) << "no shells command nowhere\nno shells command pwsh\n";
    engine_start(conf.u8string(), "test");
    law(!shell("pwsh") && shell("zsh"), "the boot takes back what server.conf names");
    law(admin("show running-config") == "no shells command pwsh", "a no line that takes back nothing stays out");

    fs::remove_all(root);
    printf("[Laws] engine_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
