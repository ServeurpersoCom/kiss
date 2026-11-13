// api.cpp: the client of every endpoint: the model list, and a request
// streamed in the protocol the endpoint speaks

#include "lib/api.h"

#include "engine/keychain.h"
#include "engine/seal.h"
#include "engine/text.h"
#include "lib/http.h"
#include "lib/json.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

const protocol & protocol_chat();
const protocol & protocol_messages();
const protocol & protocol_responses();

const std::vector<const protocol *> & api_protocols() {
    static const std::vector<const protocol *> all = { &protocol_chat(), &protocol_messages(), &protocol_responses() };
    return all;
}

// the status of an endpoint overloaded, which HTTP does not name
static const int OVERLOADED = 529;

// the statuses of an endpoint busy or down for a while: a timeout, too many
// requests, a server or gateway error, overloaded
static const int          TRANSIENT[] = { httplib::StatusCode::RequestTimeout_408,
                                          httplib::StatusCode::TooManyRequests_429,
                                          httplib::StatusCode::InternalServerError_500,
                                          httplib::StatusCode::BadGateway_502,
                                          httplib::StatusCode::ServiceUnavailable_503,
                                          httplib::StatusCode::GatewayTimeout_504,
                                          OVERLOADED };
// the commands that fix an endpoint which refuses the key, or is no LLM
static const char * const KEY_FIX     = "set endpoints key <name> <key>";
static const char * const URL_FIX     = "set endpoints url <name> <url>";
static const char * const DATA        = "data:";

const protocol * api_protocol(const std::string & name) {
    for (const protocol * p : api_protocols()) {
        if (name == p->name) {
            return p;
        }
    }
    return api_protocols()[0];
}

std::vector<endpoint> api_endpoints(const engine_reader & config) {
    std::vector<endpoint> out;
    for (const std::string & name : config.names("endpoints")) {
        endpoint e;
        e.speaks  = api_protocol(config.get("endpoints protocol", name).value_or(""));
        e.server  = remote_of(config, "endpoints", name, e.speaks->key);
        e.retries = std::stoi(*config.get("endpoints max-retries", name));
        e.pause   = std::stod(*config.get("endpoints retry-pause", name));
        out.push_back(e);
    }
    return out;
}

static std::condition_variable g_woken;

// every stop alive, and whether the process stops them all
static std::mutex           g_stops_mutex;
static std::set<api_stop *> g_stops;
static bool                 g_quit = false;

api_stop::api_stop() {
    std::lock_guard<std::mutex> lock(g_stops_mutex);
    g_stops.insert(this);
    stopped = g_quit;
}

api_stop::~api_stop() {
    std::lock_guard<std::mutex> lock(g_stops_mutex);
    g_stops.erase(this);
}

// the clients of a stop it reaches, the stop marked stopped first, so a
// waiting pause wakes and a request that goes out later refuses to; they are
// stopped outside every lock, as a stop waits for a client that connects
static void reached(api_stop & s, std::vector<std::shared_ptr<httplib::Client>> & out) {
    s.stopped = true;
    std::lock_guard<std::mutex> lock(s.mutex);
    for (const std::weak_ptr<httplib::Client> & c : s.clients) {
        if (const auto client = c.lock()) {
            out.push_back(client);
        }
    }
    g_woken.notify_all();
}

void api_quit() {
    std::vector<std::shared_ptr<httplib::Client>> clients;
    {
        std::lock_guard<std::mutex> lock(g_stops_mutex);
        g_quit = true;
        for (api_stop * s : g_stops) {
            reached(*s, clients);
        }
    }
    for (const auto & client : clients) {
        client->stop();
    }
}

void api_stop::stop() {
    std::vector<std::shared_ptr<httplib::Client>> held;
    reached(*this, held);
    for (const auto & client : held) {
        client->stop();
    }
}

std::shared_ptr<httplib::Client> api_client(const remote & r, api_stop & stop, std::string & path) {
    std::string host;
    remote_split(r.url, host, path);
    const auto client = std::make_shared<httplib::Client>(host);
    if (!client->is_valid()) {
        throw api_error(r.url + " cannot be reached: a bad port, or https on a build without TLS", URL_FIX);
    }
    const std::chrono::duration<double> timeout(r.timeout);
    client->set_connection_timeout(timeout);
    client->set_read_timeout(timeout);
    client->set_interrupt([&stop] { return stop.stopped.load(); });
    std::lock_guard<std::mutex> lock(stop.mutex);
    stop.clients.erase(std::remove_if(stop.clients.begin(), stop.clients.end(),
                                      [](const std::weak_ptr<httplib::Client> & c) { return c.expired(); }),
                       stop.clients.end());
    stop.clients.push_back(client);
    return client;
}

