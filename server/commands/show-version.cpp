// show-version.cpp: show version

#include "engine/registry.h"
#include "engine/run.h"
#include "engine/schema.h"

static step_t compile_show_version(const std::string &, const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", "version" }, args));
    }
    return [](context & ctx) {
        const std::string name = reader(ctx.draft).get("server hostname", "").value_or("");
        return comment(ENGINE_SYSTEM) + "\n" + comment(engine_component(name, engine_version()));
    };
}

static command make_show_version() {
    return {
        { "show", "version" },
        false, false, compile_show_version, nullptr
    };
}

ENGINE_COMMAND(make_show_version);
