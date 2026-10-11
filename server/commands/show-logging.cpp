// show-logging.cpp: show logging, the last lines of the log as server
// buffered keeps them, as IOS shows its logging buffer

#include "engine/registry.h"
#include "engine/run.h"
#include "log.h"

static step_t compile_show_logging(const std::string &, const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", "logging" }, args));
    }
    return [](context &) {
        std::string kept = log_buffered();
        if (kept.empty()) {
            return comment("no log kept, server buffered is 0 or nothing happened yet");
        }
        kept.pop_back();
        return kept;
    };
}

static command make_show_logging() {
    return {
        { "show", "logging" },
        false, false, compile_show_logging, nullptr
    };
}

ENGINE_COMMAND(make_show_logging);