bool api_events::feed(const char * piece, size_t n, const std::function<bool(const std::string &)> & event) {
    for (size_t i = 0; i < n; i++) {
        const char c = piece[i];
        if (cr && c == '\n') {
            cr = false;
            continue;
        }
        cr = c == '\r';
        if (c != '\r' && c != '\n') {
            line += c;
            continue;
        }
        if (!line.empty()) {
            if (starts_with(line, DATA)) {
                const size_t from = strlen(DATA) + (line.size() > strlen(DATA) && line[strlen(DATA)] == ' ' ? 1 : 0);
                data.push_back(line.substr(from));
            }
            line.clear();
            continue;
        }
        if (data.empty()) {
            continue;
        }
        const std::string e = join(data, "\n");
        data.clear();
        if (!event(e)) {
            return false;
        }
    }
    return true;
}

// what a request carries: the key and the headers of the endpoint, with those
// of its protocol
static httplib::Headers sent(const endpoint & e) {
    httplib::Headers h = remote_headers(e.server);
    e.speaks->headers(h);
    return h;
}

// the path of a request: the url of the endpoint, its last slash dropped,
// then the path of the protocol
static std::string joined(const std::string & base, const char * path) {
    return base.substr(0, base.find_last_not_of('/') + 1) + path;
}

// the server's own JSON error message, else its text, else the HTTP status
// text, with the command that fixes it when the configuration is the cause
static api_error failure(const endpoint & e, int status, const std::string & type, const std::string & body) {
    std::string    message = httplib::status_message(status);
    const json_doc doc     = json_read(body);
    yyjson_val *   said =
        doc ? yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "error"), "message") : nullptr;
    if (yyjson_is_str(said)) {
        message = yyjson_get_str(said);
    } else if (starts_with(type, TEXT_TYPE) && !trim(body).empty()) {
        message = trim(body);
    }
    const std::string fix =
        status == httplib::StatusCode::Unauthorized_401 || status == httplib::StatusCode::Forbidden_403 ? KEY_FIX :
        status == httplib::StatusCode::NotFound_404                                                     ? URL_FIX :
                                                                                                          "";
    return api_error(e.server.url + " answers " + std::to_string(status) + " " + message, fix);
}

// the models an endpoint serves, by id, each as its list writes it, JSON
static api_named api_list(const endpoint & e, api_stop & stop) {
    std::string      base;
    const auto       client = api_client(e.server, stop, base);
    httplib::Request req;
    req.method                = "GET";
    req.path                  = joined(base, e.speaks->models);
    req.headers               = sent(e);
    const httplib::Result res = client->send(req);
    if (stop.stopped) {
        throw api_error(TURN_STOPPED);
    }
    if (!res) {
        throw api_error(e.server.url + " does not answer", URL_FIX);
    }
    const std::string type = res->get_header_value("Content-Type");
    if (res->status != httplib::StatusCode::OK_200) {
        throw failure(e, res->status, type, res->body);
    }
    if (type.find("json") == std::string::npos) {
        throw api_error(e.server.url + " answers a web page, not an LLM", URL_FIX);
    }
    api_named      out;
    const json_doc doc = json_read(res->body);
    size_t         i, n;
    yyjson_val *   m;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "data"), i, n, m) {
        yyjson_val * id = yyjson_obj_get(m, "id");
        if (yyjson_is_str(id)) {
            const std::string json = json_write(m);
            out.emplace_back(yyjson_get_str(id), json);
        }
    }
    return out;
}

std::vector<std::string> api_models(const endpoint & e, api_stop & stop) {
    std::vector<std::string> out;
    for (const auto & m : api_list(e, stop)) {
        out.push_back(m.first);
    }
    return out;
}

std::string api_describe(const endpoint & e, const std::string & model, api_stop & stop) {
    static std::mutex                       m;
    static std::map<std::string, api_named> lists;
    const std::string                       id = joined(e.server.url, e.speaks->models) + " " + e.server.value;
    api_named                               all;
    {
        std::lock_guard<std::mutex> lock(m);
        const auto                  it = lists.find(id);
        if (it != lists.end()) {
            all = it->second;
        }
    }
    if (all.empty()) {
        all = api_list(e, stop);
        std::lock_guard<std::mutex> lock(m);
        lists[id] = all;
    }
    for (const auto & listed : all) {
        if (listed.first == model) {
            return listed.second;
        }
    }
    return "";
}

