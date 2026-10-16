// conversation.cpp: the laws of the conversations as files, run as the build
// ends: a file reads as the page reads it, whole or refused with the very
// place the page names, and the library holds one conversation per file named
// by its id, leaves out a file that does not read, and erases a file with its
// conversation

#include "lib/library.h"
#include "yyjson.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "conversation_test: %s\n", what.c_str());
    }
}

// a conversation holding one turn of each kind, as the page writes it
static const char * const DEMO = R"({
    "kiss": "conversations",
    "conversations": [{
        "id": "c", "title": "Demo", "updated": 1760000000000, "pinned": true, "leaf": "e3",
        "entries": [
            { "role": "user", "text": "translate hi", "id": "e1", "parent": null, "time": 1760000000000 },
            { "role": "assistant", "rounds": [
                { "reasoning": "think", "text": "first", "calls": [
                    { "id": "a", "name": "bash", "args": "{\"c\":\"ls\"}", "result": "out", "ok": true,
                      "images": [{ "mime": "image/png", "data": "AAAA" }] }] },
                { "reasoning": "again", "text": "", "calls": [],
                  "opaque": { "protocol": "messages", "items": [{ "type": "thinking", "signature": "s" }] } },
                { "reasoning": "", "text": "salut", "calls": [] }],
              "error": "stopped", "stats": { "tokens": 3, "generation": 20, "system": 50 },
              "id": "e2", "parent": "e1", "time": 1760000000001 },
            { "role": "cli", "input": "show title", "output": "title Demo", "ok": true,
              "id": "e3", "parent": "e2", "time": 1760000000002 }]
    }]
})";

// what parsing says of a text, empty when it reads
static std::string refusal(const std::string & text) {
    try {
        conversation_parse(text);
        return "";
    } catch (const std::runtime_error & e) {
        return e.what();
    }
}

// what parsing says of the demo changed by change
static std::string altered(const std::function<void(yyjson_mut_doc *)> & change) {
    const std::shared_ptr<yyjson_doc>     in(yyjson_read(DEMO, strlen(DEMO), 0), yyjson_doc_free);
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_doc_mut_copy(in.get(), nullptr), yyjson_mut_doc_free);
    change(doc.get());
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return refusal(text.get());
}

static void set(yyjson_mut_doc * d, const char * at, yyjson_mut_val * v) {
    yyjson_mut_doc_ptr_set(d, at, v);
}

static void drop(yyjson_mut_doc * d, const char * at) {
    yyjson_mut_doc_ptr_remove(d, at);
}

static yyjson_mut_val * copy(yyjson_mut_doc * d, const char * at) {
    return yyjson_mut_val_mut_copy(d, yyjson_mut_doc_ptr_get(d, at));
}

static void write(const fs::path & path, const std::string & text) {
    FILE * f = fopen(path.u8string().c_str(), "wb");
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

int main() {
    const std::vector<conversation> read = conversation_parse(DEMO);
    law(read.size() == 1 && read[0].id == "c" && read[0].title == "Demo" && read[0].updated == 1760000000000 &&
            read[0].pinned,
        "a file the page writes reads with its id, title, date and pin");

    const std::string at = "file.conversations[0]";
    const auto        is = [](const std::string & got, const std::string & want) {
        law(got == want, "refused \"" + want + "\", got \"" + got + "\"");
    };
    is(refusal("{"), "not JSON");
    is(altered([](auto d) { set(d, "/kiss", yyjson_mut_str(d, "save")); }), "file.kiss: not conversations");
    is(altered([](auto d) { set(d, "/extra", yyjson_mut_int(d, 1)); }), "file.extra: unknown");
    is(altered([](auto d) { drop(d, "/conversations/0/title"); }), at + ".title: missing");
    is(altered([](auto d) { set(d, "/conversations/0/pinned", yyjson_mut_false(d)); }), at + ".pinned: not true");
    is(altered([](auto d) { drop(d, "/conversations/0/entries/0/time"); }), at + ".entries[0].time: missing");
    is(altered([](auto d) { yyjson_mut_doc_ptr_add(d, "/conversations/1", copy(d, "/conversations/0")); }),
       "file.conversations[1].id: used twice");
    is(altered([](auto d) { set(d, "/conversations/0/entries/0/role", yyjson_mut_str(d, "system")); }),
       at + ".entries[0].role: not one of user assistant cli");
    is(altered([](auto d) { drop(d, "/conversations/0/entries/1/rounds/0/calls/0/ok"); }),
       at + ".entries[1].rounds[0].calls[0].ok: missing");
    is(altered([](auto d) { set(d, "/conversations/0/entries/2/ok", yyjson_mut_str(d, "yes")); }),
       at + ".entries[2].ok: not a boolean");
    is(altered([](auto d) { set(d, "/conversations/0/entries/1/id", copy(d, "/conversations/0/entries/0/id")); }),
       at + ".entries[1].id: used twice");
    is(altered([](auto d) { set(d, "/conversations/0/entries/0/parent", copy(d, "/conversations/0/entries/2/id")); }),
       at + ".entries[0].parent: names no entry before it");
    is(altered([](auto d) { set(d, "/conversations/0/leaf", yyjson_mut_str(d, "gone")); }),
       at + ".leaf: names no entry");
    is(altered([](auto d) { set(d, "/conversations/0/leaf", copy(d, "/conversations/0/entries/0/id")); }),
       at + ".leaf: ends no branch");
    is(altered([](auto d) { set(d, "/conversations/0/leaf", yyjson_mut_null(d)); }), at + ".leaf: names no entry");

    const fs::path root = fs::temp_directory_path() / ("conversation_test." + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    std::string named = DEMO;
    named.replace(named.find("\"id\": \"c\""), 9, "\"id\": \"one\"");
    write(root / "one.json", named);
    write(root / "two.json", DEMO);
    write(root / "bad.json", "{");
    write(root / "notes.txt", "x");
    library_open(root.u8string());
    const std::vector<conversation> listed = library_list();
    law(listed.size() == 1 && listed[0].id == "one", "a file holds the conversation its name names, else stays out");
    library_remove({ "one" });
    law(library_list().empty() && !fs::exists(root / "one.json") && fs::exists(root / "two.json"),
        "an erased conversation goes with its file alone");

    fs::remove_all(root);
    printf("conversation_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
