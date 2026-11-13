// responses.cpp: the Responses API of OpenAI, stateless: the history goes
// whole as items every round, nothing stored on the server; the reasoning
// streams as its summary and goes back as it came, encrypted; the twin of
// responses.ts of the page

#include "engine/keychain.h"
#include "lib/api.h"
#include "lib/json.h"

#include <memory>

static const char * const NAME      = "responses";
// the parameters of models sent as they are named
static const char * const CARRIED[] = { "temperature", "top_p" };
static const char * const SUMMARY   = "auto";
// the parts of a summary as they read one after the other
static const char * const PARTS     = "\n\n";

// an item of the input
static yyjson_mut_val * item(yyjson_mut_doc * d, yyjson_mut_val * list, const char * type) {
    yyjson_mut_val * i = yyjson_mut_arr_add_obj(d, list);
    yyjson_mut_obj_add_str(d, i, "type", type);
    return i;
}

// the history in items: cli messages stay out; every round of an assistant
// turn becomes the reasoning its protocol kept when it is this one, its text
// as one message in the phase it came in, its calls, then their outputs, an
// output holding its images after its text
static void history(yyjson_mut_doc * d, yyjson_mut_val * list, const std::vector<conversation_entry> & messages) {
    for (const conversation_entry & e : messages) {
        if (e.role == ROLE_USER) {
            yyjson_mut_val * m = yyjson_mut_arr_add_obj(d, list);
            yyjson_mut_obj_add_str(d, m, "role", "user");
            json_put(d, m, "content", e.text);
        }
        if (e.role != ROLE_ASSISTANT) {
            continue;
        }
        for (const conversation_round & r : e.rounds) {
            const json_doc kept  = r.opaque && r.opaque->protocol == NAME ? json_read(r.opaque->items) : nullptr;
            yyjson_val *   items = kept ? yyjson_doc_get_root(kept.get()) : nullptr;
            std::string    phase;
            size_t         i, n;
            yyjson_val *   k;
            yyjson_arr_foreach(items, i, n, k) {
                if (yyjson_equals_str(yyjson_obj_get(k, "type"), "message")) {
                    phase = json_text(yyjson_obj_get(k, "phase"));
                }
            }
            yyjson_arr_foreach(items, i, n, k) {
                if (yyjson_equals_str(yyjson_obj_get(k, "type"), "reasoning")) {
                    yyjson_mut_arr_append(list, yyjson_val_mut_copy(d, k));
                }
            }
            if (!r.text.empty()) {
                yyjson_mut_val * m = yyjson_mut_arr_add_obj(d, list);
                yyjson_mut_obj_add_str(d, m, "role", "assistant");
                json_put(d, m, "content", r.text);
                if (!phase.empty()) {
                    json_put(d, m, "phase", phase);
                }
            }
            for (const conversation_call & c : r.calls) {
                yyjson_mut_val * call = item(d, list, "function_call");
                json_put(d, call, "call_id", c.id);
                json_put(d, call, "name", c.name);
                json_put(d, call, "arguments", c.args);
            }
            for (const conversation_call & c : r.calls) {
                yyjson_mut_val * out = item(d, list, "function_call_output");
                json_put(d, out, "call_id", c.id);
                if (c.images.empty()) {
                    json_put(d, out, "output", c.result);
                    continue;
                }
                yyjson_mut_val * parts = yyjson_mut_obj_add_arr(d, out, "output");
                json_put(d, item(d, parts, "input_text"), "text", c.result);
                for (const conversation_image & image : c.images) {
                    json_put(d, item(d, parts, "input_image"), "image_url", api_data_url(image));
                }
            }
        }
    }
}

static std::string body(const api_request & r) {
    const json_mut   doc  = json_new();
    yyjson_mut_doc * d    = doc.get();
    yyjson_mut_val * root = yyjson_mut_obj(d);
    yyjson_mut_doc_set_root(d, root);
    json_put(d, root, "model", r.model);
    if (!r.system.empty()) {
        json_put(d, root, "instructions", r.system);
    }
    history(d, yyjson_mut_obj_add_arr(d, root, "input"), r.messages);
    if (!r.tools.empty()) {
        yyjson_mut_val * tools = yyjson_mut_obj_add_arr(d, root, "tools");
        for (const api_tool & t : r.tools) {
            yyjson_mut_val * tool = item(d, tools, "function");
            json_put(d, tool, "name", t.name);
            json_put(d, tool, "description", t.description);
            yyjson_mut_obj_add_val(d, tool, "parameters", json_value(d, t.input));
            yyjson_mut_obj_add_false(d, tool, "strict");
        }
    }
    for (const char * k : CARRIED) {
        const std::string v = api_parameter(r, k);
        if (!v.empty()) {
            yyjson_mut_obj_add_val(d, root, k, json_value(d, v));
        }
    }
    const std::string max = api_parameter(r, "max_tokens");
    if (!max.empty()) {
        yyjson_mut_obj_add_val(d, root, "max_output_tokens", json_value(d, max));
    }
    yyjson_mut_val *  reasoning = yyjson_mut_obj_add_obj(d, root, "reasoning");
    const std::string effort    = api_parameter(r, "reasoning_effort");
    if (!effort.empty()) {
        yyjson_mut_obj_add_val(d, reasoning, "effort", json_value(d, effort));
    }
    yyjson_mut_obj_add_str(d, reasoning, "summary", SUMMARY);
    yyjson_mut_obj_add_false(d, root, "store");
    yyjson_mut_obj_add_true(d, root, "stream");
    return json_write(d);
}

