#pragma once
// sync.h: the conversations of the server as the pages that show them follow
// them: one stream of events per page, and the actions of its user, which
// the server alone applies

#include "httplib.h"

#include <functional>
#include <string>

// the actions a page sends, POST /conversations/<action>, beside the stream
// /conversations/events: their names, as the pattern of a path takes them
std::string sync_actions();

// events, over a WebSocket: once admit finds nothing to refuse in the first
// message of the page, every conversation with the protocol, then what
// changes, each message one event per line, an empty message after 15
// seconds of silence, until the page leaves, the listener moves or the
// process ends; every stream is an attached page, which runs the calls of the
// config tool of the pages while it lasts
void sync_events(const httplib::Request &                                req,
                 httplib::ws::WebSocket &                                ws,
                 const std::function<std::string(const std::string &)> & admit);

// an action of the user on conversations, a JSON body: send {text, id?}, a
// message after the thread; command {input, id?}, a line this server runs as
// its config tool does, its output after the thread once it ran; both open a
// conversation without id and are answered {id}; complete {line}, answered
// {words}, the words that complete a line being typed; edit {id, entry, text};
// retry {id, entry}; browse {id, entry}; stop {id}; answer {id, verdict},
// once, always or refuse to the call its turn asks about, {id, response}, the
// response of the user to the form it asks them to fill, or {id, output, ok},
// what a page that runs a call answers, the first answer settling it; pin {id,
// pinned}; drop {id, entry}; erase {ids}; 204 once applied, else why not: 400
// a body that does not hold, 404 a conversation or an entry unknown, 409 a
// turn that runs or a question that does not stand, 500 a file not written
void sync_action(const httplib::Request & req, httplib::Response & res, const std::string & name);
