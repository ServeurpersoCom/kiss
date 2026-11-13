// mcp.cpp: the client of the MCP servers: the era of a server, the tool list
// page by page, and a call, each answer read as JSON or as the event of its id
// in a stream of events
//
// A server that answers server/discover with the 2026-07-28 era speaks it:
// every request carries the envelope in _meta and the headers of that era,
// and a call that asks the user comes again with what they answered. Any
// other server answers the initialize handshake of the 2025 era, its session
// id carried by every request after it.

#include "lib/mcp.h"

#include "engine/run.h"
#include "engine/text.h"
#include "lib/http.h"
#include "lib/json.h"
#include "lib/log.h"
#include "lib/tools.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>

static const char * const PROTOCOL_VERSION  = "2025-06-18";
static const char * const MODERN_VERSION    = "2026-07-28";
// the name this client gives of itself
static const char * const CLIENT_NAME       = "kiss";
static const char * const SESSION_ID_HEADER = "Mcp-Session-Id";
static const char * const VERSION_HEADER    = "MCP-Protocol-Version";

// a session: what it was opened with, whether it stands, the era the server
// speaks, its id in the 2025 era, and the tools of the server; a session that
// fails stops standing and keeps its tools
struct session {
    std::string           signature;
    bool                  open   = true;
    bool                  modern = false;
    std::string           id;
    std::vector<mcp_tool> tools;
};

static std::mutex                     g_mutex;
static std::map<std::string, session> g_sessions;

// what a session is opened with: the url, the headers and the key
static std::string signature(const remote & r) {
    return r.url + "\n" + r.headers + "\n" + r.value;
}

// a server that does not answer at all, as a request that reached it would
struct unreached : api_error {
    using api_error::api_error;
};

// the answer of a request with id: JSON, or the event of that id in a stream
// of events; the session id the server gives, if any
static json_doc post(const remote &           r,
                     const std::string &      body,
                     const httplib::Headers & era,
                     api_stop &               stop,
                     std::string &            given,
                     bool                     notification) {
    std::string      path;
    const auto       client = api_client(r, stop, path);
    httplib::Request req;
    req.method  = "POST";
    req.path    = path;
    req.headers = remote_headers(r);
    req.body    = body;
    req.set_header("Content-Type", JSON_TYPE);
    req.set_header("Accept", std::string(JSON_TYPE) + ", " + EVENTS_TYPE);
    for (const auto & h : era) {
        req.headers.insert(h);
    }
    int         status   = 0;
    bool        streamed = false;
    std::string all;
    api_events  events;
    json_doc    found;
    req.response_handler = [&](const httplib::Response & res) {
        status   = res.status;
        given    = res.get_header_value(SESSION_ID_HEADER);
        streamed = starts_with(lower(res.get_header_value("Content-Type")), EVENTS_TYPE);
        return !(notification && status < httplib::StatusCode::MultipleChoices_300);
    };
    // the first event that holds an answer, a result or an error
    req.content_receiver = [&](const char * piece, size_t n, size_t, size_t) {
        if (!streamed) {
            all.append(piece, n);
            return true;
        }
        return events.feed(piece, n, [&](const std::string & event) {
            json_doc     d    = json_read(event);
            yyjson_val * root = d ? yyjson_doc_get_root(d.get()) : nullptr;
            if (yyjson_obj_get(root, "result") || yyjson_obj_get(root, "error")) {
                found = d;
            }
            return !found;
        });
    };
    const httplib::Result res = client->send(req);
    if (stop.stopped) {
        throw api_error(TURN_STOPPED);
    }
    if (status == 0) {
        throw unreached(r.url + " does not answer: " + httplib::to_string(res.error()));
    }
    if (notification && status < httplib::StatusCode::MultipleChoices_300) {
        return nullptr;
    }
    if (found) {
        return found;
    }
    if (status >= httplib::StatusCode::MultipleChoices_300) {
        throw api_error(r.url + " answers " + std::to_string(status) + " " + trim(all));
    }
    json_doc d = json_read(all);
    if (!d) {
        throw api_error(r.url + " sent an answer that does not read");
    }
    return d;
}

