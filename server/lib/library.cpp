// library.cpp: the conversations of the server, a file each, read once as it
// starts, held under one lock, each with the version of the library it last
// changed at, and the version of its last change other than a growth

#include "lib/library.h"

#include "engine/seal.h"
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

// a conversation, the versions of the library it last changed and grew at,
// and the entry a turn grows
struct held {
    conversation c;
    uint64_t     version   = 0;
    uint64_t     structure = 0;
    std::string  live;
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

static std::vector<held>::iterator find_held(const std::string & id) {
    return std::find_if(g_held.begin(), g_held.end(), [&](const held & h) { return h.c.id == id; });
}

// the newest first
static void sort_held() {
    std::stable_sort(g_held.begin(), g_held.end(),
                     [](const held & a, const held & b) { return a.c.updated > b.c.updated; });
}

// the file of a conversation as it settled; the message of the failure
static std::string write(const conversation & c, const words_t & secrets) {
    const std::string why = file_replace(path_of(c.id), conversation_file({ conversation_stored(c) }, secrets));
    if (!why.empty()) {
        log_error("Library", "%s: %s", path_of(c.id).u8string().c_str(), why.c_str());
    }
    return why;
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
            held h;
            h.c = read(e.path());
            g_held.push_back(std::move(h));
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

std::string library_create(const conversation & c, const words_t & secrets) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string           why = write(c, secrets);
    if (!why.empty()) {
        return why;
    }
    held h;
    h.c         = c;
    h.version   = ++g_version;
    h.structure = h.version;
    g_held.push_back(std::move(h));
    sort_held();
    g_changed.notify_all();
    return "";
}

std::string library_change(const std::string &                                id,
                           const std::function<std::string(conversation &)> & change,
                           const words_t &                                    secrets) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto                  it = find_held(id);
    if (it == g_held.end()) {
        return "unknown conversation \"" + id + "\"";
    }
    conversation      changed = it->c;
    const std::string refused = change(changed);
    if (!refused.empty()) {
        return refused;
    }
    const std::string why = write(changed, secrets);
    if (!why.empty()) {
        return why;
    }
    it->c         = std::move(changed);
    it->version   = ++g_version;
    it->structure = it->version;
    sort_held();
    g_changed.notify_all();
    return "";
}

bool library_grow(const std::string &                               id,
                  const std::string &                               entry,
                  const std::function<void(conversation_entry &)> & grow) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto                  it = find_held(id);
    if (it == g_held.end()) {
        return false;
    }
    for (conversation_entry & e : it->c.entries) {
        if (e.id == entry) {
            grow(e);
            it->version = ++g_version;
            if (it->live != entry) {
                it->live      = entry;
                it->structure = it->version;
            }
            g_changed.notify_all();
            return true;
        }
    }
    return false;
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

static std::string set_event(const std::string & path, const std::string & value) {
    return "data: {\"set\":[" + path + "],\"value\":" + value + "}\n\n";
}

static std::string add_value(const std::string & path, const std::string & value) {
    return "data: {\"add\":[" + path + "],\"value\":" + value + "}\n\n";
}

static std::string add_text(const std::string & path, const std::string & text, const words_t & secrets) {
    std::string sealed;
    seal_json(quoted(text), secrets, sealed);
    return "data: {\"add\":[" + path + "],\"text\":" + sealed + "}\n\n";
}

// the shape of a round, and of an entry, as they stand
static library_shape::round round_shape(const conversation_round & r) {
    library_shape::round s{ r.reasoning.size(), r.text.size(), {} };
    for (const conversation_call & c : r.calls) {
        s.calls.push_back({ c.name.size(), c.args.size(), c.ok.has_value() });
    }
    return s;
}

static library_shape shape_of(const conversation_entry & e) {
    library_shape s;
    s.entry = e.id;
    for (const conversation_round & r : e.rounds) {
        s.rounds.push_back(round_shape(r));
    }
    return s;
}

// what an entry grew by since its shape, the shape grown with it; at is the
// path of the entry
static std::string grown(const conversation_entry & e,
                         library_shape &            s,
                         const std::string &        at,
                         const words_t &            secrets) {
    std::string out;
    for (size_t r = 0; r < e.rounds.size(); r++) {
        const conversation_round & round = e.rounds[r];
        const std::string          rp    = at + ",\"rounds\"," + std::to_string(r);
        if (r == s.rounds.size()) {
            out += add_value(at + ",\"rounds\"", conversation_round_json(round, secrets));
            s.rounds.push_back(round_shape(round));
            continue;
        }
        library_shape::round & sr = s.rounds[r];
        if (round.reasoning.size() > sr.reasoning) {
            out += add_text(rp + ",\"reasoning\"", round.reasoning.substr(sr.reasoning), secrets);
            sr.reasoning = round.reasoning.size();
        }
        if (round.text.size() > sr.text) {
            out += add_text(rp + ",\"text\"", round.text.substr(sr.text), secrets);
            sr.text = round.text.size();
        }
        for (size_t c = 0; c < round.calls.size(); c++) {
            const conversation_call & call = round.calls[c];
            const std::string         cp   = rp + ",\"calls\"," + std::to_string(c);
            if (c == sr.calls.size()) {
                out += add_value(rp + ",\"calls\"", conversation_call_json(call, secrets));
                sr.calls.push_back({ call.name.size(), call.args.size(), call.ok.has_value() });
                continue;
            }
            library_shape::call & sc = sr.calls[c];
            if (call.ok && !sc.done) {
                out += set_event(cp, conversation_call_json(call, secrets));
                sc = { call.name.size(), call.args.size(), true };
                continue;
            }
            if (call.name.size() > sc.name) {
                out += add_text(cp + ",\"name\"", call.name.substr(sc.name), secrets);
                sc.name = call.name.size();
            }
            if (call.args.size() > sc.args) {
                out += add_text(cp + ",\"args\"", call.args.substr(sc.args), secrets);
                sc.args = call.args.size();
            }
        }
    }
    return out;
}

// what a follower holds of a conversation sent whole
static library_held whole(const held & h) {
    library_held out{ h.version, h.structure, {} };
    for (const conversation_entry & e : h.c.entries) {
        if (e.id == h.live) {
            out.shape = shape_of(e);
        }
    }
    return out;
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
            f.held[h.c.id] = whole(h);
        }
        return set_event("", "{\"conversations\":[" + list + "]}");
    }
    std::string out;
    for (const held & h : g_held) {
        const auto it = f.held.find(h.c.id);
        if (it == f.held.end() || it->second.structure != h.structure) {
            out += set_event(quoted(h.c.id), conversation_json(h.c, secrets));
            f.held[h.c.id] = whole(h);
            continue;
        }
        if (it->second.version == h.version) {
            continue;
        }
        it->second.version = h.version;
        for (size_t i = 0; i < h.c.entries.size(); i++) {
            if (h.c.entries[i].id == h.live) {
                out += grown(h.c.entries[i], it->second.shape, quoted(h.c.id) + ",\"entries\"," + std::to_string(i),
                             secrets);
            }
        }
    }
    for (auto it = f.held.begin(); it != f.held.end();) {
        if (find_held(it->first) != g_held.end()) {
            ++it;
            continue;
        }
        out += set_event(quoted(it->first), "null");
        it = f.held.erase(it);
    }
    return out;
}
