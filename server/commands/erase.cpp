// erase.cpp: erase running-config, erase startup-config, erase conversations,
// erase <id>

#include "engine/near.h"
#include "engine/registry.h"
#include "engine/run.h"
#include "lib/library.h"

// IOS erase: a whole thing gone at once, once the user says yes when an MCP
// client asks: the running-config back to the factory configuration, the
// startup-config gone so the server starts on it, or conversations, every one
// but the pinned ones, or one by its id, a prefix being enough, pinned or not
static step_t compile_erase(const words_t & args) {
    if (args.empty()) {
        throw incomplete();
    }
    if (args.size() > 1) {
        fail(beyond({ "erase", args[0] }, words_t(args.begin() + 1, args.end())));
    }
    const std::string word = args[0];
    return [word](context & ctx) {
        // what each conversation is named by and shows as
        std::vector<conversation> all;
        words_t                   names = { CONVERSATIONS, RUNNING_CONFIG, STARTUP_CONFIG };
        library_each([&](const conversation & c) {
            all.push_back({ c.id, c.title, c.updated, c.pinned, {}, {} });
            names.push_back(c.id);
        });
        const std::string name = choose(word, names, "configuration or conversation");
        if (name == RUNNING_CONFIG || name == STARTUP_CONFIG) {
            if (!engine_confirmed(ctx, "erase " + name, "erase " + name)) {
                return std::string();
            }
            if (name == RUNNING_CONFIG) {
                ctx.draft.clear();
            } else {
                ctx.erase = true;
            }
            return std::string();
        }
        std::vector<conversation> gone;
        for (const conversation & c : all) {
            if (name == CONVERSATIONS ? !c.pinned : c.id == name) {
                gone.push_back(c);
            }
        }
        if (gone.empty()) {
            fail("no conversation to erase");
        }
        const std::string question =
            gone.size() == 1 ? "erase " + gone[0].id.substr(0, ID_SHOWN) + conversation_titled(gone[0].title) :
                               "erase " + std::to_string(gone.size()) + " conversations";
        if (!engine_confirmed(ctx, "erase " + name, question)) {
            return std::string();
        }
        words_t out;
        for (const conversation & c : gone) {
            ctx.forget.push_back(c.id);
            out.push_back("- " + c.id.substr(0, ID_SHOWN) + conversation_titled(c.title));
        }
        return join(out, "\n");
    };
}

static words_t next_erase(const words_t & args) {
    return args.empty() ? words_t{ CONVERSATIONS, RUNNING_CONFIG, STARTUP_CONFIG, "<id>" } : words_t{};
}

static command make_erase() {
    return { { "erase" }, false, compile_erase, next_erase, true };
}

ENGINE_COMMAND(make_erase);
