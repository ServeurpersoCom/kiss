#pragma once
// pulse.h: the clock of one turn of the agent, the twin of pulse.ts of the
// page: the phase the turn is in and since when, the endpoint asked again,
// the tokens of the round streamed now and what the rounds ended spent; the
// time the user takes on a question is taken out of it, so it measures the
// system alone

#include "lib/conversation.h"

#include <optional>
#include <string>

// what a turn does now: preparing a round, waiting for the endpoint, asking
// it again, streaming its thinking, its text or a call, running a tool, or
// streaming the summary its history compacts into
constexpr const char * PHASE_PREPARING  = "preparing";
constexpr const char * PHASE_WAITING    = "waiting";
constexpr const char * PHASE_RETRYING   = "retrying";
constexpr const char * PHASE_THINKING   = "thinking";
constexpr const char * PHASE_WRITING    = "writing";
constexpr const char * PHASE_CALLING    = "calling";
constexpr const char * PHASE_RUNNING    = "running";
constexpr const char * PHASE_COMPACTING = "compacting";

// the clock of one turn, in milliseconds of pulse_now()
struct pulse {
    // the round streamed now, from 1
    int         round = 1;
    std::string phase = PHASE_PREPARING;
    // the endpoint waited for, or the tool called or run
    std::string name;
    // while the endpoint is asked again: the status it answered, the attempt
    // made and how many it takes
    int         status     = 0;
    int         attempt    = 0;
    int         attempts   = 0;
    // when the phase began, when the turn began
    double      since      = 0;
    double      start      = 0;
    // the first chunk of the round and the last of the turn, none before them
    double      first      = -1;
    double      last       = -1;
    // the tokens of the round, one per chunk until the endpoint counts them,
    // and the tokens and the time of generation of the rounds ended
    double      tokens     = 0;
    double      total      = 0;
    double      generation = 0;
};

// the monotonic clock of the server, in milliseconds
double pulse_now();

// a turn that starts now
pulse pulse_start(double now);

// the phase the turn enters, its clock restarting only when it changes
void pulse_mark(pulse & p, const char * phase, const std::string & name, double now);

// the endpoint asked again, the clock running since it first failed
void pulse_retry(pulse & p, const std::string & name, int status, int attempt, int attempts, double now);

// a chunk of the stream, holding a token or not
void pulse_chunk(pulse & p, bool token, double now);

// the round ends, its tokens counted by the endpoint when it gives them; a
// round ended twice counts once
void pulse_close(pulse & p, std::optional<double> usage);

// the time the user took on a question, out of every clock of the turn
void pulse_held(pulse & p, double ms);

// what the turn spent, once it streamed anything
std::optional<conversation_stats> pulse_stats(pulse & p, double now);

// the pulse as a page shows it, its moments on the clock of conversation_now()
// so that it holds as written until it changes: {"round","phase","name",
// "status","attempt","attempts","since" the phase began,"first" and "last"
// chunk of the round, null before them,"tokens" of the round}
std::string pulse_json(const pulse & p);
