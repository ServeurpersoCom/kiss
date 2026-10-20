#pragma once
// compaction.h: the compaction of a history, the twin of compaction.ts of the
// page: once a request holds as many tokens as the trigger of its model, the
// steps the model reads but the preserved last ones fold into a summary, a
// round of the turn the next request reads instead of them; a step is what
// the model reads of the history, a user message or one round of an
// assistant turn; the history keeps every step, and what the model reads
// always follows from it alone

#include "lib/conversation.h"

#include <optional>
#include <vector>

// the keys of a model that KiSS reads itself, never sent
constexpr const char * COMPACTION_TRIGGER  = "compaction-trigger-tokens";
constexpr const char * COMPACTION_PRESERVE = "compaction-preserve-steps";

// the history as the model reads it
std::vector<conversation_entry> compaction_compacted(const std::vector<conversation_entry> & messages);

// what a summary of the history asks the model, the steps it folds then the
// ask, and how many steps it keeps
struct compaction_fold {
    std::vector<conversation_entry> messages;
    int                             kept = 0;
};

// the summary a history asks for, none when the steps it reads fold none past
// the preserved ones
std::optional<compaction_fold> compaction_folding(const std::vector<conversation_entry> & messages, int preserve);
