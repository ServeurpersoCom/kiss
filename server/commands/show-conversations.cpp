// show-conversations.cpp: show conversations, newest first, each by its id,
// its date and its title when it has one

#include "engine/registry.h"
#include "engine/run.h"
#include "lib/library.h"

#include <cstdio>
#include <ctime>

// a date to the minute, in the time of the machine
static std::string local_time(double ms) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
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
    return [](context &) {
        words_t out;
        library_each([&](const conversation & c) {
            out.push_back(
                comment("  " + c.id.substr(0, ID_SHOWN) + " " + local_time(c.updated) + conversation_titled(c.title)));
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
