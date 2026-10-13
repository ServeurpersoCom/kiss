// write.cpp: write memory, IOS's way to save, write alone the same: the
// running-config as the batch leaves it written to server.conf once the batch
// holds, as copy running-config startup-config writes it and asks it

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"

static const char * const MEMORY = "memory";

static step_t compile_write(const words_t & args) {
    if (args.size() > 1) {
        fail(beyond({ "write", args[0] }, words_t(args.begin() + 1, args.end())));
    }
    if (args.size() == 1) {
        choose(args[0], { MEMORY }, "write");
    }
    return [](context & ctx) {
        engine_save(ctx);
        return std::string();
    };
}

static words_t next_write(const words_t & args) {
    return args.empty() ? words_t{ MEMORY } : words_t{};
}

static command make_write() {
    return { { "write" }, false, compile_write, next_write, true };
}

ENGINE_COMMAND(make_write);
