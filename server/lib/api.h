#pragma once
// api.h: the client of every endpoint, the twin of api.ts of the page: the
// model list, and a request streamed in the protocol the endpoint speaks

#include "engine/engine.h"
#include "lib/conversation.h"
#include "lib/remote.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// what an endpoint answered wrong, with the command that fixes it when the
// configuration is the cause
struct api_error : std::runtime_error {
    api_error(const std::string & message, std::string f = "") : std::runtime_error(message), fix(std::move(f)) {}

    std::string fix;
};

// a tool as a request offers it: its name, what it does, the JSON schema of
// its arguments
struct api_tool {
    std::string name;
    std::string description;
    std::string input;
};

// one request of a turn: the model, the system prompt, the history, the
// tools, the parameters set for the model, each a JSON value, and what its
// endpoint lists of the model, JSON, empty when the protocol reads none
struct api_request {
    std::string                                      model;
    std::string                                      system;
    std::vector<conversation_entry>                  messages;
    std::vector<api_tool>                            tools;
    std::vector<std::pair<std::string, std::string>> parameters;
    std::string                                      info;
};

// a piece of a call as the stream gives it, by its index
struct api_call_piece {
    int         index = 0;
    std::string id;
    std::string name;
    std::string args;
};

// what one event of a stream tells: text, thinking, pieces of calls, an item
// the protocol needs back as it came, JSON, the tokens the endpoint counted,
// the end, or an error
struct api_delta {
    std::string                 content;
    std::string                 reasoning;
    std::vector<api_call_piece> calls;
    std::string                 opaque;
    std::optional<double>       usage;
    bool                        end = false;
    std::optional<std::string>  error;
};

// a protocol: its name, the paths of a reply and of the model list, whether
// it reads what the list says of a model, the header of the key unless
// auth-header says, the headers it adds to those of the endpoint, the
// parameters of models it never sends, the body of a request, which throws
// what keeps it from being written, and a reader of the data of each event,
// which tells nothing of an event it skips; last ends the stream
struct protocol {
    const char * name;
    const char * path;
    const char * models;
    bool         described;
    const char * key;
    void (*headers)(httplib::Headers & h);
    std::vector<std::string> drops;
    std::string (*body)(const api_request & r);
    std::function<std::optional<api_delta>(const std::string & data)> (*reader)();
    const char * last;
};

// the protocols of the server
const std::vector<const protocol *> & api_protocols();

// an endpoint: a remote server, the protocol it speaks, the times a reply is
// asked again and the seconds between two of them
struct endpoint {
    remote           server;
    const protocol * speaks  = nullptr;
    int              retries = 0;
    double           pause   = 0;
};

// every endpoint the configuration names, sorted by name
std::vector<endpoint> api_endpoints(const engine_reader & config);

// what stops a request: a flag, and the connection it waits on, closed at
// once on a stop
struct api_stop {
    std::atomic<bool>     stopped{ false };
    std::mutex            mutex;
    std::function<void()> close;

    void stop();
};

// a client of a remote server, its seconds those of the item, closed by a
// stop while it waits; path is the path of its url
std::shared_ptr<httplib::Client> api_client(const remote & r, api_stop & stop, std::string & path);

// what an endpoint lists of one model, kept for the life of the server, a
// list that failed asked again; empty when it lists no such model
std::string api_describe(const endpoint & e, const std::string & model, api_stop & stop);

// the endpoint and the model chat model names, endpoint/model; empty, the
// one model of the one endpoint
std::pair<endpoint, std::string> api_pick(const std::vector<endpoint> & all,
                                          const std::string &           chosen,
                                          api_stop &                    stop);

// a request streamed, one delta per event, asked again while the endpoint
// answers a status that passes, as many times as it takes, retrying told the
// status, the attempt and how many the endpoint takes; throws what goes wrong
void api_chat(const endpoint &                               e,
              const api_request &                            r,
              api_stop &                                     stop,
              const std::function<void(const api_delta &)> & delta,
              const std::function<void(int, int, int)> &     retrying);
