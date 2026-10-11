// show-version.cpp: show version

#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static step_t compile_show_version(const std::string &, const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", "version" }, args));
    }
    return [](context &) {
        return comment("KiSS server " + engine_version() + ", " + engine_platform());
    };
}

static command make_show_version() {
    return {
        { "show", "version" },
        false, false, compile_show_version, nullptr
    };
}

ENGINE_COMMAND(make_show_version);
