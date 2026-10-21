// messages.cpp: the Messages API of Anthropic: content blocks streamed by
// index, thinking summarized and signed, sent back as it came; the prompt
// cached up to its last block, the cache moving with the conversation; what
// the model lists of itself sets the tokens it may write and whether it
// thinks; a signed block is bound to the system, the tools and the messages
// before it, so a block the configuration changed since is dropped by the
// API, never refused; the twin of messages.ts of the page

#include "lib/api.h"
#include "yyjson.h"

#include <map>
#include <memory>

static const char * const NAME      = "messages";
static const char * const VERSION   = "2023-06-01";
// the parameters of models sent as they are named
static const char * const CARRIED[] = { "temperature", "top_p", "top_k" };
static const char * const BETA      = "anthropic-beta";
static const char * const BINDING   = "thinking-binding-controls-2026-08-01";
static const char * const THINKING =
    R"({"type":"adaptive","display":"summarized","block_binding":{"prefix_mismatch_behavior":"drop_block"}})";
// the field of the list that holds the most tokens a model writes
static const char * const MAX_TOKENS = "max_tokens";

using doc_t = std::shared_ptr<yyjson_doc>;

static doc_t parsed(const std::string & json) {
    return doc_t(yyjson_read(json.data(), json.size(), 0), yyjson_doc_free);
}

static void add_text(yyjson_mut_doc * d, yyjson_mut_val * o, const char * key, const std::string & v) {
    yyjson_mut_obj_add_strncpy(d, o, key, v.data(), v.size());
}

// a JSON text as a value of the document, null when it reads as none
static yyjson_mut_val * value_of(yyjson_mut_doc * d, const std::string & json) {
    const doc_t v = parsed(json);
    return v ? yyjson_val_mut_copy(d, yyjson_doc_get_root(v.get())) : yyjson_mut_null(d);
}

// the arguments of a call as an object, none when they read as anything else
static yyjson_mut_val * input(yyjson_mut_doc * d, const std::string & args) {
    const doc_t v = parsed(args);
    return v && yyjson_is_obj(yyjson_doc_get_root(v.get())) ? yyjson_val_mut_copy(d, yyjson_doc_get_root(v.get())) :
                                                              yyjson_mut_obj(d);
}

// whether the model takes images, which it does unless its list says
// otherwise, and whether it thinks as adaptive thinking asks, the same
static bool supported(const std::string & info, const char * path) {
    const doc_t  doc = parsed(info);
    yyjson_val * v   = doc ? yyjson_ptr_get(yyjson_doc_get_root(doc.get()), path) : nullptr;
    return !yyjson_is_false(v);
}

// a block of a turn
static yyjson_mut_val * block(yyjson_mut_doc * d, yyjson_mut_val * content, const char * type) {
    yyjson_mut_val * b = yyjson_mut_arr_add_obj(d, content);
    yyjson_mut_obj_add_str(d, b, "type", type);
    return b;
}

// the blocks of the next turn of a role: those of the last turn when it is
// of that role, as the API takes one role after the other
static yyjson_mut_val * turn(yyjson_mut_doc * d, yyjson_mut_val * list, const char * role) {
    yyjson_mut_val * last = yyjson_mut_arr_get_last(list);
    if (last && yyjson_mut_equals_str(yyjson_mut_obj_get(last, "role"), role)) {
        return yyjson_mut_obj_get(last, "content");
    }
    yyjson_mut_val * t = yyjson_mut_arr_add_obj(d, list);
    yyjson_mut_obj_add_str(d, t, "role", role);
    return yyjson_mut_obj_add_arr(d, t, "content");
}

// the history in blocks: cli messages stay out; every round of an assistant
// turn becomes the blocks its protocol kept when it is this one, its text and
// its calls, then a user turn of their results, a result holding its images
// after its text when the model sees them; turns of one role in a row merge,
// and a turn of no block is none
static void history(yyjson_mut_doc *                        d,
                    yyjson_mut_val *                        list,
                    const std::vector<conversation_entry> & messages,
                    bool                                    images) {
    for (const conversation_entry & e : messages) {
        if (e.role == ROLE_USER) {
            add_text(d, block(d, turn(d, list, "user"), "text"), "text", e.text);
        }
        if (e.role != ROLE_ASSISTANT) {
            continue;
        }
        for (const conversation_round & r : e.rounds) {
            const bool kept = r.opaque && r.opaque->protocol == NAME && r.opaque->items != "[]";
            if (kept || !r.text.empty() || !r.calls.empty()) {
                yyjson_mut_val * content = turn(d, list, "assistant");
                if (kept) {
                    const doc_t  items = parsed(r.opaque->items);
                    size_t       i, n;
                    yyjson_val * item;
                    yyjson_arr_foreach(yyjson_doc_get_root(items.get()), i, n, item) {
                        yyjson_mut_arr_append(content, yyjson_val_mut_copy(d, item));
                    }
                }
                if (!r.text.empty()) {
                    add_text(d, block(d, content, "text"), "text", r.text);
                }
                for (const conversation_call & c : r.calls) {
                    yyjson_mut_val * b = block(d, content, "tool_use");
                    add_text(d, b, "id", c.id);
                    add_text(d, b, "name", c.name);
                    yyjson_mut_obj_add_val(d, b, "input", input(d, c.args));
                }
            }
            if (r.calls.empty()) {
                continue;
            }
            yyjson_mut_val * results = turn(d, list, "user");
            for (const conversation_call & c : r.calls) {
                yyjson_mut_val * b = block(d, results, "tool_result");
                add_text(d, b, "tool_use_id", c.id);
                if (images && !c.images.empty()) {
                    yyjson_mut_val * parts = yyjson_mut_obj_add_arr(d, b, "content");
                    add_text(d, block(d, parts, "text"), "text", c.result);
                    for (const conversation_image & i : c.images) {
                        yyjson_mut_val * source = yyjson_mut_obj_add_obj(d, block(d, parts, "image"), "source");
                        yyjson_mut_obj_add_str(d, source, "type", "base64");
                        add_text(d, source, "media_type", i.mime);
                        add_text(d, source, "data", i.data);
                    }
                } else {
                    add_text(d, b, "content", c.result);
                }
                if (c.ok == false) {
                    yyjson_mut_obj_add_true(d, b, "is_error");
                }
            }
        }
    }
}

