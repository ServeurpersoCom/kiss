#pragma once
// library.h: the conversations of the server, each the file <id>.json of the
// directory it lives in, holding that conversation alone, as the page writes
// one; read once as the server starts, written again at each change, and
// followed by the pages that show them

#include "engine/text.h"
#include "lib/conversation.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// the conversations of a directory, a file that does not read logged and left
// out; none while it does not exist
void library_open(const std::string & dir);

// reads each conversation, the newest first, under the lock of the library
void library_each(const std::function<void(const conversation &)> & read);

// a conversation changed as change says, its file written again, every
// follower woken; the message of what keeps it from changing, change's own
// included, empty when it changed
std::string library_change(const std::string &                                id,
                           const std::function<std::string(conversation &)> & change,
                           const words_t &                                    secrets);

// the conversations gone, file and all, every follower woken; throws what
// keeps one from going, those before it gone
void library_remove(const words_t & ids);

// what a follower holds: whether it has every conversation yet, the version
// of each it holds, and the version of the library it last read
struct library_follower {
    bool                            started = false;
    std::map<std::string, uint64_t> held;
    uint64_t                        seen = 0;
};

// what a follower is to apply, as events: every conversation at its first
// call, after it each conversation that changed, whole, and each gone, as
// null; it waits up to wait_ms for a change, nothing when none came
std::string library_follow(library_follower & f, int wait_ms, const words_t & secrets);