// the headers of a request of the session after its handshake: those of the
// 2026-07-28 era, the tool a call names included, or the session id and the
// version of the 2025 era
static httplib::Headers era_headers(const session & s, const std::string & method, const std::string & name) {
    if (s.modern) {
        httplib::Headers h{
            { VERSION_HEADER, MODERN_VERSION },
            { "Mcp-Method",   method         }
        };
        if (!name.empty()) {
            h.emplace("Mcp-Name", name);
        }
        return h;
    }
    if (s.id.empty()) {
        return {};
    }
    return {
        { SESSION_ID_HEADER, s.id             },
        { VERSION_HEADER,    PROTOCOL_VERSION }
    };
}

// a JSON-RPC request, its result, its params an object, the envelope of the
// 2026-07-28 era in their _meta when the session speaks it: the version, the
// client and the forms it shows; throws the error the server answers
static json_doc request(const remote &      r,
                        const session &     s,
                        const std::string & method,
                        const std::string & params,
                        api_stop &          stop,
                        std::string &       given,
                        const std::string & name = "") {
    const json_mut   doc    = json_new();
    const json_doc   parsed = json_read(params);
    yyjson_mut_val * root   = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);
    yyjson_mut_obj_add_str(doc.get(), root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_int(doc.get(), root, "id", 1);
    yyjson_mut_obj_add_strncpy(doc.get(), root, "method", method.data(), method.size());
    yyjson_mut_val * p = yyjson_val_mut_copy(doc.get(), yyjson_doc_get_root(parsed.get()));
    yyjson_mut_obj_add_val(doc.get(), root, "params", p);
    if (s.modern) {
        const std::string version = engine_version();
        yyjson_mut_val *  meta    = yyjson_mut_obj_add_obj(doc.get(), p, "_meta");
        yyjson_mut_obj_add_str(doc.get(), meta, "io.modelcontextprotocol/protocolVersion", MODERN_VERSION);
        yyjson_mut_val * info = yyjson_mut_obj_add_obj(doc.get(), meta, "io.modelcontextprotocol/clientInfo");
        yyjson_mut_obj_add_str(doc.get(), info, "name", CLIENT_NAME);
        yyjson_mut_obj_add_strncpy(doc.get(), info, "version", version.data(), version.size());
        yyjson_mut_val * caps = yyjson_mut_obj_add_obj(doc.get(), meta, "io.modelcontextprotocol/clientCapabilities");
        yyjson_mut_obj_add_obj(doc.get(), yyjson_mut_obj_add_obj(doc.get(), caps, "elicitation"), MCP_FORM_MODE);
    }
    const std::string body = json_write(doc);
    json_doc          d    = post(r, body, era_headers(s, method, name), stop, given, false);
    yyjson_val *      err  = yyjson_obj_get(yyjson_doc_get_root(d.get()), "error");
    if (err) {
        yyjson_val * message = yyjson_obj_get(err, "message");
        throw api_error(r.url + " " + method + ": " + (yyjson_is_str(message) ? yyjson_get_str(message) : "error"));
    }
    return d;
}

// whether a server speaks the 2026-07-28 era: its answer to server/discover
// offers it; a server that answers anything else, or an error, speaks the
// 2025 era, while one that does not answer at all is not reached
static bool speaks_modern(const remote & r, api_stop & stop) {
    session     probe{ signature(r), true, true, "", {} };
    std::string given;
    json_doc    d;
    try {
        d = request(r, probe, "server/discover", JSON_OBJECT, stop, given);
    } catch (const unreached &) {
        throw;
    } catch (const std::exception &) {
        if (stop.stopped) {
            throw;
        }
        return false;
    }
    size_t       i, n;
    yyjson_val * v;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(d.get()), "result"), "supportedVersions"), i,
                       n, v) {
        if (yyjson_is_str(v) && std::string(yyjson_get_str(v)) == MODERN_VERSION) {
            return true;
        }
    }
    return false;
}

