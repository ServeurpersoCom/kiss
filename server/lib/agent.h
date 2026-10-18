#pragma once
// agent.h: the agent of the server, the twin of agent.ts of the page: a turn
// answers a message of the user round by round, each round reading the
// configuration as it stands, until a round calls nothing; it runs on its
// own, a page following it or not

#include <cstdint>
#include <functional>
#include <string>

struct conversation;

// what keeps a turn from starting in a conversation where one runs
constexpr const char * AGENT_BUSY = "a turn runs in this conversation";

// the verdicts a user gives a call in consent: once, always, which turns the
// tool on, or refuse
constexpr const char * AGENT_VERDICTS[] = { "once", "always", "refuse" };

// the model answers a message of the user in a conversation, from the path up
// to it, the answer entering as a new entry beside those it had; user names
// that message, writing it first when it enters with the turn, once the turn
// is taken, so a message enters only with the turn that answers it; the
// message of what keeps it from starting, AGENT_BUSY while a turn runs there,
// empty once it runs
std::string agent_answer(const std::string & id, const std::function<std::string(conversation &)> & user);

// a line the user typed runs on this server as its config tool runs it, as
// the turn of the conversation, the forms it asks shown to every page
// following it, its output entering after the thread; the message of what
// keeps it from starting, AGENT_BUSY while a turn runs there, empty once it
// runs
std::string agent_command(const std::string & id, const std::string & input);

// what the agent of this server names the config tool of an attached page
// after the hostname of the page: abcd_client_config
constexpr const char * AGENT_CLIENT_TOOL = "_client_config";

// a page attaches under its hostname, the agent calling its config tool from
// then on, unless another page holds that name: whether it holds it now
bool agent_attach(const std::string & client);

// a page that held its name leaves, a call it runs answering that it is gone
void agent_detach(const std::string & client);

// what an attached page answers the call a turn asks it to run in a
// conversation, the question it settles by its number: its output and
// whether it went; the message of what keeps it from reaching the turn, no
// such question standing, empty once it does
std::string agent_ran(const std::string & id, uint64_t question, const std::string & output, bool ok);

// whether a turn runs in a conversation
bool agent_busy(const std::string & id);

// what the user answers to the call a turn asks about in a conversation, the
// question by its number, one of the verdicts; the message of what keeps it
// from reaching the turn, no such question standing, empty once it does
std::string agent_grant(const std::string & id, uint64_t question, const std::string & verdict);

// what the user answers to the form a turn asks them to fill in a
// conversation, the question by its number: their response as JSON, {"action":"accept","content":{...}}
// or {"action":"cancel"}; the message of what keeps it from reaching the
// turn, no such question standing, empty once it does
std::string agent_fill(const std::string & id, uint64_t question, const std::string & response);

// the turn of a conversation stops: its request closes, no call runs after
// it, and the answer keeps what settled before
void agent_stop(const std::string & id);
