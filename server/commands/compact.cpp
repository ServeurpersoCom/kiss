// compact.cpp: compact, the conversation the batch runs in compacting its
// history once the batch holds: the steps the model reads but the preserved
// last ones fold into a summary, as they do once a request reaches the
// trigger of the model; typed by the user, a turn of its own writes the
// summary, run by the model, it folds before its next request; the twin of
// compact.ts of the page

#include "engine/registry.h"
#include "engine/run.h"

static step_t compile_compact(const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "compact" }, args));
    }
    return [](context & ctx) {
        if (!ctx.conversation) {
            fail("no conversation here");
        }
        ctx.compact = true;
        return std::string();
    };
}

static command make_compact() {
    return { { "compact" }, false, compile_compact, nullptr };
}

ENGINE_COMMAND(make_compact);