// the value of a parameter of the model, JSON, empty when it is not set
static std::string parameter(const api_request & r, const char * name) {
    for (const auto & p : r.parameters) {
        if (p.first == name) {
            return p.second;
        }
    }
    return "";
}

static std::string body(const api_request & r) {
    std::string max = parameter(r, "max_tokens");
    if (max.empty()) {
        const doc_t  info   = parsed(r.info);
        yyjson_val * listed = info ? yyjson_obj_get(yyjson_doc_get_root(info.get()), MAX_TOKENS) : nullptr;
        if (listed && !yyjson_is_null(listed)) {
            const std::shared_ptr<char> text(yyjson_val_write(listed, 0, nullptr), free);
            max = text.get();
        }
    }
    if (max.empty()) {
        throw api_error(std::string(NAME) + " needs models max_tokens",
                        "set models max_tokens <endpoint/model> <tokens>");
    }
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc *                      d    = doc.get();
    yyjson_mut_val *                      root = yyjson_mut_obj(d);
    yyjson_mut_doc_set_root(d, root);
    add_text(d, root, "model", r.model);
    yyjson_mut_obj_add_val(d, root, "max_tokens", value_of(d, max));
    for (const char * k : CARRIED) {
        const std::string v = parameter(r, k);
        if (!v.empty()) {
            yyjson_mut_obj_add_val(d, root, k, value_of(d, v));
        }
    }
    if (supported(r.info, "/capabilities/thinking/types/adaptive/supported")) {
        yyjson_mut_obj_add_val(d, root, "thinking", value_of(d, THINKING));
    }
    const std::string effort = parameter(r, "reasoning_effort");
    if (!effort.empty()) {
        yyjson_mut_obj_add_val(d, yyjson_mut_obj_add_obj(d, root, "output_config"), "effort", value_of(d, effort));
    }
    yyjson_mut_obj_add_str(d, yyjson_mut_obj_add_obj(d, root, "cache_control"), "type", "ephemeral");
    if (!r.system.empty()) {
        add_text(d, root, "system", r.system);
    }
    history(d, yyjson_mut_obj_add_arr(d, root, "messages"), r.messages,
            supported(r.info, "/capabilities/image_input/supported"));
    if (!r.tools.empty()) {
        yyjson_mut_val * tools = yyjson_mut_obj_add_arr(d, root, "tools");
        for (const api_tool & t : r.tools) {
            yyjson_mut_val * tool = yyjson_mut_arr_add_obj(d, tools);
            add_text(d, tool, "name", t.name);
            add_text(d, tool, "description", t.description);
            yyjson_mut_obj_add_val(d, tool, "input_schema", value_of(d, t.input));
            yyjson_mut_obj_add_true(d, tool, "eager_input_streaming");
        }
    }
    yyjson_mut_obj_add_true(d, root, "stream");
    const std::shared_ptr<char> text(yyjson_mut_write(d, 0, nullptr), free);
    return text.get();
}

// the betas of the endpoint join the one the binding of thinking needs, as
// the API takes a list of them, instead of replacing it; the version goes
// unless the endpoint names its own
static void headers(httplib::Headers & h) {
    if (!h.count("anthropic-version")) {
        h.emplace("anthropic-version", VERSION);
    }
    const auto        it     = h.find(BETA);
    const std::string theirs = it == h.end() ? "" : it->second;
    h.erase(BETA);
    h.emplace(BETA, theirs.empty() ? std::string(BINDING) : std::string(BINDING) + "," + theirs);
}

static std::string text_of(yyjson_val * v) {
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : "";
}

