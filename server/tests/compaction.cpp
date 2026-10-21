// compaction.cpp: the laws of the compaction of a history, run as the build
// ends, the twins of compaction.test.ts of the page: the model reads every
// step without a summary, the commands left out; from the last summary, its
// kept steps first, across the summary before it; a summary folds the steps
// but the preserved ones, sending the very prefix the model last read, and
// none when no step lies past the preserved ones

#include "lib/compaction.h"

#include "lib/api.h"
#include "lib/prompts.h"
#include "yyjson.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

const protocol & protocol_chat();

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] compaction_test broken: %s\n", what.c_str());
    }
}

static conversation_round made(const std::string & text, std::optional<int> kept = std::nullopt) {
    conversation_round r;
    r.text = text;
    r.kept = kept;
    return r;
}

static conversation_entry user(const std::string & text) {
    conversation_entry e;
    e.text = text;
    return e;
}

static conversation_entry assistant(std::vector<conversation_round> rounds) {
    conversation_entry e;
    e.role   = ROLE_ASSISTANT;
    e.rounds = std::move(rounds);
    return e;
}

// what the model reads of a history, a step by its text, the rounds of a
// message in brackets
static std::string read(const std::vector<conversation_entry> & messages) {
    std::string out;
    for (const conversation_entry & m : messages) {
        out += out.empty() ? "" : "|";
        if (m.role == ROLE_USER) {
            out += m.text;
        }
        if (m.role == ROLE_ASSISTANT) {
            std::string rounds;
            for (const conversation_round & r : m.rounds) {
                rounds += (rounds.empty() ? "" : ",") + r.text;
            }
            out += "[" + rounds + "]";
        }
    }
    return out;
}

static std::string framed(const std::string & summary) {
    std::string text = prompts_compaction("summary");
    text.replace(text.find("{summary}"), 9, summary);
    return text;
}

// the messages of the chat body of a history
static std::vector<std::string> sent(const std::vector<conversation_entry> & messages) {
    api_request r;
    r.model                                = "m";
    r.system                               = "s";
    r.messages                             = messages;
    const std::string                 body = protocol_chat().body(r);
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(body.data(), body.size(), 0), yyjson_doc_free);
    std::vector<std::string>          out;
    size_t                            i, n;
    yyjson_val *                      m;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "messages"), i, n, m) {
        const std::shared_ptr<char> text(yyjson_val_write(m, 0, nullptr), free);
        out.push_back(text.get());
    }
    return out;
}

int main() {
    conversation_entry cli;
    cli.role                                      = ROLE_CLI;
    cli.input                                     = "show title";
    cli.output                                    = "title x";
    const std::vector<conversation_entry> history = { user("one"), assistant({ made("a1") }), cli, user("two"),
                                                      assistant({ made("b1"), made("b2"), made("b3") }) };
    law(read(compaction_compacted(history)) == "one|[a1]|two|[b1,b2,b3]",
        "the model reads every step without a summary, the commands left out");

    std::vector<conversation_entry> h = { history[0], history[1], history[2], history[3],
                                          assistant({ made("b1"), made("b2"), made("S", 2), made("b3") }) };
    law(read(compaction_compacted(h)) == framed("S") + "|[b1,b2,b3]",
        "the model reads from the last summary, its kept steps first, the summary itself never sent: " +
            read(compaction_compacted(h)));

    const std::vector<conversation_entry> twice = { user("one"), assistant({ made("a1"), made("S1", 1), made("a2") }),
                                                    user("two"), assistant({ made("b1"), made("S2", 3), made("b2") }) };
    law(read(compaction_compacted(twice)) == framed("S2") + "|[a2]|two|[b1,b2]",
        "the kept steps read across the summary before it, which it folds: " + read(compaction_compacted(twice)));

    const std::vector<conversation_entry> dollars = { user("one"), assistant({ made("a"), made("x $& $$ $'", 0) }) };
    law(read(compaction_compacted(dollars)) == framed("x $& $$ $'"),
        "a summary reads as written, whatever it holds: " + read(compaction_compacted(dollars)));

    const auto fold = compaction_folding(history, 2);
    law(fold && fold->kept == 2 && read(fold->messages) == "one|[a1]|two|[b1]|" + prompts_compaction("ask"),
        "a summary folds the steps the model reads but the preserved ones, then asks");

    h                 = { history[0], history[1], history[2], history[3],
                          assistant({ made("b1"), made("S", 1), made("b2"), made("b3") }) };
    const auto prefix = compaction_folding(h, 1);
    bool       same   = false;
    if (prefix) {
        const std::vector<std::string> asked = sent(prefix->messages);
        const std::vector<std::string> last  = sent(compaction_compacted(h));
        same                                 = asked.size() > 1 && last.size() >= asked.size() - 1 &&
               std::equal(asked.begin(), asked.end() - 1, last.begin());
    }
    law(prefix && read(prefix->messages) == framed("S") + "|[b1,b2]|" + prompts_compaction("ask") && same,
        "a summary sends the very prefix the model last read, so its endpoint reuses what it cached");

    const std::vector<conversation_entry> done = { assistant({ made("a1"), made("S", 1) }) };
    law(compaction_folding(history, 5) && !compaction_folding(history, 6) && compaction_folding(done, 0) &&
            !compaction_folding(done, 1),
        "a summary folds nothing when no step lies past the preserved ones");

    printf("[Laws] compaction_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
