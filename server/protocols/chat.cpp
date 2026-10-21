// chat.cpp: OpenAI chat completions, the open standard of llama.cpp, vLLM and
// Hugging Face: one delta per event until [DONE], the endpoint counting the
// tokens it generated and those of the prompt on a last chunk of its own; the
// images of the calls go
// to a model whose list says it sees them, or says nothing

#include "engine/keychain.h"
#include "lib/api.h"
#include "yyjson.h"

#include <memory>

static const char * const DONE = "[DONE]";

// whether the model takes images: its list names what it takes as input, as
// llama-server and Hugging Face do, or says nothing
static bool sees(const std::string & info) {
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(info.data(), info.size(), 0), yyjson_doc_free);
    yyjson_val *                      input =
        doc ? yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "architecture"), "input_modalities") :
                                   nullptr;
    if (!yyjson_is_arr(input)) {
        return true;
    }
    size_t       i, n;
    yyjson_val * x;
    yyjson_arr_foreach(input, i, n, x) {
        if (yyjson_equals_str(x, "image")) {
            return true;
        }
    }
    return false;
}

static void add_text(yyjson_mut_doc * d, yyjson_mut_val * o, const char * key, const std::string & v) {
    yyjson_mut_obj_add_strncpy(d, o, key, v.data(), v.size());
}

static yyjson_mut_val * message(yyjson_mut_doc * d, yyjson_mut_val * list, const char * role) {
    yyjson_mut_val * m = yyjson_mut_arr_add_obj(d, list);
    yyjson_mut_obj_add_str(d, m, "role", role);
    return m;
}

// the history in OpenAI form: cli messages stay out, and every round of an
// assistant turn becomes one assistant message followed by its results, then
// one user message of the images they hold, a tool message holding text only
static void history(yyjson_mut_doc *                        d,
                    yyjson_mut_val *                        list,
                    const std::vector<conversation_entry> & messages,
                    bool                                    images) {
    for (const conversation_entry & e : messages) {
        if (e.role == ROLE_USER) {
            add_text(d, message(d, list, "user"), "content", e.text);
        }
        if (e.role != ROLE_ASSISTANT) {
            continue;
        }
        for (const conversation_round & r : e.rounds) {
            yyjson_mut_val * a = message(d, list, "assistant");
            add_text(d, a, "content", r.text);
            if (!r.reasoning.empty()) {
                add_text(d, a, "reasoning_content", r.reasoning);
            }
            if (!r.calls.empty()) {
                yyjson_mut_val * calls = yyjson_mut_obj_add_arr(d, a, "tool_calls");
                for (const conversation_call & c : r.calls) {
                    yyjson_mut_val * call = yyjson_mut_arr_add_obj(d, calls);
                    add_text(d, call, "id", c.id);
                    yyjson_mut_obj_add_str(d, call, "type", "function");
                    yyjson_mut_val * f = yyjson_mut_obj_add_obj(d, call, "function");
                    add_text(d, f, "name", c.name);
                    add_text(d, f, "arguments", c.args);
                }
            }
            std::vector<const conversation_image *> seen;
            for (const conversation_call & c : r.calls) {
                yyjson_mut_val * t = message(d, list, "tool");
                add_text(d, t, "tool_call_id", c.id);
                add_text(d, t, "content", c.result);
                for (const conversation_image & i : c.images) {
                    seen.push_back(&i);
                }
            }
            if (!images || seen.empty()) {
                continue;
            }
            yyjson_mut_val * content = yyjson_mut_obj_add_arr(d, message(d, list, "user"), "content");
            for (const conversation_image * i : seen) {
                yyjson_mut_val * part = yyjson_mut_arr_add_obj(d, content);
                yyjson_mut_obj_add_str(d, part, "type", "image_url");
                add_text(d, yyjson_mut_obj_add_obj(d, part, "image_url"), "url",
                         "data:" + i->mime + ";base64," + i->data);
            }
        }
    }
}

