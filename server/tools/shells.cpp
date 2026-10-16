// shells.cpp: one tool per shell whose program is found, named after it: the
// command on stdin, the merged output back with the exit code and the duration

#include "lib/job.h"
#include "lib/prompts.h"
#include "lib/tools.h"
#include "yyjson.h"

#include <chrono>

// a shell as its tool runs it
struct shell {
    std::string command;
    std::string cwd;
    int         timeout;
    size_t      output;
};

// a result with how the call ended, as the log tells it
static tool_outcome ended(tool_outcome r, const std::string & outcome, log_level level) {
    r.outcome = outcome;
    r.level   = level;
    return r;
}

static tool_outcome run(const shell & s, yyjson_val * args, const tool_tick & tick) {
    yyjson_val *     command = yyjson_obj_get(args, "command");
    const auto       t0      = std::chrono::steady_clock::now();
    const job_result r       = job_run(s.command, s.cwd, std::string(yyjson_get_str(command), yyjson_get_len(command)),
                                       s.timeout, s.output, tick);
    const long long  ms =
        (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!r.started) {
        return ended({ false, "Cannot start " + s.command + ": " + r.error }, "cannot start", LOG_ERROR);
    }
    std::string text = r.output;
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    if (r.cancelled) {
        return ended({ false, text + "Cancelled" }, "cancelled", LOG_INFO);
    }
    if (r.timed_out) {
        const std::string after = std::to_string(s.timeout) + " s";
        return ended({ false, text + "Killed after " + after }, "killed after " + after, LOG_ERROR);
    }
    const std::string code = std::to_string(r.exit_code);
    return ended({ r.exit_code == 0, text + "Exit code " + code + " (" + std::to_string(ms) + " ms)" }, "exit " + code,
                 LOG_INFO);
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
        s.timeout = std::stoi(config.get("shells timeout", name).value_or("0"));
        s.output  = (size_t) std::stoul(config.get("shells output", name).value_or("0"));

        tool t;
        t.suffix      = name;
        t.description = prompts_description(
            "shells", {
                          { "shell",    name                                       },
                          { "hostname", config.get("server hostname").value_or("") }
        });
        t.input = prompts_input("shells");
        t.run   = [s](yyjson_val * args, const engine_typed &, const tool_tick & tick) {
            return run(s, args, tick);
        };
        tools.push_back(t);
    }
    return tools;
}

SERVER_TOOLS(shell_tools);
