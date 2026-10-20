// compaction.cpp: the compaction of a history, the twin of compaction.ts of
// the page

#include "lib/compaction.h"

#include "lib/prompts.h"

#include <algorithm>
#include <string>

// where the summary goes in the text the model reads it in
static const std::string SUMMARY = "{summary}";

// a step by where it sits: a user message, or one round of an assistant turn,
// none for a user message
struct step_t {
    size_t                message = 0;
    std::optional<size_t> round;
};

// what the model reads of a history: the last summary, none before the first,
// and the steps after it, the kept ones before it first
struct view_t {
    std::optional<std::string> summary;
    std::vector<step_t>        steps;
};

// every step of a history in order, a summary or a cli message being none
static std::vector<step_t> every(const std::vector<conversation_entry> & messages) {
    std::vector<step_t> out;
    for (size_t m = 0; m < messages.size(); m++) {
        if (messages[m].role == ROLE_USER) {
            out.push_back({ m, std::nullopt });
        }
        if (messages[m].role != ROLE_ASSISTANT) {
            continue;
        }
        for (size_t r = 0; r < messages[m].rounds.size(); r++) {
            if (!messages[m].rounds[r].kept) {
                out.push_back({ m, r });
            }
        }
    }
    return out;
}

// the steps the model reads: those after the last summary, behind the kept
// steps right before it
static view_t view(const std::vector<conversation_entry> & messages) {
    const std::vector<step_t> all = every(messages);
    for (size_t m = messages.size(); m-- > 0;) {
        if (messages[m].role != ROLE_ASSISTANT) {
            continue;
        }
        for (size_t r = messages[m].rounds.size(); r-- > 0;) {
            const conversation_round & round = messages[m].rounds[r];
            if (!round.kept) {
                continue;
            }
            const auto   after = std::find_if(all.begin(), all.end(), [&](const step_t & s) {
                return s.message > m || (s.message == m && s.round && *s.round > r);
            });
            const size_t at    = (size_t) (after - all.begin());
            const size_t from  = at - std::min(at, (size_t) *round.kept);
            return { round.text, std::vector<step_t>(all.begin() + (std::ptrdiff_t) from, all.end()) };
        }
    }
    return { std::nullopt, all };
}

// the history a view reads as: the summary as a user message, then the steps,
// the rounds of one turn in a row as one assistant message
static std::vector<conversation_entry> render(const std::vector<conversation_entry> & messages, const view_t & v) {
    std::vector<conversation_entry> out;
    if (v.summary) {
        conversation_entry summary;
        summary.text    = prompts_compaction("summary");
        const size_t at = summary.text.find(SUMMARY);
        summary.text.replace(at, SUMMARY.size(), *v.summary);
        out.push_back(summary);
    }
    // the turn whose rounds the last assistant message holds, none after
    // another message
    std::optional<size_t> turn;
    for (const step_t & s : v.steps) {
        const conversation_entry & m = messages[s.message];
        if (m.role != ROLE_ASSISTANT) {
            out.push_back(m);
            turn.reset();
            continue;
        }
        if (turn != s.message) {
            conversation_entry reply;
            reply.role = ROLE_ASSISTANT;
            out.push_back(reply);
            turn = s.message;
        }
        out.back().rounds.push_back(m.rounds[*s.round]);
    }
    return out;
}

std::vector<conversation_entry> compaction_compacted(const std::vector<conversation_entry> & messages) {
    return render(messages, view(messages));
}

std::optional<compaction_fold> compaction_folding(const std::vector<conversation_entry> & messages, int preserve) {
    const view_t v      = view(messages);
    const int    folded = (int) v.steps.size() - preserve;
    if (folded < 1) {
        return std::nullopt;
    }
    view_t part = v;
    part.steps.resize((size_t) folded);
    compaction_fold    out{ render(messages, part), preserve };
    conversation_entry ask;
    ask.text = prompts_compaction("ask");
    out.messages.push_back(ask);
    return out;
}
