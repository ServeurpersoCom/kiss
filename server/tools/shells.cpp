// shells.cpp: one tool per shell whose program is found, named after it: the
// command on stdin, the merged output back with the exit code and the
// duration, its lines shown as they come

#include "lib/job.h"
#include "lib/json.h"
#include "lib/prompts.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <chrono>

// the texts every shell tool shares, by this name in prompts.json
static const char * const NAME = "shells";

// a shell as its tool runs it
struct shell {
    std::string command;
    std::string cwd;
    int         timeout;
    size_t      output;
    size_t      shown;
};

// a result with how the call ended, as the log tells it
static tool_outcome ended(tool_outcome r, const std::string & outcome, log_level level) {
    r.outcome = outcome;
    r.level   = level;
    return r;
}

static tool_outcome run(const shell & s, yyjson_val * args, const tool_tick & tick, const tool_partial & partial) {
    yyjson_val *     command = yyjson_obj_get(args, "command");
    const auto       t0      = std::chrono::steady_clock::now();
    const job_result r = job_run(s.command, s.cwd, json_text(command), s.timeout, s.output, tick, partial, s.shown);
    const long long  ms =
        (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!r.started) {
        return ended({ false, "Cannot start " + s.command + ": " + r.error }, "cannot start", LOG_ERROR);
    }
    // the line that tells how the call ended closes the output, within its
    // budget
    output_buffer kept = r.kept;
    if (r.cancelled) {
        return ended({ false, kept.closed(CANCELLED) }, "cancelled", LOG_INFO);
    }
    if (r.timed_out) {
        const std::string after = std::to_string(s.timeout) + " s";
        return ended({ false, kept.closed("Killed after " + after) }, "killed after " + after, LOG_ERROR);
    }
    const std::string code = std::to_string(r.exit_code);
    return ended({ r.exit_code == 0, kept.closed("Exit code " + code + " (" + std::to_string(ms) + " ms)") },
                 "exit " + code, LOG_INFO);
}

static std::vector<tool> shell_tools(const engine_reader & config) {
    std::vector<tool> tools;
    const std::string cwd = config.get("server working-directory").value_or("");
    for (const std::string & name : config.names("shells")) {
        const std::string command = config.get("shells command", name).value_or("");
        if (job_find(command).empty()) {
            continue;
        }
        shell s;
        s.command = command;
        s.cwd     = cwd;
        s.timeout = std::stoi(*config.get("shells timeout", name));
        s.output  = (size_t) std::stoul(*config.get("tools max-output"));
        s.shown   = (size_t) std::stoul(*config.get("tools max-shown"));

        tool t;
        t.suffix      = name;
        t.description = prompts_description(
            NAME, {
                      { "shell",    name                                       },
                      { "hostname", config.get("server hostname").value_or("") }
        });
        t.input = prompts_input(NAME);
        t.run   = [s](yyjson_val * args, const engine_typed &, const tool_tick & tick, const tool_partial & partial) {
            return run(s, args, tick, partial);
        };
        tools.push_back(t);
    }
    return tools;
}

SERVER_TOOLS(shell_tools);
