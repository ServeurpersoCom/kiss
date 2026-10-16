// mcp.cpp: the client of the MCP servers: the initialize handshake, the tool
// list page by page, and a call, each answer read as JSON or as the event of
// its id in a stream of events

#include "lib/mcp.h"

#include "engine/run.h"
#include "engine/text.h"
#include "lib/log.h"
#include "yyjson.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

static const char * const PROTOCOL_VERSION = "2025-06-18";
static const char * const SESSION_HEADER   = "Mcp-Session-Id";
static const char * const VERSION_HEADER   = "MCP-Protocol-Version";
static const char * const DATA             = "data:";
static const size_t       CHUNK            = 16384;

// a session: what it was opened with, its id, and the tools of the server
struct session {
    std::string           signature;
    std::string           id;
    std::vector<mcp_tool> tools;
};

static std::mutex                     g_mutex;
static std::map<std::string, session> g_sessions;

// what a session is opened with: the url, the headers and the key
static std::string signature(const remote & r) {
    return r.url + "\n" + r.headers + "\n" + r.value;
}

using doc_t = std::shared_ptr<yyjson_doc>;

// the answer of a request with id: JSON, or the event of that id in a stream
// of events; the session id the server gives, if any
static doc_t post(const remote &      r,
                  const std::string & body,
                  const std::string & sid,
                  api_stop &          stop,
                  std::string &       given,
                  bool                notification) {
    std::string path;
    const auto  client  = api_client(r, stop, path);
    auto        headers = remote_headers(r);
    headers.emplace("Accept", "application/json, text/event-stream");
    if (!sid.empty()) {
        headers.emplace(SESSION_HEADER, sid);
        headers.emplace(VERSION_HEADER, PROTOCOL_VERSION);
    }
    auto stream = client->open_stream("POST", path, {}, headers, body, "application/json");
    if (stop.stopped) {
        throw api_error("Stopped");
    }
    if (!stream.is_valid()) {
        throw api_error(r.url + " does not answer: " + httplib::to_string(stream.error));
    }
    const int status = stream.response->status;
    given            = stream.response->get_header_value(SESSION_HEADER);
    if (notification && status < 300) {
        return nullptr;
    }
    std::string all;
    char        buf[CHUNK];
    const bool  events = starts_with(lower(stream.response->get_header_value("Content-Type")), "text/event-stream");
    for (ssize_t n; (n = stream.read(buf, sizeof(buf))) > 0;) {
        all.append(buf, (size_t) n);
        if (!events) {
            continue;
        }
        // lines end in LF, CR LF or CR alike
        all.erase(std::remove(all.begin(), all.end(), '\r'), all.end());
        // the first event that holds an answer, a result or an error
        for (size_t end; (end = all.find("\n\n")) != std::string::npos;) {
            std::string data;
            for (const std::string & line : split(all.substr(0, end), '\n')) {
                if (starts_with(line, DATA)) {
                    data += (data.empty() ? "" : "\n") + trim(line.substr(strlen(DATA)));
                }
            }
            all.erase(0, end + 2);
            doc_t        d(yyjson_read(data.data(), data.size(), 0), yyjson_doc_free);
            yyjson_val * root = d ? yyjson_doc_get_root(d.get()) : nullptr;
            if (yyjson_obj_get(root, "result") || yyjson_obj_get(root, "error")) {
                return d;
            }
        }
    }
    if (status >= 300) {
        throw api_error(r.url + " answers " + std::to_string(status) + " " + trim(all));
    }
    doc_t d(yyjson_read(all.data(), all.size(), 0), yyjson_doc_free);
    if (!d) {
        throw api_error(r.url + " sent an answer that does not read");
    }
    return d;
}

// a JSON-RPC request, its result; throws the error the server answers
static doc_t request(const remote &      r,
                     const std::string & method,
                     const std::string & params,
                     const std::string & sid,
                     api_stop &          stop,
                     std::string &       given) {
    const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + method + "\",\"params\":" + params + "}";
    doc_t             d    = post(r, body, sid, stop, given, false);
    yyjson_val *      err  = yyjson_obj_get(yyjson_doc_get_root(d.get()), "error");
    if (err) {
        yyjson_val * message = yyjson_obj_get(err, "message");
        throw api_error(r.url + " " + method + ": " + (yyjson_is_str(message) ? yyjson_get_str(message) : "error"));
    }
    return d;
}

