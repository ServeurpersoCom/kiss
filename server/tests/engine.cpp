// engine.cpp: the laws of the engine, run as the build ends, twins of those of
// the page: no takes back what the running configuration sets, the factory
// value applying again, then what the factory configuration holds, as a no
// line of its own that write saves and the boot reads back; an item taken back
// goes, a set gives it back, an erase of the running configuration brings the
// factory configuration back whole, and a no line that takes back nothing
// stays out at boot; an MCP client opens a guarded register only once the user
// lets it, whatever line opens it, while the administrator opens it at once;
// a key of the administrator, where commands and the tools on files run, moves
// by no line of an MCP client, the administrator moving it at once; the mask of
// a secret is no value; a dotted key, no, and show running-config all answer as
// on the page

#include "engine/engine.h"

#include "engine/text.h"
#include "factory.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

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
    laws_factory();
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

    // the guard: what an MCP client asks before it applies, by stored key, on
    // the factory configuration
    admin("erase running-config");
    const engine_typed erase_yes = {
        { "erase running-config", "true" }
    };
    const auto asks = [](const engine_result & r) {
        words_t out;
        for (const engine_ask & a : r.asks) {
            out.push_back(a.key + (a.yes ? " yes: " : ": ") + a.question);
        }
        return join(out, "\n");
    };
    const auto use = [](const std::string & tool) {
        return engine_get("tools use", tool).value_or("");
    };
    law(asks(engine_run("set tools use client_config on", ENGINE_MCP)) ==
            "tools use client_config yes: set tools use client_config on",
        "an MCP client that opens a tool asks the user");
    law(use("client_config") == "consent", "a tool the user has not let stays closed");
    law(engine_run("set tools use client_config on", ENGINE_MCP,
                   engine_typed{
                       { "tools use client_config", "true" }
    })
                .text == "+ set tools use client_config on",
        "a tool the user lets opens");
    law(engine_run("set tools use client_config off", ENGINE_MCP).text ==
            "- set tools use client_config on\n+ set tools use client_config off",
        "an MCP client closes a tool without asking");
    law(admin("set tools use client_config on") ==
            "- set tools use client_config off\n+ set tools use client_config on",
        "the administrator opens a tool at once");
    admin("write");
    admin("set tools use client_config off");
    law(asks(engine_run("copy startup-config running-config", ENGINE_MCP)) ==
            "tools use client_config yes: set tools use client_config on",
        "a copy that opens a tool asks the user");
    law(asks(engine_run("no tools use client_config", ENGINE_MCP)) ==
            "tools use client_config yes: set tools use client_config consent",
        "a no that opens a tool asks the user");
    law(use("client_config") == "off", "nothing applies while the user has not let it");
    admin("set tools use bash consent");
    law(asks(engine_run("erase running-config", ENGINE_MCP, erase_yes)) ==
            "tools use bash yes: set tools use bash on\ntools use client_config yes: set tools use client_config "
            "consent",
        "an erase the user says yes to asks the user for each tool it opens, the factory turning them on");
    law(use("bash") == "consent", "the erase applies nothing while the user has not let the tool");

    // the keys of the administrator: where a shell runs, the directory of the
    // server
    const std::string refused = "% shells command bash is the administrator's -> server.conf or the console";
    const auto        mcp     = [](const std::string & lines, const engine_typed & typed = {}) {
        return engine_run(lines, ENGINE_MCP, typed).text;
    };
    const auto bash = [] {
        return engine_get("shells command", "bash").value_or("");
    };
    law(mcp("set shells command bash \"sh -s\"") == refused, "an MCP client sets no shell");
    law(mcp("no shells command bash") == refused, "an MCP client takes back no shell");
    law(mcp("no shells bash") == refused, "an MCP client drops no shell");
    law(mcp("set shells command fresh \"sh -s\"") ==
            "% shells command fresh is the administrator's -> server.conf or the console",
        "an MCP client adds no shell");
    law(mcp("set server working-directory /") ==
            "% server working-directory is the administrator's -> server.conf or the console",
        "an MCP client moves not the directory of the server");
    law(mcp("set files shell bash") == "% files shell is the administrator's -> server.conf or the console",
        "an MCP client moves not where the tools on files act");
    law(mcp("set shells timeout bash 9") == "- set shells timeout bash 290\n+ set shells timeout bash 9",
        "an MCP client sets what is not the administrator's");
    admin("no shells timeout bash");
    law(admin("no shells command bash") == "- set shells command bash \"bash -s\"",
        "the administrator takes back a shell at once");
    admin("write");
    law(admin("set shells command bash \"bash -s\"") == "+ set shells command bash \"bash -s\"",
        "the administrator sets a shell at once");
    law(mcp("copy startup-config running-config") == refused, "a copy of an MCP client moves no shell");
    admin("copy startup-config running-config");
    law(mcp("erase running-config", erase_yes) == refused, "an erase of an MCP client moves no shell");
    law(bash().empty(), "nothing applies of a batch the administrator's keys refuse");
    const engine_typed erase_start = {
        { "erase startup-config", "true" }
    };
    const engine_typed save_yes = {
        { "copy running-config startup-config", "true" }
    };
    law(mcp("erase startup-config", erase_start) == refused,
        "an erase of the startup-config by an MCP client moves no shell of the next start");
    law(mcp("write", save_yes).find("copied") != std::string::npos,
        "a save by an MCP client that keeps the administrator's keys writes");
    admin("set shells command bash \"bash -s\"");
    law(mcp("write", save_yes) == refused, "a save by an MCP client moves no shell of the next start");
    law(read(conf).find("no shells command bash") != std::string::npos, "server.conf keeps the administrator's keys");

    // the mask of a secret is no value, as on the page: a line pasted back from
    // show running-config keeps the secret it masks
    admin("set server key sesame");
    law(admin("set server key <removed>") ==
            "% server key: \"<removed>\" masks a secret, it is not a value -> the line without a value asks the "
            "user",
        "the mask of a secret is no value");
    law(engine_get("server key") == std::optional<std::string>("<removed>") &&
            engine_run("show running-config | include key", ENGINE_ADMIN).text.find("server key is set") !=
                std::string::npos,
        "the secret the mask hides stays set");

    // the words of the page, twins of its laws
    law(admin("set chat.model x") == "% unknown key \"chat.model\", did you mean chat model",
        "a key written with dots names the key with blanks");
    law(admin("set tools use nowhere on") == "% unknown tools \"nowhere\"",
        "a set names no item its sources do not serve, as on the page");
    law(admin("set tools use client_confi on") == "% unknown tools \"client_confi\", did you mean client_config",
        "a set near an item served names it");
    const auto offers = [](const std::string & line, const std::string & word) {
        const words_t all = engine_suggest(line);
        return std::find(all.begin(), all.end(), word) != all.end();
    };
    admin("no tools use client_config");
    bool served = false;
    for (int i = 0; i < 100 && !served; i++) {
        served = offers("set tools use ", "client_config");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    law(served && !offers("no tools use ", "client_config"),
        "no completes with the items set, set with the items served too");
    law(admin("show running-config all").find("! KiSS") != std::string::npos,
        "show running-config all lists the items of a module by source, as show <module> does");

    fs::remove_all(root);
    printf("[Laws] engine_test: %zu laws, %zu broken\n", g_laws, g_fails);
    engine_exit(g_fails ? 1 : 0);
}
