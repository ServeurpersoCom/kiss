// shells.cpp: the shells module: a collection of shells, each a command line
// that runs a script read on stdin, with the seconds a run may take and the
// bytes of output it returns; a shell whose program is found nowhere stays
// out, the log says so

#include "engine/engine.h"
#include "job.h"
#include "log.h"

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
        for (const char * key : { "command", "timeout", "output" }) {
            if (!config.get(std::string("shells ") + key, shell)) {
                return std::string("shells ") + key + " " + shell + " is not set";
            }
        }
        if (job_argv(config.get("shells command", shell).value_or("")).empty()) {
            return "shells command " + shell + " names no program";
        }
    }
    return "";
}

static void apply(const engine_reader & config) {
    for (const std::string & shell : config.names("shells")) {
        const std::string command = config.get("shells command", shell).value_or("");
        const std::string program = job_find(command);
        if (program.empty()) {
            log_info("Shells", "Shell %s left out, %s is found nowhere", shell.c_str(),
                     job_argv(command).front().c_str());
        } else {
            log_info("Shells", "Shell %s runs %s", shell.c_str(), program.c_str());
        }
    }
}

static engine_module make_shells() {
    engine_key command("command", ENGINE_STRING);
    command.named = true;

    engine_key timeout("timeout", ENGINE_NUMBER);
    timeout.named   = true;
    timeout.min     = 1;
    timeout.integer = true;

    // the head and the tail of the output share the budget
    engine_key output("output", ENGINE_NUMBER);
    output.named   = true;
    output.min     = 2;
    output.integer = true;

    engine_module m;
    m.name = "shells";
    m.keys.push_back(command);
    m.keys.push_back(timeout);
    m.keys.push_back(output);
    m.validate = validate;
    m.apply    = apply;
    return m;
}

ENGINE_MODULE(make_shells);