static std::string text_of(yyjson_val * v) {
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : "";
}

static std::string json_of(yyjson_val * v) {
    const std::shared_ptr<char> text(yyjson_val_write(v, 0, nullptr), free);
    return text ? text.get() : "{}";
}

// the session of a server, opened and listed when it has none or the item
// changed
static session opened(const remote & r, api_stop & stop) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto                  it = g_sessions.find(r.name);
        if (it != g_sessions.end() && it->second.signature == signature(r)) {
            return it->second;
        }
    }
    session     s{ signature(r), "", {} };
    std::string given;
    request(r, "initialize",
            std::string("{\"protocolVersion\":\"") + PROTOCOL_VERSION +
                "\",\"capabilities\":{},\"clientInfo\":{\"name\":\"kiss\",\"version\":\"" + engine_version() + "\"}}",
            "", stop, given);
    s.id = given;
    post(r, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", s.id, stop, given, true);
    for (std::string cursor;;) {
        const doc_t d =
            request(r, "tools/list", cursor.empty() ? "{}" : "{\"cursor\":" + cursor + "}", s.id, stop, given);
        yyjson_val * result = yyjson_obj_get(yyjson_doc_get_root(d.get()), "result");
        size_t       i, n;
        yyjson_val * t;
        yyjson_arr_foreach(yyjson_obj_get(result, "tools"), i, n, t) {
            s.tools.push_back({ r.name, text_of(yyjson_obj_get(t, "name")), text_of(yyjson_obj_get(t, "description")),
                                json_of(yyjson_obj_get(t, "inputSchema")) });
        }
        yyjson_val * next = yyjson_obj_get(result, "nextCursor");
        if (!yyjson_is_str(next)) {
            break;
        }
        cursor = json_of(next);
    }
    log_info("MCP", "Server %s serves %zu tools", r.name.c_str(), s.tools.size());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sessions[r.name] = s;
    return s;
}

static void drop(const std::string & name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sessions.erase(name);
}

std::vector<mcp_tool> mcp_tools(const remote & r, api_stop & stop) {
    try {
        return opened(r, stop).tools;
    } catch (...) {
        drop(r.name);
        throw;
    }
}

mcp_outcome mcp_call(const remote & r, const std::string & name, const std::string & args, api_stop & stop) {
    try {
        const session                         s = opened(r, stop);
        std::string                           given;
        const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
        yyjson_mut_val *                      root = yyjson_mut_obj(doc.get());
        yyjson_mut_doc_set_root(doc.get(), root);
        yyjson_mut_obj_add_strncpy(doc.get(), root, "name", name.data(), name.size());
        const doc_t parsed(yyjson_read(args.data(), args.size(), 0), yyjson_doc_free);
        yyjson_mut_obj_add_val(
            doc.get(), root, "arguments",
            parsed ? yyjson_val_mut_copy(doc.get(), yyjson_doc_get_root(parsed.get())) : yyjson_mut_obj(doc.get()));
        const std::shared_ptr<char> body(yyjson_mut_write(doc.get(), 0, nullptr), free);
        const doc_t                 d      = request(r, "tools/call", body.get(), s.id, stop, given);
        yyjson_val *                result = yyjson_obj_get(yyjson_doc_get_root(d.get()), "result");
        mcp_outcome                 out;
        out.ok = !yyjson_is_true(yyjson_obj_get(result, "isError"));
        size_t       i, n;
        yyjson_val * part;
        yyjson_arr_foreach(yyjson_obj_get(result, "content"), i, n, part) {
            const std::string type = text_of(yyjson_obj_get(part, "type"));
            if (type == "text") {
                out.text += (out.text.empty() ? "" : "\n") + text_of(yyjson_obj_get(part, "text"));
            } else if (type == "image") {
                out.images.push_back(
                    { text_of(yyjson_obj_get(part, "mimeType")), text_of(yyjson_obj_get(part, "data")) });
            }
        }
        return out;
    } catch (...) {
        drop(r.name);
        throw;
    }
}
