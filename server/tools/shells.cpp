// shells.cpp: one tool per shell whose program is found, named after it: the
// script on stdin, the merged output and the exit status back

#include "process.h"
#include "tools.h"
#include "yyjson.h"

// a shell as its tool runs it
struct shell {
    std::string command;
    std::string cwd;
    int         timeout;
    size_t      output;
};

static tool_result run(const shell & s, yyjson_val * args, const tool_tick & tick) {
    yyjson_val * script = yyjson_obj_get(args, "script");
    if (!yyjson_is_str(script)) {
        return { "script is the text of the script, as the shell reads it on stdin", true };
    }
    yyjson_val * asked   = yyjson_obj_get(args, "timeout");
    int          timeout = s.timeout;
    if (yyjson_is_int(asked) && yyjson_get_sint(asked) > 0 && yyjson_get_sint(asked) < s.timeout) {
        timeout = (int) yyjson_get_sint(asked);
    }
    const process_result r = process_run(s.command, s.cwd, std::string(yyjson_get_str(script), yyjson_get_len(script)),
                                         timeout, s.output, tick);
    if (!r.started) {
        return { "! cannot start " + s.command + ": " + r.error, true };
    }
    std::string text = r.output;
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    if (r.cancelled) {
        return { text + "! cancelled", true };
    }
    if (r.timed_out) {
        return { text + "! killed after " + std::to_string(timeout) + " s", true };
    }
    return { text + "! exit " + std::to_string(r.exit_code), false };
}

static std::vector<tool> shell_tools(const engine_reader & config) {
    std::vector<tool> tools;
    const std::string cwd = config.get("server cwd").value_or("");
    for (const std::string & name : config.names("shells")) {
        const std::string command = config.get("shells command", name).value_or("");
        if (process_find(command).empty()) {
            continue;
        }
        shell s;
        s.command = command;
        s.cwd     = cwd;
        s.timeout = std::stoi(config.get("shells timeout", name).value_or("0"));
        s.output  = (size_t) std::stoul(config.get("shells output", name).value_or("0"));

        tool t;
        t.suffix      = name;
        t.description = "Runs a script with " + command + " on " + engine_platform() + ", in " + cwd +
                        ". The script goes on stdin; stdout and stderr come back merged, the head and the tail of " +
                        std::to_string(s.output) + " bytes at most, then the exit status. The run ends after " +
                        "timeout seconds, " + std::to_string(s.timeout) +
                        " at most and by default, and every process it leaves running ends with it.";
        t.input = R"({"type":"object","properties":{"script":{"type":"string","description":)"
                  R"("the script, as the shell reads it on stdin"},"timeout":{"type":"integer","description":)"
                  R"("seconds the run may take"}},"required":["script"]})";
        t.call  = [s](yyjson_val * args, const tool_tick & tick) {
            return run(s, args, tick);
        };
        tools.push_back(t);
    }
    return tools;
}

SERVER_TOOLS(shell_tools);
