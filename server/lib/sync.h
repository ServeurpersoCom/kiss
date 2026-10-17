#pragma once
// sync.h: the conversations of the server as the pages that show them follow
// them: one stream of events per page, and the actions of its user, which
// the server alone applies

#include "httplib.h"

// the actions a page sends, POST /conversations/<action>, beside the stream
// GET /conversations/events
constexpr const char * SYNC_ACTIONS = "send|command|complete|edit|retry|stop|answer|pin|browse|drop|erase";

// events: every conversation with the protocol, then what changes, until the
// page leaves or the listener moves, a comment each second it stays quiet;
// ?client=<hostname> names an attached page, whose config tool the agent
// calls while its stream lasts, unless another page holds that name, 400 a
// name a tool name does not take
void sync_events(const httplib::Request & req, httplib::Response & res);

// an action of the user on conversations, a JSON body: send {text, id?}, a
// message after the thread; command {input, id?}, a line this server runs as
// its config tool does, its output after the thread once it ran; both open a
// conversation without id and are answered {id}; complete {line}, answered
// {words}, the words that complete a line being typed; edit {id, entry,
// text}; retry {id, entry}; browse {id, entry}; stop {id}; answer {id,
// verdict}, once, always or refuse to the call its turn asks about, {id,
// response}, the response of the user to the form it asks them to fill, or
// {id, output, ok}, what the page a call runs on answers; pin {id, pinned};
// drop {id, entry}; erase {ids};
// 204 once applied, else why not: 400 a body that does not hold, 404 a
// conversation or an entry unknown, 409 a turn that runs or a question that
// does not stand, 500 a file not written
void sync_action(const httplib::Request & req, httplib::Response & res, const std::string & action);
