#pragma once
// registry.h: the commands of the CLI, each registered by its file, and how a
// line of words resolves to one, the way Cisco IOS reads it

#include "engine/engine.h"
#include "engine/text.h"
#include "engine/values.h"

#include <functional>
#include <utility>

// what a step runs on: the draft, who sends the batch, the secrets the user
// typed, the stored keys of the secrets still to ask for, the yes questions
// still to ask, and whether the batch writes or erases server.conf once it
// holds
struct context {
    layer &                 draft;
    engine_role             role;
    const engine_typed &    typed;
    words_t &               asks;
    std::vector<engine_ask> questions = {};
    bool                    save      = false;
    bool                    erase     = false;
};

// whether the user lets a step do what question says: always on the console;
// for an MCP client once the user answered yes under key, the question asked
// as the batch ends until then
bool engine_confirmed(context & ctx, const std::string & key, const std::string & question);

// a write of server.conf once the batch holds, as copy and write ask it of the
// user; false while it waits for their yes
bool engine_save(context & ctx);

using step_t = std::function<std::string(context &)>;

struct command {
    words_t path;
    // its first argument names a module, so after its path the name of a
    // module competes with the words of longer commands
    bool    module;
    // the step the arguments spell, checked before any line runs
    step_t (*compile)(const words_t & args);
    // candidates for the next argument; a candidate in <> names what to type
    words_t (*complete)(const words_t & args);
};

// a file in commands/ registers its command by existing
bool engine_command(command (*make)());

#define ENGINE_COMMAND(make) static const bool engine_command_##make = engine_command(make)

// the command the words spell and the words after its path; incomplete when
// they stop short of one
std::pair<const command *, words_t> resolve(const words_t & words);

// what may follow the words: path words while the path goes on, then the
// arguments the command proposes
words_t next_words(const words_t & words);
