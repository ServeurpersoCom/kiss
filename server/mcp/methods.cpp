// methods.cpp: the methods the server answers

#include "mcp/methods.h"

#include "engine/engine.h"
#include "log.h"
#include "mcp/jsonrpc.h"
#include "tools.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

static const char * const SERVER_INFO_META         = "io.modelcontextprotocol/serverInfo";
static const char * const CLIENT_CAPABILITIES_META = "io.modelcontextprotocol/clientCapabilities";

// the one input a call asks for: a form of the secrets the user is to type
static const char * const SECRETS_FORM = "secrets";

// a result the server does not keep: the tools follow the configuration, so a
// client asks again rather than caching
static const int TTL_MS = 0;

static void server_info(yyjson_mut_doc *    doc,
                        yyjson_mut_val *    info,
                        const std::string & name,
                        const std::string & version) {
    const std::string component = engine_component(name, version);
    yyjson_mut_obj_add_strncpy(doc, info, "name", name.c_str(), name.size());
    yyjson_mut_obj_add_str(doc, info, "title", ENGINE_SYSTEM);
    yyjson_mut_obj_add_strncpy(doc, info, "version", version.c_str(), version.size());
    yyjson_mut_obj_add_strncpy(doc, info, "description", component.c_str(), component.size());
}

// the tools and the server hostname of one same configuration
static std::vector<tool> listed(std::string & server) {
    std::vector<tool> tools;
    engine_with([&](const engine_reader & config) {
        server = config.get("server hostname").value_or("");
        tools  = tools_list(config);
    });
    return tools;
}

void method_discover(httplib::Response & res, yyjson_val * id, const std::string & version) {
    const std::string server = engine_get("server hostname").value_or("");
    jsonrpc_result(res, id, true, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, out, "supportedVersions");
        for (const char * v : MODERN_VERSIONS) {
            yyjson_mut_arr_add_str(doc, list, v);
        }
        yyjson_mut_obj_add_obj(doc, yyjson_mut_obj_add_obj(doc, out, "capabilities"), "tools");
        yyjson_mut_obj_add_int(doc, out, "ttlMs", TTL_MS);
        yyjson_mut_obj_add_str(doc, out, "cacheScope", "private");
        yyjson_mut_val * meta = yyjson_mut_obj_add_obj(doc, out, "_meta");
        server_info(doc, yyjson_mut_obj_add_obj(doc, meta, SERVER_INFO_META), server, version);
    });
}

// a version the server does not speak gets its latest, as the protocol states
void method_initialize(httplib::Response & res, yyjson_val * id, yyjson_val * params, const std::string & version) {
    const std::string server = engine_get("server hostname").value_or("");
    yyjson_val *      asked  = yyjson_obj_get(params, "protocolVersion");
    const std::string want   = yyjson_is_str(asked) ? yyjson_get_str(asked) : "";
    std::string       chosen = LEGACY_VERSIONS[0];
    for (const char * v : LEGACY_VERSIONS) {
        if (want == v) {
            chosen = v;
        }
    }
    jsonrpc_result(res, id, false, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_obj_add_strncpy(doc, out, "protocolVersion", chosen.c_str(), chosen.size());
        yyjson_mut_val * caps = yyjson_mut_obj_add_obj(doc, out, "capabilities");
        yyjson_mut_obj_add_bool(doc, yyjson_mut_obj_add_obj(doc, caps, "tools"), "listChanged", false);
        server_info(doc, yyjson_mut_obj_add_obj(doc, out, "serverInfo"), server, version);
    });
}

void method_tools_list(httplib::Response & res, yyjson_val * id, bool modern) {
    std::string             server;
    const std::vector<tool> tools = listed(server);
    jsonrpc_result(res, id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_val * list = yyjson_mut_obj_add_arr(doc, out, "tools");
        for (const tool & t : tools) {
            const std::string name = server + "_" + t.suffix;
            yyjson_mut_val *  item = yyjson_mut_arr_add_obj(doc, list);
            yyjson_mut_obj_add_strncpy(doc, item, "name", name.c_str(), name.size());
            yyjson_mut_obj_add_strncpy(doc, item, "description", t.description.c_str(), t.description.size());
            yyjson_doc * schema = yyjson_read(t.input.c_str(), t.input.size(), 0);
            yyjson_mut_obj_add_val(doc, item, "inputSchema", yyjson_val_mut_copy(doc, yyjson_doc_get_root(schema)));
            yyjson_doc_free(schema);
        }
        if (modern) {
            yyjson_mut_obj_add_int(doc, out, "ttlMs", TTL_MS);
            yyjson_mut_obj_add_str(doc, out, "cacheScope", "private");
        }
    });
}

