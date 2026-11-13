// show-conversations.cpp: show conversations, newest first, each by its id,
// its date and its title when it has one, the one the batch was sent in marked

#include "engine/registry.h"
#include "engine/run.h"
#include "lib/library.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>

static const char * const MARK = "* ";

// a date to the minute, in the time of the machine
static std::string local_time(double ms) {
    using namespace std::chrono;
    const std::time_t t = system_clock::to_time_t(system_clock::time_point(milliseconds((int64_t) ms)));
    std::tm           tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

static step_t compile_show_conversations(const words_t & args) {
    if (!args.empty()) {
        fail(beyond({ "show", CONVERSATIONS }, args));
    }
    return [](context & ctx) {
        words_t out;
        library_each([&](const conversation & c) {
            const std::string mark = c.id == ctx.conversation ? MARK : "";
            out.push_back(
                comment(mark + conversation_shown(c.id) + " " + local_time(c.updated) + conversation_titled(c.title)));
        });
        return out.empty() ? comment("no conversation yet") : join(out, "\n");
    };
}

static command make_show_conversations() {
    return {
        { "show", CONVERSATIONS },
        false, compile_show_conversations, nullptr
    };
}

ENGINE_COMMAND(make_show_conversations);
