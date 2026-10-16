#pragma once
// agent.h: the agent of the server, the twin of agent.ts of the page: a turn
// answers a message of the user round by round, each round reading the
// configuration as it stands, until a round calls nothing; it runs on its
// own, a page following it or not

#include <string>

// the model answers a message of the user in a conversation, from the path up
// to it, the answer entering as a new entry beside those it had; the message
// of what keeps it from starting, empty once it runs
std::string agent_answer(const std::string & id, const std::string & user);

// whether a turn runs in a conversation
bool agent_busy(const std::string & id);

// the turn of a conversation stops: its request closes, no call runs after
// it, and the answer keeps what settled before
void agent_stop(const std::string & id);
