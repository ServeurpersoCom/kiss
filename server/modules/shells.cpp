// shells.cpp: the shells module: a collection of shells, each a command line
// that runs a script read on stdin, with the seconds a run may take and the
// bytes of output it returns; a shell whose program is found nowhere stays
// out, as the log and show shells say

#include "engine/engine.h"
#include "engine/text.h"
#include "lib/job.h"
#include "lib/log.h"

// a shell names a tool, so its name is a word a tool name takes
static bool tool_word(const std::string & s) {
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return !s.empty();
}

static std::string validate(const engine_reader & config) {
    for (const std::string & shell : config.names("shells")) {
        if (!tool_word(shell)) {
            return "shells " + shell + ": a shell name takes a-z, 0-9, _ and - only, as a tool name does";
        }
        if (!config.get("shells command", shell)) {
            return "shells command " + shell + " is not set";
        }
        if (job_argv(config.get("shells command", shell).value_or("")).empty()) {
            return "shells command " + shell + " names no program";
        }
    }
    return "";
}

// what a shell does on this machine: the program it runs, or why it stays out
static std::string found(const engine_reader & config, const std::string & shell) {
    const std::string command = config.get("shells command", shell).value_or("");
    const std::string program = job_find(command);
    return program.empty() ? "left out, " + job_argv(command).front() + " is found nowhere" : "runs " + program;
}

static void apply(const engine_reader & config) {
    for (const std::string & shell : config.names("shells")) {
        log_info("Shells", "Shell %s %s", shell.c_str(), found(config, shell).c_str());
    }
}

static words_t notes(const engine_reader & config, const std::string & shell) {
    return { comment("shell " + shell + " " + found(config, shell)) };
}

static engine_module make_shells() {
    engine_key command("command", ENGINE_STRING);
    command.named = true;

    engine_key timeout("timeout", ENGINE_NUMBER);
    timeout.named   = true;
    timeout.min     = 1;
    timeout.integer = true;
    timeout.reset   = "290";

    // the head and the tail of the output share the budget
    engine_key output("max-output", ENGINE_NUMBER);
    output.named   = true;
    output.min     = 2;
    output.integer = true;
    output.reset   = "65536";

    engine_module m;
    m.name = "shells";
    m.keys.push_back(command);
    m.keys.push_back(timeout);
    m.keys.push_back(output);
    m.validate = validate;
    m.apply    = apply;
    m.notes    = notes;
    return m;
}

ENGINE_MODULE(make_shells);