// one system message, first, as every template takes it
static std::string body(const api_request & r) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc *                      d    = doc.get();
    yyjson_mut_val *                      root = yyjson_mut_obj(d);
    yyjson_mut_doc_set_root(d, root);
    for (const auto & p : r.parameters) {
        const std::shared_ptr<yyjson_doc> v(yyjson_read(p.second.data(), p.second.size(), 0), yyjson_doc_free);
        yyjson_mut_obj_add_val(d, root, p.first.c_str(), yyjson_val_mut_copy(d, yyjson_doc_get_root(v.get())));
    }
    add_text(d, root, "model", r.model);
    yyjson_mut_val * messages = yyjson_mut_obj_add_arr(d, root, "messages");
    if (!r.system.empty()) {
        add_text(d, message(d, messages, "system"), "content", r.system);
    }
    history(d, messages, r.messages, sees(r.info));
    yyjson_mut_val * tools = yyjson_mut_obj_add_arr(d, root, "tools");
    for (const api_tool & t : r.tools) {
        yyjson_mut_val * tool = yyjson_mut_arr_add_obj(d, tools);
        yyjson_mut_obj_add_str(d, tool, "type", "function");
        yyjson_mut_val * f = yyjson_mut_obj_add_obj(d, tool, "function");
        add_text(d, f, "name", t.name);
        add_text(d, f, "description", t.description);
        const std::shared_ptr<yyjson_doc> input(yyjson_read(t.input.data(), t.input.size(), 0), yyjson_doc_free);
        yyjson_mut_obj_add_val(d, f, "parameters", yyjson_val_mut_copy(d, yyjson_doc_get_root(input.get())));
    }
    yyjson_mut_obj_add_true(d, root, "stream");
    yyjson_mut_obj_add_true(d, yyjson_mut_obj_add_obj(d, root, "stream_options"), "include_usage");
    const std::shared_ptr<char> text(yyjson_mut_write(d, 0, nullptr), free);
    return text.get();
}

static std::string text_of(yyjson_val * v) {
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : "";
}

static std::optional<api_delta> read_event(const std::string & data) {
    api_delta d;
    if (data == DONE) {
        d.end = true;
        return d;
    }
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(data.data(), data.size(), 0), yyjson_doc_free);
    if (!doc) {
        throw std::runtime_error("not JSON");
    }
    yyjson_val * root  = yyjson_doc_get_root(doc.get());
    yyjson_val * error = yyjson_obj_get(root, "error");
    if (error) {
        d.error = text_of(yyjson_obj_get(error, "message"));
        return d;
    }
    yyjson_val * usage  = yyjson_obj_get(yyjson_obj_get(root, "usage"), "completion_tokens");
    yyjson_val * prompt = yyjson_obj_get(yyjson_obj_get(root, "usage"), "prompt_tokens");
    if (yyjson_is_num(usage)) {
        d.usage = yyjson_get_num(usage);
        if (yyjson_is_num(prompt)) {
            d.context = yyjson_get_num(prompt) + *d.usage;
        }
    }
    yyjson_val * choice = yyjson_arr_get(yyjson_obj_get(root, "choices"), 0);
    yyjson_val * delta  = yyjson_obj_get(choice, "delta");
    if (!delta) {
        return d.usage ? std::optional<api_delta>(d) : std::nullopt;
    }
    d.cut       = text_of(yyjson_obj_get(choice, "finish_reason")) == "length";
    d.content   = text_of(yyjson_obj_get(delta, "content"));
    d.reasoning = text_of(yyjson_obj_get(delta, "reasoning_content"));
    size_t       i, n;
    yyjson_val * c;
    yyjson_arr_foreach(yyjson_obj_get(delta, "tool_calls"), i, n, c) {
        yyjson_val * f = yyjson_obj_get(c, "function");
        d.calls.push_back({ (int) yyjson_get_int(yyjson_obj_get(c, "index")), text_of(yyjson_obj_get(c, "id")),
                            text_of(yyjson_obj_get(f, "name")), text_of(yyjson_obj_get(f, "arguments")) });
    }
    return d;
}

static std::function<std::optional<api_delta>(const std::string &)> reader() {
    return read_event;
}

static void headers(httplib::Headers &) {}

const protocol & protocol_chat() {
    static const protocol p{ "chat", "/chat/completions", "/models", true, BEARER, headers, {}, body, reader, DONE };
    return p;
}