std::pair<endpoint, std::string> api_pick(const std::vector<endpoint> & all,
                                          const std::string &           chosen,
                                          api_stop &                    stop) {
    if (all.empty()) {
        throw api_error("no endpoint yet", URL_FIX);
    }
    if (chosen.empty()) {
        const auto models = all.size() == 1 ? api_list(all[0], stop) : api_named{};
        if (models.size() != 1) {
            throw api_error("chat model is not set", "set chat model <endpoint/model>");
        }
        return { all[0], models[0].first };
    }
    const size_t cut = chosen.find(MODEL_SEPARATOR);
    for (const endpoint & e : all) {
        if (cut != std::string::npos && chosen.substr(0, cut) == e.server.name) {
            return { e, chosen.substr(cut + std::strlen(MODEL_SEPARATOR)) };
        }
    }
    throw api_error("chat model " + chosen + " names no endpoint", "set chat model <endpoint/model>");
}

void api_chat(const endpoint &                               e,
              const api_request &                            r,
              api_stop &                                     stop,
              const std::function<void(const api_delta &)> & delta,
              const std::function<void(int, int, int)> &     retrying) {
    std::string body;
    seal_json(e.speaks->body(r), keychain_known(), body);
    const auto read = e.speaks->reader();
    // asked again while the endpoint answers a status that passes, before it
    // streamed anything, so nothing repeats
    for (int attempt = 1;; attempt++) {
        std::string      base;
        const auto       client = api_client(e.server, stop, base);
        httplib::Request req;
        req.method  = "POST";
        req.path    = joined(base, e.speaks->path);
        req.headers = sent(e);
        req.body    = body;
        req.set_header("Content-Type", JSON_TYPE);
        int                status = 0;
        std::string        type, refused;
        api_events         events;
        bool               ended = false;
        std::exception_ptr broke;
        req.response_handler = [&](const httplib::Response & res) {
            status = res.status;
            type   = res.get_header_value("Content-Type");
            return !(status == httplib::StatusCode::OK_200 && type.find("html") != std::string::npos);
        };
        // each event of the stream a delta as it comes, the stream read until
        // its last; anything they throw ends the request, thrown again once
        // it did
        const auto each = [&](const std::string & event) {
            std::optional<api_delta> d;
            try {
                d = read(trim(event));
            } catch (const std::exception &) {
                throw api_error(e.server.url + " sent an event that does not read: " + trim(event));
            }
            if (d && d->error) {
                throw api_error(d->error->empty() ? e.server.url + " broke the stream" : *d->error);
            }
            if (d) {
                delta(*d);
                ended = d->end;
            }
            return !ended;
        };
        req.content_receiver = [&](const char * piece, size_t n, size_t, size_t) {
            if (status != httplib::StatusCode::OK_200) {
                refused.append(piece, n);
                return true;
            }
            try {
                return events.feed(piece, n, each);
            } catch (...) {
                broke = std::current_exception();
                return false;
            }
        };
        const httplib::Result res = client->send(req);
        if (stop.stopped) {
            throw api_error(TURN_STOPPED);
        }
        if (broke) {
            std::rethrow_exception(broke);
        }
        if (ended) {
            return;
        }
        if (status == 0) {
            throw api_error(e.server.url + " does not answer: " + httplib::to_string(res.error()), URL_FIX);
        }
        if (status == httplib::StatusCode::OK_200 && type.find("html") != std::string::npos) {
            throw api_error(e.server.url + " answers a web page, not an LLM", URL_FIX);
        }
        if (status == httplib::StatusCode::OK_200) {
            throw api_error(std::string("the stream ended before ") + e.speaks->last);
        }
        const bool passes = std::find(std::begin(TRANSIENT), std::end(TRANSIENT), status) != std::end(TRANSIENT);
        if (!passes || attempt > e.retries) {
            throw failure(e, status, type, refused);
        }
        retrying(status, attempt, e.retries);
        std::unique_lock<std::mutex> lock(stop.mutex);
        g_woken.wait_for(lock, std::chrono::duration<double>(e.pause), [&] { return stop.stopped.load(); });
        if (stop.stopped) {
            throw api_error(TURN_STOPPED);
        }
    }
}
