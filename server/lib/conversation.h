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

// one tool call of a round, with its outcome
struct conversation_call {
    std::string                     id;
    std::string                     name;
    std::string                     args;
    std::string                     result;
    bool                            ok = false;
    std::vector<conversation_image> images;
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

// a file holding conversations, as the page writes one, every secret sealed
std::string conversation_file(const std::vector<conversation> & conversations, const words_t & secrets);

// the last entry written under an entry, itself included: a branch comes back
// as it was last written in
std::string conversation_latest(const conversation & c, const std::string & id);

// an entry leaves the conversation, the entries following it following its
// parent in their order, the leaf moving to its parent when it is the leaf
void conversation_drop(conversation & c, const std::string & id);

// a title as show and erase write it after an id, quoted as a value, nothing
// for none
std::string conversation_titled(const std::string & title);
