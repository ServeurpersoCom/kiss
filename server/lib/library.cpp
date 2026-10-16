// library.cpp: the conversations of the server, a file each, read once as it
// starts and held under one lock

#include "lib/library.h"

#include "engine/text.h"
#include "lib/file.h"
#include "lib/log.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

static const char * const EXTENSION = ".json";

static std::mutex                g_mutex;
static fs::path                  g_dir;
static std::vector<conversation> g_conversations;

// the conversation of a file, its id the name of the file
static conversation read(const fs::path & path) {
    std::string       text;
    const std::string why = file_load(path, SIZE_MAX, text);
    if (!why.empty()) {
        throw std::runtime_error(why);
    }
    const std::vector<conversation> found = conversation_parse(text);
    if (found.size() != 1) {
        throw std::runtime_error("file.conversations: not one conversation");
    }
    if (found[0].id != path.stem().u8string()) {
        throw std::runtime_error("file.conversations[0].id: not the name of its file");
    }
    return found[0];
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
            g_conversations.push_back(read(e.path()));
        } catch (const std::runtime_error & err) {
            log_error("Library", "%s skipped: %s", e.path().u8string().c_str(), err.what());
        }
    }
    std::sort(g_conversations.begin(), g_conversations.end(),
              [](const conversation & a, const conversation & b) { return a.updated > b.updated; });
    const size_t n = g_conversations.size();
    log_info("Library", "%zu %s in %s", n, n == 1 ? "conversation" : "conversations", dir.c_str());
}

std::vector<conversation> library_list() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_conversations;
}

void library_remove(const std::vector<std::string> & ids) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string & id : ids) {
        const fs::path  path = g_dir / fs::u8path(id + EXTENSION);
        std::error_code ec;
        fs::remove(path, ec);
        if (ec) {
            fail("cannot erase " + path.u8string() + ": " + ec.message());
        }
        g_conversations.erase(std::remove_if(g_conversations.begin(), g_conversations.end(),
                                             [&](const conversation & c) { return c.id == id; }),
                              g_conversations.end());
        log_info("Library", "Erased %s", id.c_str());
    }
}
