// write.cpp: write memory, IOS's way to save, write alone the same: the
// running-config as the batch leaves it written to server.conf once the batch
// holds, as copy running-config startup-config writes it

#include "engine/registry.h"
#include "engine/run.h"

static const char * const MEMORY = "memory";

static step_t compile_write(const words_t & args) {
    if (args.size() > 1) {
        fail(beyond({ "write", args[0] }, words_t(args.begin() + 1, args.end())));
    }
    if (args.size() == 1 && !starts_with(MEMORY, lower(args[0]))) {
        fail("unknown write \"" + args[0] + "\", use " + MEMORY);
    }
    return [](context & ctx) {
        ctx.save = true;
        return std::string();
    };
}

static words_t next_write(const words_t & args) {
    return args.empty() ? words_t{ MEMORY } : words_t{};
}

static command make_write() {
    return { { "write" }, false, compile_write, next_write };
}

ENGINE_COMMAND(make_write);
