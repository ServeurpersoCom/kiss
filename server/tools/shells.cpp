// shells.cpp: one tool per shell whose program is found, named after it: the
// command on stdin, the merged output back with the exit code and the duration

#include "job.h"
#include "tools.h"
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
static tool_result ended(tool_result r, const std::string & outcome, log_level level) {
    r.outcome = outcome;
    r.level   = level;
    return r;
}

static tool_result run(const shell & s, yyjson_val * args, const tool_tick & tick) {
    yyjson_val *     command = yyjson_obj_get(args, "command");
    const auto       t0      = std::chrono::steady_clock::now();
    const job_result r       = job_run(s.command, s.cwd, std::string(yyjson_get_str(command), yyjson_get_len(command)),
                                       s.timeout, s.output, tick);
    const long long  ms =
        (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!r.started) {
        return ended({ "Cannot start " + s.command + ": " + r.error, true }, "cannot start", LOG_ERROR);
    }
    std::string text = r.output;
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    if (r.cancelled) {
        return ended({ text + "Cancelled", true }, "cancelled", LOG_INFO);
    }
    if (r.timed_out) {
        const std::string after = std::to_string(s.timeout) + " s";
        return ended({ text + "Killed after " + after, true }, "killed after " + after, LOG_ERROR);
    }
    const std::string code = std::to_string(r.exit_code);
    return ended({ text + "Exit code " + code + " (" + std::to_string(ms) + " ms)", r.exit_code != 0 }, "exit " + code,
                 LOG_INFO);
}

static std::vector<tool> shell_tools(const engine_reader & config) {
    std::vector<tool> tools;
    const std::string cwd = config.get("server cwd").value_or("");
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
        t.description = "Runs a command with " + command + " on " + engine_platform() + ", in " + cwd +
                        ". The command goes on stdin; stdout and stderr come back merged, the head and the tail of " +
                        std::to_string(s.output) + " bytes at most, then the exit code and the duration. The run " +
                        "ends after " + std::to_string(s.timeout) +
                        " seconds, and every process it leaves running ends with it.";
        t.input = R"({"type":"object","properties":{)"
                  R"("description":{"type":"string","description":"Why I'm running this command, written first"},)"
                  R"("command":{"type":"string","description":"The command to run, as the shell reads it on stdin"}},)"
                  R"("required":["description","command"],"additionalProperties":false})";
        t.call  = [s](yyjson_val * args, const tool_tick & tick) {
            return run(s, args, tick);
        };
        tools.push_back(t);
    }
    return tools;
}

SERVER_TOOLS(shell_tools);
