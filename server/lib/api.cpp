// api.cpp: the client of every endpoint: the model list, and a request
// streamed in the protocol the endpoint speaks

#include "lib/api.h"

#include "engine/keychain.h"
#include "engine/seal.h"
#include "engine/text.h"
#include "yyjson.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>

const protocol & protocol_chat();

const std::vector<const protocol *> & api_protocols() {
    static const std::vector<const protocol *> all = { &protocol_chat() };
    return all;
}

// the statuses of an endpoint busy or down for a while: a timeout, too many
// requests, a server or gateway error, overloaded
static const int          TRANSIENT[] = { 408, 429, 500, 502, 503, 504, 529 };
// the commands that fix an endpoint which refuses the key, or is no LLM
static const char * const KEY_FIX     = "set endpoints key <name> <key>";
static const char * const URL_FIX     = "set endpoints url <name> <url>";
static const char * const DATA        = "data:";
// the bytes read at a time
static const size_t       CHUNK       = 16384;

static const protocol * protocol_named(const std::string & name) {
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
        e.speaks  = protocol_named(config.get("endpoints protocol", name).value_or(""));
        e.server  = remote_of(config, "endpoints", name, e.speaks->key);
        e.retries = std::stoi(config.get("endpoints max-retries", name).value_or("0"));
        e.pause   = std::stod(config.get("endpoints retry-pause", name).value_or("0"));
        out.push_back(e);
    }
    return out;
}

static std::condition_variable g_woken;

void api_stop::stop() {
    stopped = true;
    std::lock_guard<std::mutex> lock(mutex);
    if (close) {
        close();
    }
    g_woken.notify_all();
}

std::shared_ptr<httplib::Client> api_client(const remote & r, api_stop & stop, std::string & path) {
    std::string host;
    remote_split(r.url, host, path);
    const auto client = std::make_shared<httplib::Client>(host);
    if (!client->is_valid()) {
        throw api_error(r.url + " cannot be reached: a bad port, or https on a build without TLS", URL_FIX);
    }
    const time_t seconds = (time_t) r.timeout;
    const long   micros  = (long) ((r.timeout - (double) seconds) * 1e6);
    client->set_connection_timeout(seconds, micros);
    client->set_read_timeout(seconds, micros);
    std::lock_guard<std::mutex> lock(stop.mutex);
    stop.close = [client] {
        client->stop();
    };
    if (stop.stopped) {
        throw api_error("Stopped");
    }
    return client;
}

// the path of a request: the url of the endpoint, its last slash dropped,
// then the path of the protocol
static std::string joined(const std::string & base, const char * path) {
    return base.substr(0, base.find_last_not_of('/') + 1) + path;
}

// the server's own JSON error message, else its text, else the HTTP status
// text, with the command that fixes it when the configuration is the cause
static api_error failure(const endpoint & e, int status, const std::string & type, const std::string & body) {
    std::string                       message = httplib::status_message(status);
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(body.data(), body.size(), 0), yyjson_doc_free);
    yyjson_val *                      said =
        doc ? yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "error"), "message") : nullptr;
    if (yyjson_is_str(said)) {
        message = yyjson_get_str(said);
    } else if (starts_with(type, "text/plain") && !trim(body).empty()) {
        message = trim(body);
    }
    const std::string fix = status == 401 || status == 403 ? KEY_FIX : status == 404 ? URL_FIX : "";
    return api_error(e.server.url + " answers " + std::to_string(status) + " " + message, fix);
}

std::vector<std::pair<std::string, std::string>> api_list(const endpoint & e, api_stop & stop) {
    std::string base;
    const auto  client = api_client(e.server, stop, base);
    const auto  res    = client->Get(joined(base, e.speaks->models), remote_headers(e.server));
    if (!res) {
        throw api_error(e.server.url + " does not answer", URL_FIX);
    }
    const std::string type = res->get_header_value("Content-Type");
    if (res->status != 200) {
        throw failure(e, res->status, type, res->body);
    }
    if (type.find("json") == std::string::npos) {
        throw api_error(e.server.url + " answers a web page, not an LLM", URL_FIX);
    }
    std::vector<std::pair<std::string, std::string>> out;
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(res->body.data(), res->body.size(), 0), yyjson_doc_free);
    size_t                            i, n;
    yyjson_val *                      m;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "data"), i, n, m) {
        yyjson_val * id = yyjson_obj_get(m, "id");
        if (yyjson_is_str(id)) {
            const std::shared_ptr<char> json(yyjson_val_write(m, 0, nullptr), free);
            out.emplace_back(yyjson_get_str(id), json.get());
        }
    }
    return out;
}

