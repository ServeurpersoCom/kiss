#pragma once
// conversation.h: the conversations as a file, the twin of conversation.ts of
// the page: a marker naming what it holds, then the conversations, each a tree
// of entries

#include <string>
#include <vector>

// what erase names to take every conversation but the pinned ones
constexpr const char * CONVERSATIONS = "conversations";
// the length an id shows, unique among a few thousand conversations
constexpr size_t       ID_SHOWN      = 8;

// a conversation as the list of the server holds it
struct conversation {
    std::string id;
    std::string title;
    // the moment its last answer entered, in milliseconds
    double      updated = 0;
    bool        pinned  = false;
};

// a file as the page writes it, whole or refused with where it goes wrong, as
// the page refuses it: every conversation once, each a tree
std::vector<conversation> conversation_parse(const std::string & text);

// a title as show and erase write it after an id, quoted as a value, nothing
// for none
std::string conversation_titled(const std::string & title);
