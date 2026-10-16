#pragma once
// sync.h: the conversations of the server as the pages that show them follow
// them: one stream of events per page, and the actions of its user, which
// the server alone applies

#include "httplib.h"

// the actions a page sends, each a route of its own beside the MCP
constexpr const char * SYNC_ACTIONS = "send|edit|retry|stop|pin|browse|drop|erase";

// events: every conversation, then each one that changes, whole, or goes, as
// null, until the page leaves or the listener moves
void sync_events(const httplib::Request & req, httplib::Response & res);

// an action of the user on conversations, a JSON body: send {text, id?}, a
// message in a conversation, a new one without id, answered {id}; edit {id,
// entry, text}; retry {id, entry}; stop {id}; pin {id, pinned}; browse {id,
// entry}; drop {id, entry}; erase {ids}; 204 once applied, else why not
void sync_action(const httplib::Request & req, httplib::Response & res, const std::string & action);