// a call that asks the user for secrets: an input_required result whose one
// form has a text field per stored key, the call coming again with them
static std::string asked_json(yyjson_val * id, const tool_result & r) {
    return jsonrpc_input_required_json(id, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_val * form =
            yyjson_mut_obj_add_obj(doc, yyjson_mut_obj_add_obj(doc, out, "inputRequests"), SECRETS_FORM);
        yyjson_mut_obj_add_str(doc, form, "method", "elicitation/create");
        yyjson_mut_val * params = yyjson_mut_obj_add_obj(doc, form, "params");
        yyjson_mut_obj_add_str(doc, params, "mode", "form");
        yyjson_mut_obj_add_strncpy(doc, params, "message", r.text.c_str(), r.text.size());
        yyjson_mut_val * schema = yyjson_mut_obj_add_obj(doc, params, "requestedSchema");
        yyjson_mut_obj_add_str(doc, schema, "type", "object");
        yyjson_mut_val * properties = yyjson_mut_obj_add_obj(doc, schema, "properties");
        yyjson_mut_val * required   = yyjson_mut_obj_add_arr(doc, schema, "required");
        for (const std::string & key : r.asks) {
            yyjson_mut_val * field = yyjson_mut_obj_add_obj(doc, properties, key.c_str());
            yyjson_mut_obj_add_str(doc, field, "type", "string");
            yyjson_mut_arr_add_strcpy(doc, required, key.c_str());
        }
    });
}

// the result of a tool call
static std::string call_json(yyjson_val * id, bool modern, const tool_result & r) {
    if (!r.asks.empty()) {
        return asked_json(id, r);
    }
    return jsonrpc_result_json(id, modern, [&](yyjson_mut_doc * doc, yyjson_mut_val * out) {
        yyjson_mut_val * content = yyjson_mut_obj_add_arr(doc, out, "content");
        if (!r.text.empty() || r.image.empty()) {
            yyjson_mut_val * block = yyjson_mut_arr_add_obj(doc, content);
            yyjson_mut_obj_add_str(doc, block, "type", "text");
            yyjson_mut_obj_add_strncpy(doc, block, "text", r.text.c_str(), r.text.size());
        }
        if (!r.image.empty()) {
            yyjson_mut_val * block = yyjson_mut_arr_add_obj(doc, content);
            yyjson_mut_obj_add_str(doc, block, "type", "image");
            yyjson_mut_obj_add_strncpy(doc, block, "data", r.image.c_str(), r.image.size());
            yyjson_mut_obj_add_strncpy(doc, block, "mimeType", r.mime.c_str(), r.mime.size());
        }
        yyjson_mut_obj_add_bool(doc, out, "isError", r.error);
    });
}

// a text on one line, its line breaks made spaces
static std::string one_line(std::string s) {
    for (char & c : s) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return s;
}

// a call as the log tells it: its why as it starts and, in detail, every
// other argument, a text by its first line; then how it ended, in how long
struct call_log {
    std::string                           tool;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    void start(yyjson_val * args) const {
        yyjson_val * why = yyjson_obj_get(args, "description");
        log_info("MCP", "Call %s: %s", tool.c_str(), one_line(yyjson_get_str(why)).c_str());
        size_t       i, n;
        yyjson_val * key;
        yyjson_val * value;
        yyjson_obj_foreach(args, i, n, key, value) {
            if (value == why) {
                continue;
            }
            std::string shown;
            if (yyjson_is_str(value)) {
                const std::string text  = yyjson_get_str(value);
                const size_t      lines = (size_t) std::count(text.begin(), text.end(), '\n') + 1;
                shown = text.substr(0, text.find('\n')) + (lines > 1 ? " (" + std::to_string(lines) + " lines)" : "");
            } else {
                char * json = yyjson_val_write(value, 0, nullptr);
                shown       = json ? json : "";
                free(json);
            }
            log_debug("MCP", "Call %s: %s %s", tool.c_str(), yyjson_get_str(key), shown.c_str());
        }
    }

    long long ms() const {
        return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    }

    void end(const tool_result & r, const char * how) const {
        const std::string outcome = !r.outcome.empty() ? r.outcome : r.error ? "failed" : "done";
        if (r.level == LOG_ERROR) {
            log_error("MCP", "Call %s: %s in %lld ms", tool.c_str(), outcome.c_str(), ms());
        } else {
            log_info("MCP", "Call %s: %s in %lld ms", tool.c_str(), outcome.c_str(), ms());
        }
        const size_t bytes = r.image.empty() ? r.text.size() : r.image.size();
        log_debug("MCP", "Call %s: %zu bytes back %s", tool.c_str(), bytes, how);
    }