static std::string json_of(yyjson_val * v) {
    const std::shared_ptr<char> text(yyjson_val_write(v, 0, nullptr), free);
    return text.get();
}

// a thinking block as it streams: its text and its signature
struct thought {
    std::string thinking;
    std::string signature;
};

// a thinking block is kept once it closes, its text and its signature whole;
// a redacted one as it opens; the tokens of the prompt, read from the cache,
// written to it or neither, come as the message starts, those generated so
// far with every change of the message
static std::function<std::optional<api_delta>(const std::string &)> reader() {
    auto thinking = std::make_shared<std::map<int, thought>>();
    auto prompt   = std::make_shared<std::optional<double>>();
    return [thinking, prompt](const std::string & data) -> std::optional<api_delta> {
        const doc_t doc = parsed(data);
        if (!doc) {
            throw std::runtime_error("not JSON");
        }
        yyjson_val *      e     = yyjson_doc_get_root(doc.get());
        const std::string type  = text_of(yyjson_obj_get(e, "type"));
        const int         index = (int) yyjson_get_int(yyjson_obj_get(e, "index"));
        api_delta         out;
        if (type == "message_start") {
            yyjson_val * u = yyjson_obj_get(yyjson_obj_get(e, "message"), "usage");
            for (const char * count : { "input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens" }) {
                yyjson_val * n = yyjson_obj_get(u, count);
                if (yyjson_is_num(n)) {
                    *prompt = prompt->value_or(0) + yyjson_get_num(n);
                }
            }
            return std::nullopt;
        }
        if (type == "content_block_start") {
            yyjson_val *      b    = yyjson_obj_get(e, "content_block");
            const std::string kind = text_of(yyjson_obj_get(b, "type"));
            if (kind == "thinking") {
                (*thinking)[index] = {};
            }
            if (kind == "redacted_thinking") {
                out.opaque = json_of(b);
                return out;
            }
            if (kind == "tool_use") {
                out.calls.push_back(
                    { index, text_of(yyjson_obj_get(b, "id")), text_of(yyjson_obj_get(b, "name")), "" });
                return out;
            }
            if (kind == "text" && !text_of(yyjson_obj_get(b, "text")).empty()) {
                out.content = text_of(yyjson_obj_get(b, "text"));
                return out;
            }
            return std::nullopt;
        }
        if (type == "content_block_delta") {
            yyjson_val *      delta = yyjson_obj_get(e, "delta");
            const std::string kind  = text_of(yyjson_obj_get(delta, "type"));
            if (kind == "text_delta") {
                out.content = text_of(yyjson_obj_get(delta, "text"));
                return out;
            }
            if (kind == "input_json_delta") {
                out.calls.push_back({ index, "", "", text_of(yyjson_obj_get(delta, "partial_json")) });
                return out;
            }
            const auto block = thinking->find(index);
            if (block != thinking->end() && kind == "thinking_delta") {
                out.reasoning = text_of(yyjson_obj_get(delta, "thinking"));
                block->second.thinking += out.reasoning;
                return out;
            }
            if (block != thinking->end() && kind == "signature_delta") {
                block->second.signature += text_of(yyjson_obj_get(delta, "signature"));
            }
            return std::nullopt;
        }
        if (type == "content_block_stop") {
            const auto block = thinking->find(index);
            if (block == thinking->end()) {
                return std::nullopt;
            }
            const std::shared_ptr<yyjson_mut_doc> made(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
            yyjson_mut_val *                      o = yyjson_mut_obj(made.get());
            yyjson_mut_doc_set_root(made.get(), o);
            yyjson_mut_obj_add_str(made.get(), o, "type", "thinking");
            add_text(made.get(), o, "thinking", block->second.thinking);
            add_text(made.get(), o, "signature", block->second.signature);
            const std::shared_ptr<char> text(yyjson_mut_write(made.get(), 0, nullptr), free);
            out.opaque = text.get();
            return out;
        }
        if (type == "message_delta") {
            yyjson_val * usage = yyjson_obj_get(yyjson_obj_get(e, "usage"), "output_tokens");
            out.cut            = text_of(yyjson_obj_get(yyjson_obj_get(e, "delta"), "stop_reason")) == "max_tokens";
            if (!yyjson_is_num(usage)) {
                return out.cut ? std::optional<api_delta>(out) : std::nullopt;
            }
            out.usage = yyjson_get_num(usage);
            if (*prompt) {
                out.context = **prompt + *out.usage;
            }
            return out;
        }
        if (type == "message_stop") {
            out.end = true;
            return out;
        }
        if (type == "error") {
            out.error = text_of(yyjson_obj_get(yyjson_obj_get(e, "error"), "message"));
            return out;
        }
        return std::nullopt;
    };
}

const protocol & protocol_messages() {
    static const protocol p{
        NAME,
        "/messages",
        "/models?limit=1000",
        true,
        "x-api-key",
        headers,
        { "min_p", "presence_penalty", "frequency_penalty", "seed" },
        body,
        reader,
        "message_stop"
    };
    return p;
}
