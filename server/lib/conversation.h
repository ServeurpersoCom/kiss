#pragma once
// conversation.h: the conversations as a file, the twin of conversation.ts of
// the page: a marker naming what it holds, then the conversations, each a tree
// of entries

#include "engine/text.h"

#include <optional>
#include <string>
#include <vector>

// what erase names to take every conversation but the pinned ones
constexpr const char * CONVERSATIONS = "conversations";
// the length an id shows, unique among a few thousand conversations
constexpr size_t       ID_SHOWN      = 8;

// an image a tool hands back, base64 data, shown to the user only
struct conversation_image {
    std::string mime;
    std::string data;
};

// one tool call of a round, with its outcome once it ran; sent, in memory
// only, once it reaches its tool
struct conversation_call {
    std::string                     id;
    std::string                     name;
    std::string                     args;
    std::string                     result;
    std::optional<bool>             ok;
    std::vector<conversation_image> images;
    bool                            sent = false;
};

// what the protocol a round streamed in needs back: its name, and its items as
// the JSON array it wrote
struct conversation_opaque {
    std::string protocol;
    std::string items;
};

// one request of an assistant turn: what the model streamed back, then the
// calls it made
struct conversation_round {
    std::string                        reasoning;
    std::string                        text;
    std::vector<conversation_call>     calls;
    std::optional<conversation_opaque> opaque;
};

// what a turn spent: its tokens, and in milliseconds of the system,
// generating them and in all
struct conversation_stats {
    double tokens     = 0;
    double generation = 0;
    double system     = 0;
};

// user: what the user typed; assistant: one whole turn, round by round, what
// stopped it when it failed, and what it spent; cli: a slash command and its
// output, never sent to the model
enum conversation_role {
    ROLE_USER,
    ROLE_ASSISTANT,
    ROLE_CLI,
};

// a message where it sits in the tree: its id, the entry it follows, none for
// a first one, and when it entered, in milliseconds; the fields of its role
struct conversation_entry {
    std::string                       id;
    std::optional<std::string>        parent;
    double                            time = 0;
    conversation_role                 role = ROLE_USER;
    // user
    std::string                       text;
    // assistant
    std::vector<conversation_round>   rounds;
    std::optional<std::string>        error;
    std::optional<conversation_stats> stats;
    // cli
    std::string                       input;
    std::string                       output;
    bool                              ok = false;
};

// a tree of entries; the path up from the leaf is the thread
struct conversation {
    std::string                     id;
    std::string                     title;
    // the moment its last answer entered, in milliseconds
    double                          updated = 0;
    bool                            pinned  = false;
    std::vector<conversation_entry> entries;
    // the entry the thread ends on, none while the conversation is empty
    std::optional<std::string>      leaf;
};

// a file as the page writes it, whole or refused with where it goes wrong, as
// the page refuses it: every conversation once, each a tree
std::vector<conversation> conversation_parse(const std::string & text);

// one conversation as JSON, as the page writes it, every secret sealed
std::string conversation_json(const conversation & c, const words_t & secrets);

// a part of a conversation as JSON, the same way
std::string conversation_round_json(const conversation_round & r, const words_t & secrets);
std::string conversation_call_json(const conversation_call & c, const words_t & secrets);

// a file holding conversations, as the page writes one, every secret sealed
std::string conversation_file(const std::vector<conversation> & conversations, const words_t & secrets);

// the outcome of a call stopped once sent: its tool may have acted
constexpr const char * STOPPED = "Stopped before it answered: the tool may have acted";

// what of a turn holds once it stops: the calls that ran, those sent as
// stopped, and the rounds that streamed something; a call never sent never ran
std::vector<conversation_round> conversation_settled(const std::vector<conversation_round> & rounds);

// the conversation as it settled, even while the model answers
conversation conversation_stored(const conversation & c);

// a random UUID, version 4 of RFC 9562
std::string conversation_uuid();

// now, in milliseconds since the epoch
double conversation_now();

// a message entering the conversation after an entry, none for a first one,
// with a new id and the moment it entered; the leaf moves to it; returns its
// index
size_t conversation_append(conversation & c, const std::optional<std::string> & parent, conversation_entry e);

// the entries from the first one to the leaf: the thread, and the history the
// model reads
std::vector<conversation_entry> conversation_path(const conversation & c);

// the last entry written under an entry, itself included: a branch comes back
// as it was last written in
std::string conversation_latest(const conversation & c, const std::string & id);

// an entry leaves the conversation, the entries following it following its
// parent in their order, the leaf moving to its parent when it is the leaf
void conversation_drop(conversation & c, const std::string & id);

// a title as show and erase write it after an id, quoted as a value, nothing
// for none
std::string conversation_titled(const std::string & title);