std::string api_describe(const endpoint & e, const std::string & model, api_stop & stop) {
    static std::mutex                                                              m;
    static std::map<std::string, std::vector<std::pair<std::string, std::string>>> lists;
    const std::string                                id = joined(e.server.url, e.speaks->models) + " " + e.server.value;
    std::vector<std::pair<std::string, std::string>> all;
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
        const auto models = all.size() == 1 ? api_list(all[0], stop) : decltype(api_list(all[0], stop)){};
        if (models.size() != 1) {
            throw api_error("chat model is not set", "set chat model <endpoint/model>");
        }
        return { all[0], models[0].first };
    }
    const size_t cut = chosen.find('/');
    for (const endpoint & e : all) {
        if (cut != std::string::npos && chosen.substr(0, cut) == e.server.name) {
            return { e, chosen.substr(cut + 1) };
        }
    }
    throw api_error("chat model " + chosen + " names no endpoint", "set chat model <endpoint/model>");
}

// the stream of a reply, asked again while the endpoint answers a status
// that passes; nothing streamed yet, nothing repeats
static std::shared_ptr<httplib::ClientImpl::StreamHandle> opened(const endpoint &                           e,
                                                                 const std::string &                        body,
                                                                 api_stop &                                 stop,
                                                                 std::shared_ptr<httplib::Client> &         keep,
                                                                 const std::function<void(int, int, int)> & retrying) {
    for (int attempt = 1;; attempt++) {
        std::string base;
        keep              = api_client(e.server, stop, base);
        const auto stream = std::make_shared<httplib::ClientImpl::StreamHandle>(keep->open_stream(
            "POST", joined(base, e.speaks->path), {}, remote_headers(e.server), body, "application/json"));
        if (stop.stopped) {
            throw api_error("Stopped");
        }
        if (!stream->is_valid()) {
            throw api_error(e.server.url + " does not answer: " + httplib::to_string(stream->error), URL_FIX);
        }
        const int         status = stream->response->status;
        const std::string type   = stream->response->get_header_value("Content-Type");
        const bool        passes = std::find(std::begin(TRANSIENT), std::end(TRANSIENT), status) != std::end(TRANSIENT);
        if (status == 200 && type.find("html") != std::string::npos) {
            throw api_error(e.server.url + " answers a web page, not an LLM", URL_FIX);
        }
        if (status == 200) {
            return stream;
        }
        if (!passes || attempt > e.retries) {
            std::string all;
            char        buf[CHUNK];
            for (ssize_t n; (n = stream->read(buf, sizeof(buf))) > 0;) {
                all.append(buf, (size_t) n);
            }
            throw failure(e, status, type, all);
        }
        retrying(status, attempt, e.retries);
        std::unique_lock<std::mutex> lock(stop.mutex);
        g_woken.wait_for(lock, std::chrono::duration<double>(e.pause), [&] { return stop.stopped.load(); });
        if (stop.stopped) {
            throw api_error("Stopped");
        }
    }
}

void api_chat(const endpoint &                               e,
              const api_request &                            r,
              api_stop &                                     stop,
              const std::function<void(const api_delta &)> & delta,
              const std::function<void(int, int, int)> &     retrying) {
    std::string body;
    seal_json(e.speaks->body(r), keychain_running(), body);
    std::shared_ptr<httplib::Client> client;
    const auto                       stream = opened(e, body, stop, client, retrying);
    const auto                       read   = e.speaks->reader();
    std::string                      buffer;
    std::vector<std::string>         data;
    char                             buf[CHUNK];
    for (;;) {
        const ssize_t n = stream->read(buf, sizeof(buf));
        if (stop.stopped) {
            throw api_error("Stopped");
        }
        if (n <= 0) {
            throw api_error(std::string("the stream ended before ") + e.speaks->last);
        }
        buffer.append(buf, (size_t) n);
        for (size_t nl; (nl = buffer.find('\n')) != std::string::npos;) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (starts_with(line, DATA)) {
                data.push_back(line.substr(strlen(DATA)));
            }
            if (!line.empty() || data.empty()) {
                continue;
            }
            const std::string event = trim(join(data, "\n"));
            data.clear();
            std::optional<api_delta> d;
            try {
                d = read(event);
            } catch (const std::exception &) {
                throw api_error(e.server.url + " sent an event that does not read: " + event);
            }
            if (!d) {
                continue;
            }
            if (d->error) {
                throw api_error(d->error->empty() ? e.server.url + " broke the stream" : *d->error);
            }
            delta(*d);
            if (d->end) {
                return;
            }
        }
    }
}
