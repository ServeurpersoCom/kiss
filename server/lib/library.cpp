// library.cpp: the conversations of the server, a file each, read once as it
// starts, held under one lock, each with the version of the library it last
// changed at

#include "lib/library.h"

#include "lib/file.h"
#include "lib/log.h"
#include "yyjson.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

static const char * const EXTENSION = ".json";

// a conversation and the version of the library it last changed at
struct held {
    conversation c;
    uint64_t     version;
};

static std::mutex              g_mutex;
static std::condition_variable g_changed;
static fs::path                g_dir;
static std::vector<held>       g_held;
static uint64_t                g_version = 0;

// the conversation of a file, its id the name of the file
static conversation read(const fs::path & path) {
    std::string       text;
    const std::string why = file_load(path, SIZE_MAX, text);
    if (!why.empty()) {
        throw std::runtime_error(why);
    }
    std::vector<conversation> found = conversation_parse(text);
    if (found.size() != 1) {
        throw std::runtime_error("file.conversations: not one conversation");
    }
    if (found[0].id != path.stem().u8string()) {
        throw std::runtime_error("file.conversations[0].id: not the name of its file");
    }
    return std::move(found[0]);
}

static fs::path path_of(const std::string & id) {
    return g_dir / fs::u8path(id + EXTENSION);
}

// the newest first
static void sort_held() {
    std::stable_sort(g_held.begin(), g_held.end(),
                     [](const held & a, const held & b) { return a.c.updated > b.c.updated; });
}

void library_open(const std::string & dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = fs::u8path(dir);
    std::error_code ec;
    for (const fs::directory_entry & e : fs::directory_iterator(g_dir, ec)) {
        if (e.path().extension() != EXTENSION) {
            continue;
        }
        try {
            g_held.push_back({ read(e.path()), 0 });
        } catch (const std::runtime_error & err) {
            log_error("Library", "%s skipped: %s", e.path().u8string().c_str(), err.what());
        }
    }
    sort_held();
    const size_t n = g_held.size();
    log_info("Library", "%zu %s in %s", n, n == 1 ? "conversation" : "conversations", dir.c_str());
}

void library_each(const std::function<void(const conversation &)> & read_one) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const held & h : g_held) {
        read_one(h.c);
    }
}

std::string library_change(const std::string &                                id,
                           const std::function<std::string(conversation &)> & change,
                           const words_t &                                    secrets) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = std::find_if(g_held.begin(), g_held.end(), [&](const held & h) { return h.c.id == id; });
    if (it == g_held.end()) {
        return "unknown conversation \"" + id + "\"";
    }
    conversation      changed = it->c;
    const std::string refused = change(changed);
    if (!refused.empty()) {
        return refused;
    }
    const std::string why = file_replace(path_of(id), conversation_file({ changed }, secrets));
    if (!why.empty()) {
        log_error("Library", "%s: %s", path_of(id).u8string().c_str(), why.c_str());
        return why;
    }
    it->c       = std::move(changed);
    it->version = ++g_version;
    sort_held();
    g_changed.notify_all();
    return "";
}

void library_remove(const words_t & ids) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string & id : ids) {
        std::error_code ec;
        fs::remove(path_of(id), ec);
        if (ec) {
            fail("cannot erase " + path_of(id).u8string() + ": " + ec.message());
        }
        g_held.erase(std::remove_if(g_held.begin(), g_held.end(), [&](const held & h) { return h.c.id == id; }),
                     g_held.end());
        ++g_version;
        g_changed.notify_all();
        log_info("Library", "Erased %s", id.c_str());
    }
}

// a string as JSON
static std::string quoted(const std::string & s) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return text.get();
}

static std::string event(const std::string & path, const std::string & value) {
    return "data: {\"set\":[" + path + "],\"value\":" + value + "}\n\n";
}

std::string library_follow(library_follower & f, int wait_ms, const words_t & secrets) {
    std::unique_lock<std::mutex> lock(g_mutex);
    if (f.started) {
        g_changed.wait_for(lock, std::chrono::milliseconds(wait_ms), [&] { return g_version != f.seen; });
    }
    f.seen = g_version;
    if (!f.started) {
        f.started = true;
        std::string list;
        for (const held & h : g_held) {
            list += (list.empty() ? "" : ",") + conversation_json(h.c, secrets);
            f.held[h.c.id] = h.version;
        }
        return event("", "{\"conversations\":[" + list + "]}");
    }
    std::string out;
    for (const held & h : g_held) {
        const auto it = f.held.find(h.c.id);
        if (it == f.held.end() || it->second != h.version) {
            out += event(quoted(h.c.id), conversation_json(h.c, secrets));
            f.held[h.c.id] = h.version;
        }
    }
    for (auto it = f.held.begin(); it != f.held.end();) {
        const bool kept =
            std::any_of(g_held.begin(), g_held.end(), [&](const held & h) { return h.c.id == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        out += event(quoted(it->first), "null");
        it = f.held.erase(it);
    }
    return out;
}