static void headers(httplib::Headers &) {}

// a call by the index of its item, its arguments as they stream; a reasoning
// item kept once done, whole, and the phase of a message; the tokens counted,
// generated and of the prompt, come with the end
static std::optional<api_delta> read_event(const std::string & data) {
    const json_doc doc = json_read(data);
    if (!doc) {
        throw std::runtime_error("not JSON");
    }
    yyjson_val *      e     = yyjson_doc_get_root(doc.get());
    yyjson_val *      it    = yyjson_obj_get(e, "item");
    const std::string type  = json_text(yyjson_obj_get(e, "type"));
    const std::string kind  = json_text(yyjson_obj_get(it, "type"));
    const int         index = (int) yyjson_get_int(yyjson_obj_get(e, "output_index"));
    api_delta         out;
    if (type == "response.output_item.added") {
        if (kind != "function_call") {
            return std::nullopt;
        }
        out.calls.push_back(
            { index, json_text(yyjson_obj_get(it, "call_id")), json_text(yyjson_obj_get(it, "name")), "" });
        return out;
    }
    if (type == "response.function_call_arguments.delta") {
        out.calls.push_back({ index, "", "", json_text(yyjson_obj_get(e, "delta")) });
        return out;
    }
    if (type == "response.output_text.delta") {
        out.content = json_text(yyjson_obj_get(e, "delta"));
        return out;
    }
    if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta") {
        out.reasoning = json_text(yyjson_obj_get(e, "delta"));
        return out;
    }
    if (type == "response.reasoning_summary_part.added") {
        if (yyjson_get_int(yyjson_obj_get(e, "summary_index")) <= 0) {
            return std::nullopt;
        }
        out.reasoning = PARTS;
        return out;
    }
    if (type == "response.output_item.done") {
        const std::string phase = json_text(yyjson_obj_get(it, "phase"));
        if (kind == "reasoning") {
            out.opaque = json_write(it);
            return out;
        }
        if (kind == "message" && !phase.empty()) {
            const json_mut   made = json_new();
            yyjson_mut_val * o    = yyjson_mut_obj(made.get());
            yyjson_mut_doc_set_root(made.get(), o);
            yyjson_mut_obj_add_str(made.get(), o, "type", "message");
            json_put(made.get(), o, "phase", phase);
            out.opaque = json_write(made);
            return out;
        }
        return std::nullopt;
    }
    if (type == "response.completed" || type == "response.incomplete") {
        yyjson_val * counts = yyjson_obj_get(yyjson_obj_get(e, "response"), "usage");
        yyjson_val * usage  = yyjson_obj_get(counts, "output_tokens");
        yyjson_val * prompt = yyjson_obj_get(counts, "input_tokens");
        if (yyjson_is_num(usage)) {
            out.usage = yyjson_get_num(usage);
            if (yyjson_is_num(prompt)) {
                out.context = yyjson_get_num(prompt) + *out.usage;
            }
        }
        out.cut = type == "response.incomplete";
        out.end = true;
        return out;
    }
    if (type == "response.failed") {
        out.error = json_text(yyjson_obj_get(yyjson_obj_get(yyjson_obj_get(e, "response"), "error"), "message"));
        return out;
    }
    if (type == "error") {
        out.error = json_text(yyjson_obj_get(e, "message"));
        return out;
    }
    return std::nullopt;
}

static std::function<std::optional<api_delta>(const std::string &)> reader() {
    return read_event;
}

const protocol & protocol_responses() {
    static const protocol p{
        NAME,
        "/responses",
        "/models",
        false,
        BEARER,
        headers,
        { "top_k", "min_p", "presence_penalty", "frequency_penalty", "seed" },
        body,
        reader,
        "response.completed"
    };
    return p;
}