// the session of a server, opened and listed when it has none or the item
// changed
static session opened(const remote & r, api_stop & stop) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto                  it = g_sessions.find(r.name);
        if (it != g_sessions.end() && it->second.open && it->second.signature == signature(r)) {
            return it->second;
        }
    }
    session     s{ signature(r), true, speaks_modern(r, stop), "", {} };
    std::string given;
    if (!s.modern) {
        request(r, s, "initialize",
                std::string("{\"protocolVersion\":\"") + PROTOCOL_VERSION +
                    "\",\"capabilities\":{},\"clientInfo\":{\"name\":\"" + CLIENT_NAME + "\",\"version\":\"" +
                    engine_version() + "\"}}",
                stop, given);
        s.id = given;
        post(r, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", era_headers(s, "", ""), stop, given,
             true);
    }
    for (std::string cursor;;) {
        const json_doc d =
            request(r, s, "tools/list", cursor.empty() ? JSON_OBJECT : "{\"cursor\":" + cursor + "}", stop, given);
        yyjson_val * result = yyjson_obj_get(yyjson_doc_get_root(d.get()), "result");
        size_t       i, n;
        yyjson_val * t;
        yyjson_arr_foreach(yyjson_obj_get(result, "tools"), i, n, t) {
            yyjson_val * schema = yyjson_obj_get(t, "inputSchema");
            s.tools.push_back({ r.name,
                                json_text(yyjson_obj_get(t, "name")),
                                json_text(yyjson_obj_get(t, "description")),
                                schema ? json_write(schema) : JSON_OBJECT,
                                {},
                                {} });
        }
        yyjson_val * next = yyjson_obj_get(result, "nextCursor");
        if (!yyjson_is_str(next)) {
            break;
        }
        cursor = json_write(next);
    }
    log_info("MCP", "Server %s serves %zu tools in the %s era", r.name.c_str(), s.tools.size(),
             s.modern ? MODERN_VERSION : PROTOCOL_VERSION);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sessions[r.name] = s;
    return s;
}

static void drop(const std::string & name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto                  it = g_sessions.find(name);
    if (it != g_sessions.end()) {
        it->second.open = false;
    }
}

// between the server and its tool in the item that sets the tool:
// exa/web_search_exa
static const char * const TOOL_SEPARATOR   = "/";
// between the server and its tool in the name the model sees a tool by once
// one before holds its own: exa_search
static const char * const SERVED_SEPARATOR = "_";

std::string mcp_item(const std::string & server, const std::string & name) {
    return server + TOOL_SEPARATOR + name;
}

std::string mcp_prefixed(const std::string & server, const std::string & name) {
    return server + SERVED_SEPARATOR + name;
}

std::string mcp_unique(std::set<std::string> & taken, const std::string & server, const std::string & name) {
    const std::string served = taken.count(name) ? mcp_prefixed(server, name) : name;
    const bool        valid  = !served.empty() && served.size() <= TOOL_NAME_MAX &&
                       std::all_of(served.begin(), served.end(),
                                   [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
    return valid && taken.insert(served).second ? served : "";
}

std::vector<mcp_tool> mcp_tools(const remote & r, api_stop & stop) {
    try {
        return opened(r, stop).tools;
    } catch (...) {
        drop(r.name);
        throw;
    }
}

void mcp_forget(const words_t & named) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_sessions.begin(); it != g_sessions.end();) {
        it = std::find(named.begin(), named.end(), it->first) == named.end() ? g_sessions.erase(it) : std::next(it);
    }
}

std::vector<mcp_tool> mcp_kept(const remote & r) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto                  it = g_sessions.find(r.name);
    return it != g_sessions.end() && it->second.signature == signature(r) ? it->second.tools : std::vector<mcp_tool>{};
}

