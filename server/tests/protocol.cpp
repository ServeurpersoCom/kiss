// protocol.cpp: the laws of the protocols, run as the build ends: chat writes
// one system message first, the parameters of the model as they are typed,
// every round as one assistant message, its calls, their results, then one
// user message of their images for a model that sees, none for one whose
// list says it does not; it reads a delta, pieces of calls by their index,
// the count of the endpoint, an error, and the end

#include "lib/api.h"
#include "yyjson.h"

#include <cstdio>
#include <cstring>
#include <memory>

const protocol & protocol_chat();

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "protocol_test: %s\n", what.c_str());
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

    printf("protocol_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
