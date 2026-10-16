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

// a new conversation, its file written, every follower woken; the message of
// what keeps it from being written, empty once it is
std::string library_create(const conversation & c, const words_t & secrets);

// a conversation changed as change says, its file written again as it
// settled, every follower woken; the message of what keeps it from changing,
// change's own included, empty when it changed
std::string library_change(const std::string &                                id,
                           const std::function<std::string(conversation &)> & change,
                           const words_t &                                    secrets);

// an entry a turn writes, grown as grow says, in memory only, every follower
// woken: its strings only lengthen, its rounds and calls only come after
// those it has, and a call gains its outcome once; false when the
// conversation or the entry is gone
bool library_grow(const std::string &                               id,
                  const std::string &                               entry,
                  const std::function<void(conversation_entry &)> & grow);

// whether a turn answers in a conversation now, every follower woken; held
// in memory only
void library_answering(const std::string & id, bool answering);

// the conversations gone, file and all, every follower woken; throws what
// keeps one from going, those before it gone
void library_remove(const words_t & ids);

// what a follower holds of an entry a turn writes: the length of every string
// and the calls whose outcome came
struct library_shape {
    struct call {
        size_t name = 0;
        size_t args = 0;
        bool   done = false;
    };

    struct round {
        size_t            reasoning = 0;
        size_t            text      = 0;
        std::vector<call> calls;
    };

    std::string        entry;
    std::vector<round> rounds;
};

// what a follower holds of a conversation: the version it read, the version
// of its last change other than a growth, and the entry a turn writes
struct library_held {
    uint64_t      version   = 0;
    uint64_t      structure = 0;
    bool          answering = false;
    library_shape shape;
};

// what a follower holds: whether it has every conversation yet, each
// conversation as it holds it, and the version of the library it last read
struct library_follower {
    bool                                started = false;
    std::map<std::string, library_held> held;
    uint64_t                            seen = 0;
};

// what a follower is to apply, as events: every conversation at its first
// call; after it, a conversation changed otherwise than by growth, whole, one
// gone, as null, whether a turn answers in it, true or null, and what an
// entry a turn writes grew by: the text a string
// gained, a round or a call that came, a call whole once its outcome came; it
// waits up to wait_ms for a change, nothing when none came
std::string library_follow(library_follower & f, int wait_ms, const words_t & secrets);
