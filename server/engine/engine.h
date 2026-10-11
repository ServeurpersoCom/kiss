#pragma once
// engine.h: the CLI of the server, the twin of the engine of the page
//
// A key is two fixed words, a module then a key; a key of a collection takes
// the item name next, and the value comes last. Each word may be any prefix
// that names one word only, the way Cisco IOS reads a line. A line may open
// with a platform, linux, macos or windows: it then holds on that platform
// only and wins over the same line without one, so one server.conf serves
// every machine.
//
// A batch compiles every line before the first one runs, runs them on a draft
// of the running configuration, checks the rules of every module on the
// draft, then replaces the running configuration and applies the modules
// whose keys changed. A failure at any step applies nothing.

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum engine_kind {
    ENGINE_STRING,
    ENGINE_NUMBER,
    ENGINE_ENUM,
    ENGINE_SECRET,
};

// who sends a batch: the administrator, through the command line or
// server.conf, or an MCP client, whose lines a model may write, so it never
// writes a secret
enum engine_role {
    ENGINE_ADMIN,
    ENGINE_MCP,
};

struct engine_key {
    engine_key(std::string n, engine_kind k) : name(std::move(n)), kind(k) {}

    std::string                  name;
    engine_kind                  kind;
    // one value per item of a collection, the item named right before the value
    bool                         named = false;
    // the choices of an enum
    std::vector<std::string>     values;
    // the bounds of a number, both included, and whether it is whole
    std::optional<double>        min;
    std::optional<double>        max;
    bool                         integer = false;
    // the value when no layer holds one, read when asked: the host name, the
    // start directory
    std::function<std::string()> fallback;
};

// what a module reads: the value of a key through every layer, none when no
// layer holds one, and the items of a collection
struct engine_reader {
    virtual ~engine_reader()                                                                             = default;
    virtual std::optional<std::string> get(const std::string & key, const std::string & item = "") const = 0;
    virtual std::vector<std::string>   names(const std::string & module) const                           = 0;
};

// a module owns the keys spelled after its name: "server" owns "server port"
struct engine_module {
    std::string                                       name;
    std::vector<engine_key>                           keys;
    // the rule across keys, checked on every change: why the configuration
    // breaks it, empty when it holds
    std::function<std::string(const engine_reader &)> validate;
    // mirrors the running configuration into the server after a change
    std::function<void(const engine_reader &)>        apply;
};

// a file in modules/ registers its module by existing
bool engine_register(engine_module (*make)());

#define ENGINE_MODULE(make) static const bool engine_registered_##make = engine_register(make)

struct engine_result {
    bool        ok;
    std::string text;
};

// the platform this server runs on: linux, macos or windows
const char * engine_platform();

// the defaults the binary embeds: set lines, platform prefixes allowed, below
// every other layer; returns what it refuses, empty when it takes them all
std::string engine_defaults(const std::string & text);

// the startup-config: the file server.conf runs as a batch of the
// administrator over the defaults, and copy running-config startup-config
// writes it back
engine_result engine_start(const std::string & path, const std::string & version);

// one batch at a time, whoever sends it
engine_result engine_run(const std::string & text, engine_role role);

// the running configuration, read under the lock of the engine
std::optional<std::string> engine_get(const std::string & key, const std::string & item = "");

// reads the running configuration under the lock of the engine, for a reader
// that needs several values of one same configuration
void engine_with(const std::function<void(const engine_reader &)> & read);