    void gone() const { log_info("MCP", "Call %s: cancelled in %lld ms, the client is gone", tool.c_str(), ms()); }
};

// whether a client of the 2026-07-28 era shows a form to the user, as the
// capabilities of its request declare: elicitation with form, or with no mode
static bool takes_forms(yyjson_val * params) {
    yyjson_val * caps        = yyjson_obj_get(yyjson_obj_get(params, "_meta"), CLIENT_CAPABILITIES_META);
    yyjson_val * elicitation = yyjson_obj_get(caps, "elicitation");
    return elicitation && (yyjson_obj_get(elicitation, "form") || !yyjson_obj_get(elicitation, "url"));
}

// the secrets the user typed into the form of an earlier call, when the call
// comes again with its answer; false when they gave none
static bool answered(yyjson_val * params, engine_typed & typed) {
    yyjson_val * form = yyjson_obj_get(yyjson_obj_get(params, "inputResponses"), SECRETS_FORM);
    if (!form) {
        return true;
    }
    yyjson_val * action = yyjson_obj_get(form, "action");
    if (!yyjson_is_str(action) || std::string(yyjson_get_str(action)) != "accept") {
        return false;
    }
    size_t       i, n;
    yyjson_val * key;
    yyjson_val * value;
    yyjson_obj_foreach(yyjson_obj_get(form, "content"), i, n, key, value) {
        if (yyjson_is_str(value)) {
            typed[yyjson_get_str(key)] = yyjson_get_str(value);
        }
    }
    return true;
}

// a client that takes SSE gets a comment every tick while the tool runs, then
// the result as one event; the first write that fails ends the tool. Any other
// client gets the result as JSON, the tool ending once its socket closes. The
// stream outlives the handler, so it holds the request document. A call that
// asks the user for secrets comes again with them in its inputResponses.
void method_tools_call(const httplib::Request &            req,
                       httplib::Response &                 res,
                       const std::shared_ptr<yyjson_doc> & body,
                       yyjson_val *                        id,
                       bool                                modern,
                       yyjson_val *                        params) {
    yyjson_val *      name   = yyjson_obj_get(params, "name");
    const std::string called = yyjson_is_str(name) ? yyjson_get_str(name) : "";
    yyjson_val *      args   = yyjson_obj_get(params, "arguments");
    std::string       server;
    for (const tool & t : listed(server)) {
        if (called != server + "_" + t.suffix) {
            continue;
        }
        const std::string refused = tools_check(t, args);
        if (!refused.empty()) {
            log_error("MCP", "Call %s: refused, %s", called.c_str(), refused.c_str());
            res.set_content(call_json(id, modern, { refused, true }), "application/json");
            return;
        }
        engine_typed typed;
        const bool   given = answered(params, typed);
        const bool   forms = modern && takes_forms(params);
        const auto   run   = [t, args, typed, given, forms](const tool_tick & tick) -> tool_result {
            if (!given) {
                return { "The user typed no secret", true };
            }
            const tool_result r = t.call(args, typed, tick);
            if (!r.asks.empty() && !forms) {
                return {
                    "This client shows the user no form to type a secret: it goes in server.conf or on the "
                        "console of the server",
                    true
                };
            }
            return r;
        };
        const call_log log{ called };
        log.start(args);
        if (req.get_header_value("Accept").find("text/event-stream") == std::string::npos) {
            const tool_result r = run([&req]() { return !req.is_connection_closed(); });
            if (req.is_connection_closed()) {
                log.gone();
                return;
            }
            log.end(r, "as JSON");
            res.set_content(call_json(id, modern, r), "application/json");
            return;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider(
            "text/event-stream", [body, run, log, id, modern](size_t, httplib::DataSink & sink) {
                bool              gone = false;
                const tool_result r    = run([&]() {
                    gone = gone || !sink.write(":\n\n", 3);
                    return !gone;
                });
                if (gone) {
                    log.gone();
                    return false;
                }
                log.end(r, "by SSE");
                const std::string event = "event: message\ndata: " + call_json(id, modern, r) + "\n\n";
                sink.write(event.data(), event.size());
                sink.done();
                return true;
            });
        return;
    }
    log_error("MCP", "Call %s: refused, no such tool", called.c_str());
    jsonrpc_error(res, 200, id, JSONRPC_INVALID_PARAMS, "unknown tool \"" + called + "\"");
}