// why KiSS does not answer an input a server asks for, empty when it does: a
// form whose fields are text or yes, as the page answers it
static std::string unanswered(yyjson_val * input) {
    yyjson_val *      params = yyjson_obj_get(input, "params");
    const std::string method = json_text(yyjson_obj_get(input, "method"));
    const std::string mode   = json_text(yyjson_obj_get(params, "mode"));
    if (method != MCP_ELICITATION || (!mode.empty() && mode != MCP_FORM_MODE)) {
        return "asks for " + method + " " + mode + ", which KiSS does not answer";
    }
    yyjson_val * fields = yyjson_obj_get(yyjson_obj_get(params, "requestedSchema"), "properties");
    if (!yyjson_obj_size(fields)) {
        return "asks for a form with no field";
    }
    size_t       i, n;
    yyjson_val * key;
    yyjson_val * field;
    yyjson_obj_foreach(fields, i, n, key, field) {
        const std::string type = json_text(yyjson_obj_get(field, "type"));
        if (type != "string" && type != "boolean") {
            return std::string("asks for ") + yyjson_get_str(key) + ", a field KiSS does not answer: only text or yes";
        }
    }
    return "";
}

mcp_outcome mcp_call(const remote &      r,
                     const std::string & name,
                     const std::string & args,
                     api_stop &          stop,
                     const mcp_answer &  answer) {
    try {
        const session    s = opened(r, stop);
        std::string      given;
        const json_mut   doc  = json_new();
        yyjson_mut_val * root = yyjson_mut_obj(doc.get());
        yyjson_mut_doc_set_root(doc.get(), root);
        yyjson_mut_obj_add_strncpy(doc.get(), root, "name", name.data(), name.size());
        const json_doc parsed = json_read(args);
        yyjson_mut_obj_add_val(
            doc.get(), root, "arguments",
            parsed ? yyjson_val_mut_copy(doc.get(), yyjson_doc_get_root(parsed.get())) : yyjson_mut_obj(doc.get()));
        for (;;) {
            const std::string body   = json_write(doc);
            const json_doc    d      = request(r, s, "tools/call", body, stop, given, name);
            yyjson_val *      result = yyjson_obj_get(yyjson_doc_get_root(d.get()), "result");
            if (json_text(yyjson_obj_get(result, "resultType")) == "input_required") {
                // the call again, with what the user answered to each input
                yyjson_val * inputs = yyjson_obj_get(result, "inputRequests");
                if (!yyjson_obj_size(inputs)) {
                    throw api_error(r.url + " asks for input and names none");
                }
                yyjson_mut_obj_remove_key(root, "inputResponses");
                yyjson_mut_obj_remove_key(root, "requestState");
                yyjson_mut_val * responses = yyjson_mut_obj_add_obj(doc.get(), root, "inputResponses");
                size_t           i, n;
                yyjson_val *     key;
                yyjson_val *     input;
                yyjson_obj_foreach(inputs, i, n, key, input) {
                    const std::string why = unanswered(input);
                    if (!why.empty()) {
                        throw api_error(r.url + " " + why);
                    }
                    const std::string response = answer(json_write(input));
                    const json_doc    answered = json_read(response);
                    // the key copied into the call, as the answer it holds: the
                    // answer of the server it comes from is gone before the call
                    // goes again
                    yyjson_mut_obj_add(responses,
                                       yyjson_mut_strncpy(doc.get(), yyjson_get_str(key), yyjson_get_len(key)),
                                       yyjson_val_mut_copy(doc.get(), yyjson_doc_get_root(answered.get())));
                }
                yyjson_val * state = yyjson_obj_get(result, "requestState");
                if (state) {
                    yyjson_mut_obj_add_val(doc.get(), root, "requestState", yyjson_val_mut_copy(doc.get(), state));
                }
                continue;
            }
            mcp_outcome out;
            out.ok = !yyjson_is_true(yyjson_obj_get(result, "isError"));
            size_t       i, n;
            yyjson_val * part;
            yyjson_arr_foreach(yyjson_obj_get(result, "content"), i, n, part) {
                const std::string type = json_text(yyjson_obj_get(part, "type"));
                if (type == "text") {
                    out.text += (out.text.empty() ? "" : "\n") + json_text(yyjson_obj_get(part, "text"));
                } else if (type == "image") {
                    out.images.push_back(
                        { json_text(yyjson_obj_get(part, "mimeType")), json_text(yyjson_obj_get(part, "data")) });
                }
            }
            return out;
        }
    } catch (...) {
        drop(r.name);
        throw;
    }
}
