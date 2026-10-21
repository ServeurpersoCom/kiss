// protocol.cpp: the laws of the protocols, run as the build ends: chat writes
// one system message first, the parameters of the model as they are typed,
// every round as one assistant message, its calls, their results, then one
// user message of their images for a model that sees, none for one whose
// list says it does not; it reads a delta, pieces of calls by their index,
// the count of the endpoint, an error, and the end; messages and responses
// write, from one history, the very bytes their twins of the page write,
// and read their streams into text, thinking, calls, the items they need
// back, the count and the end

#include "lib/api.h"
#include "yyjson.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

const protocol & protocol_chat();
const protocol & protocol_messages();
const protocol & protocol_responses();

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] protocol_test broken: %s\n", what.c_str());
    }
}

// the messages of a body, each as JSON
static std::string messages(const std::string & body) {
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(body.data(), body.size(), 0), yyjson_doc_free);
    const std::shared_ptr<char>       text(
        yyjson_val_write(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "messages"), 0, nullptr), free);
    return text.get();
}

int main() {
    const protocol & chat = protocol_chat();

    api_request r;
    r.model      = "m";
    r.system     = "be brief";
    r.parameters = {
        { "temperature",      "0.2"     },
        { "reasoning_effort", "\"low\"" }
    };
    conversation_entry user;
    user.role = ROLE_USER;
    user.text = "look";
    conversation_entry answer;
    answer.role = ROLE_ASSISTANT;
    conversation_round round;
    round.reasoning = "think";
    round.text      = "here";
    round.calls.push_back({ "c1", "view", "{\"path\":\"a.png\"}", "", true, { { "image/png", "AAAA" } }, false });
    answer.rounds = { round };
    conversation_entry command;
    command.role  = ROLE_CLI;
    command.input = "show version";
    r.messages    = { user, command, answer };
    r.tools       = {
        { "view", "views", "{\"type\":\"object\"}" }
    };

    const std::string body = chat.body(r);
    law(body.find("\"temperature\":0.2,\"reasoning_effort\":\"low\",\"model\":\"m\"") != std::string::npos,
        "the parameters of the model go as they are typed");
    law(messages(body) ==
            "[{\"role\":\"system\",\"content\":\"be brief\"},{\"role\":\"user\",\"content\":\"look\"},"
            "{\"role\":\"assistant\",\"content\":\"here\",\"reasoning_content\":\"think\",\"tool_calls\":[{\"id\":"
            "\"c1\",\"type\":\"function\",\"function\":{\"name\":\"view\",\"arguments\":\"{\\\"path\\\":\\\"a.png\\\"}"
            "\"}}]},"
            "{\"role\":\"tool\",\"tool_call_id\":\"c1\",\"content\":\"\"},"
            "{\"role\":\"user\",\"content\":[{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/"
            "png;base64,AAAA\"}}]}]",
        "the system first, a command left out, a round, its call, its result, then its images");
    law(body.find(
            "\"tools\":[{\"type\":\"function\",\"function\":{\"name\":\"view\",\"description\":\"views\","
            "\"parameters\":{\"type\":\"object\"}}}],\"stream\":true,\"stream_options\":{\"include_usage\":true}") !=
            std::string::npos,
        "the tools as functions, the stream counting its tokens");
    r.info = "{\"id\":\"m\",\"architecture\":{\"input_modalities\":[\"text\"]}}";
    law(messages(chat.body(r)).find("image_url") == std::string::npos, "a model that sees no image gets none");
    r.info = "{\"id\":\"m\",\"architecture\":{\"input_modalities\":[\"text\",\"image\"]}}";
    law(messages(chat.body(r)).find("image_url") != std::string::npos, "a model that sees images gets them");

    const auto read = chat.reader();
    const auto d    = read(
        "{\"choices\":[{\"delta\":{\"content\":\"a\",\"reasoning_content\":\"b\",\"tool_calls\":["
           "{\"index\":1,\"id\":\"x\",\"function\":{\"name\":\"ba\",\"arguments\":\"{\"}}]}}]}");
    law(d && d->content == "a" && d->reasoning == "b" && d->calls.size() == 1 && d->calls[0].index == 1 &&
            d->calls[0].id == "x" && d->calls[0].name == "ba" && d->calls[0].args == "{",
        "a delta reads its text, its thinking and the pieces of its calls by their index");
    const auto u = read("{\"choices\":[],\"usage\":{\"completion_tokens\":42}}");
    law(u && u->usage && *u->usage == 42, "the last chunk gives the count of the endpoint");
    law(!read("{\"choices\":[]}"), "an event that says nothing is skipped");
    const auto e = read("{\"error\":{\"message\":\"boom\"}}");
    law(e && e->error && *e->error == "boom", "an error is read with its message");
    law(read("[DONE]")->end, "[DONE] ends the stream");

    {
        // one history with every kind of round, written as tests/agent.test.ts
        // of the page writes it; webui/tests/bodies.jsonl holds what both
        // write, a line per body
        conversation_entry look;
        look.role = ROLE_USER;
        look.text = "look";
        conversation_entry again;
        again.role = ROLE_USER;
        again.text = "again";
        conversation_entry turn;
        turn.role = ROLE_ASSISTANT;
        conversation_round first;
        first.reasoning = "think";
        first.text      = "here";
        first.calls.push_back(
            { "c1", "view", "{\"path\":\"a.png\"}", "seen", true, { { "image/png", "AAAA" } }, false });
        conversation_round second;
        second.calls.push_back({ "c2", "bash", "not json", "boom", false, {}, false });
        conversation_round third;
        third.text      = "done";
        const auto with = [&](const std::string & protocol, const std::string & items) {
            api_request q;
            q.model      = "m";
            q.system     = "be brief";
            q.parameters = {
                { "temperature",      "0.2"     },
                { "top_k",            "40"      },
                { "max_tokens",       "1024"    },
                { "reasoning_effort", "\"low\"" }
            };
            q.tools = {
                { "view", "views", "{\"type\":\"object\"}" }
            };
            conversation_round kept = first;
            kept.opaque             = conversation_opaque{ protocol, items };
            conversation_entry t    = turn;
            t.rounds                = { kept, second, third };
            q.messages              = { look, command, t, again };
            return q;
        };
        api_request m = with("messages", "[{\"type\":\"thinking\",\"thinking\":\"think\",\"signature\":\"s\"}]");
        m.info =
            "{\"id\":\"m\",\"max_tokens\":64000,\"capabilities\":{\"image_input\":{\"supported\":true},"
            "\"thinking\":{\"types\":{\"adaptive\":{\"supported\":true}}}}}";
        std::ifstream            in(BODIES);
        std::vector<std::string> bodies;
        for (std::string line; std::getline(in, line);) {
            bodies.push_back(line);
        }
        law(bodies.size() == 4, "bodies.jsonl holds four bodies");
        bodies.resize(4);
        law(protocol_chat().body(m) == bodies[3], "chat writes what the page writes of the same history");
        law(protocol_messages().body(m) == bodies[0],
            "messages writes what the page writes: thinking first, merged turns, results with images, an error");
        api_request o = with("responses",
                             "[{\"type\":\"reasoning\",\"id\":\"r1\",\"encrypted_content\":\"x\","
                             "\"summary\":[]},{\"type\":\"message\",\"phase\":\"final_answer\"}]");
        o.parameters  = {
            { "temperature",      "0.2"     },
            { "max_tokens",       "1024"    },
            { "reasoning_effort", "\"low\"" }
        };
        law(protocol_responses().body(o) == bodies[1],
            "responses writes what the page writes: reasoning kept, the phase of the text, calls and outputs");
        api_request blind = with("responses", "[]");
        blind.parameters  = {};
        blind.info =
            "{\"id\":\"m\",\"max_tokens\":64000,\"capabilities\":{\"image_input\":{\"supported\":false},"
            "\"thinking\":{\"types\":{\"adaptive\":{\"supported\":false}}}}}";
        law(protocol_messages().body(blind) == bodies[2],
            "messages sends no image and no thinking to a model whose list says it takes none, its tokens from it");
        blind.info = "{}";
        bool needs = false;
        try {
            protocol_messages().body(blind);
        } catch (const api_error & e) {
            needs = !e.fix.empty();
        }
        law(needs, "messages without max_tokens set or listed asks for it");

        httplib::Headers h{
            { "Anthropic-Beta", "mine" }
        };
        protocol_messages().headers(h);
        law(h.count("anthropic-version") == 1 &&
                h.find("anthropic-beta")->second == "thinking-binding-controls-2026-08-01,mine",
            "messages adds its version and joins its beta to those of the endpoint");

        const auto mr = protocol_messages().reader();
        mr("{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\"}}");
        const auto t =
            mr("{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\","
               "\"thinking\":\"hm\"}}");
        mr("{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":"
           "\"sig\"}}");
        const auto kept = mr("{\"type\":\"content_block_stop\",\"index\":0}");
        const auto call =
            mr("{\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"u\","
               "\"name\":\"bash\"}}");
        const auto args =
            mr("{\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":"
               "\"input_json_delta\",\"partial_json\":\"{\\\"c\"}}");
        const auto count = mr("{\"type\":\"message_delta\",\"usage\":{\"output_tokens\":7}}");
        law(t && t->reasoning == "hm" && kept &&
                kept->opaque == "{\"type\":\"thinking\",\"thinking\":\"hm\",\"signature\":\"sig\"}" && call &&
                call->calls[0].index == 1 && call->calls[0].id == "u" && call->calls[0].name == "bash" && args &&
                args->calls[0].args == "{\"c" && count && *count->usage == 7 &&
                mr("{\"type\":\"message_stop\"}")->end && !mr("{\"type\":\"ping\"}"),
            "messages reads thinking kept whole with its signature, a call by its index, the count and the end");

        const auto rr = protocol_responses().reader();
        const auto add =
            rr("{\"type\":\"response.output_item.added\",\"output_index\":2,\"item\":{\"type\":"
               "\"function_call\",\"call_id\":\"f\",\"name\":\"view\"}}");
        const auto part = rr("{\"type\":\"response.reasoning_summary_part.added\",\"summary_index\":1}");
        const auto item = rr("{\"type\":\"response.output_item.done\",\"item\":{\"type\":\"reasoning\",\"id\":\"r\"}}");
        const auto phase =
            rr("{\"type\":\"response.output_item.done\",\"item\":{\"type\":\"message\",\"phase\":\"commentary\"}}");
        const auto done = rr("{\"type\":\"response.completed\",\"response\":{\"usage\":{\"output_tokens\":5}}}");
        law(add && add->calls[0].index == 2 && add->calls[0].id == "f" && part && part->reasoning == "\n\n" && item &&
                item->opaque == "{\"type\":\"reasoning\",\"id\":\"r\"}" && phase &&
                phase->opaque == "{\"type\":\"message\",\"phase\":\"commentary\"}" && done && done->end &&
                *done->usage == 5 && *rr("{\"type\":\"error\",\"message\":\"no\"}")->error == "no",
            "responses reads a call by its item, the parts of a summary, the items it needs back, the end");

        const auto held = read("{\"choices\":[],\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":2}}");
        const auto mh   = protocol_messages().reader();
        mh("{\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":3,"
           "\"cache_read_input_tokens\":4,\"cache_creation_input_tokens\":1}}}");
        const auto mc = mh("{\"type\":\"message_delta\",\"usage\":{\"output_tokens\":2}}");
        const auto rc = protocol_responses().reader()(
            "{\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":5,\"output_tokens\":2}}}");
        const auto chat_cut = read("{\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}");
        const auto chat_end = read("{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}");
        const auto msg_cut =
            mh("{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\"},"
               "\"usage\":{\"output_tokens\":2}}");
        const auto rsp_cut = protocol_responses().reader()("{\"type\":\"response.incomplete\",\"response\":{}}");
        law(chat_cut && chat_cut->cut && chat_end && !chat_end->cut && msg_cut && msg_cut->cut && !mc->cut && rsp_cut &&
                rsp_cut->cut && rsp_cut->end && !rc->cut,
            "an answer the output limit cuts says so, in every protocol");
        law(held && held->context && *held->context == 7 && mc && mc->context && *mc->context == 10 && rc &&
                rc->context && *rc->context == 7 && u && !u->context,
            "the tokens a request holds come as its prompt and what it generated, in every protocol, the cache of "
            "messages counted, none when the prompt goes untold");
    }

    printf("[Laws] protocol_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
