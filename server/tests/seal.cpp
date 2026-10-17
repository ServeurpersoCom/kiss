// seal.cpp: the laws of the seal of the keychain, run as the build ends: a key
// a server writes back, as it is or escaped, whole or in pieces of any size,
// never reaches what a client decodes, and JSON stays JSON
//
// The client is the page: it reads a body as JSON when it is, and a stream as
// events, the data lines of each joined by LF and read as JSON. yyjson decodes
// what the client would, and every string, the names of properties included,
// is searched for every key.

#include "engine/seal.h"

#include "yyjson.h"

#include <cstdio>
#include <cstring>
#include <memory>

// the keys of the laws: plain, ending in a backslash, holding a backslash, a
// quote and a slash, and holding a character past U+FFFF
static const words_t KEYS = { "sk\\route\"q/0005", "sk-\xF0\x9F\x98\x80-0004", "sk-FAKE-END\\", "sk-proj-0001" };

// the most bytes a stream is cut into
static const size_t PIECE = 40;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] seal_test broken: %s\n", what.c_str());
    }
}

// every string a JSON value holds, the names of properties included
static void strings(yyjson_val * v, words_t & out) {
    size_t       i, n;
    yyjson_val * k;
    yyjson_val * x;
    if (yyjson_is_str(v)) {
        out.emplace_back(yyjson_get_str(v), yyjson_get_len(v));
    } else if (yyjson_is_arr(v)) {
        yyjson_arr_foreach(v, i, n, x) {
            strings(x, out);
        }
    } else if (yyjson_is_obj(v)) {
        yyjson_obj_foreach(v, i, n, k, x) {
            strings(k, out);
            strings(x, out);
        }
    }
}

// whether a JSON text decodes, and with no key in any string it holds
static bool clean_json(const std::string & json) {
    const std::shared_ptr<yyjson_doc> doc(yyjson_read(json.data(), json.size(), 0), yyjson_doc_free);
    if (!doc) {
        return false;
    }
    words_t seen;
    strings(yyjson_doc_get_root(doc.get()), seen);
    for (const std::string & s : seen) {
        for (const std::string & k : KEYS) {
            if (s.find(k) != std::string::npos) {
                return false;
            }
        }
    }
    return true;
}

// whether a stream reads as the client reads it with no key in it: each event
// closed by a blank line, its data lines joined by LF, JSON with no key in it
static bool clean_events(const std::string & stream) {
    words_t data;
    for (size_t at = 0; at < stream.size();) {
        const size_t      eol  = stream.find('\n', at);
        const std::string line = stream.substr(at, eol - at);
        at                     = eol + 1;
        if (line.empty()) {
            if (!data.empty() && !clean_json(join(data, "\n"))) {
                return false;
            }
            data.clear();
        } else if (line.compare(0, 6, "data: ") == 0) {
            data.push_back(line.substr(6));
        }
    }
    return data.empty();
}

// a string as JSON writes it inside quotes: as yyjson writes it, or with every
// character as \uXXXX, or with every other one so
static std::string escaped(const std::string & s, int way) {
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), yyjson_mut_strncpy(doc.get(), s.data(), s.size()));
    const yyjson_write_flag     flags = way ? YYJSON_WRITE_ESCAPE_UNICODE | YYJSON_WRITE_ESCAPE_SLASHES : 0;
    const std::shared_ptr<char> w(yyjson_mut_write(doc.get(), flags, nullptr), free);
    const std::string           q(w.get() + 1, strlen(w.get()) - 2);
    if (way == 0) {
        return q;
    }
    std::string out;
    for (size_t i = 0, n = 0; i < q.size(); i += n, n = 0) {
        n = q[i] == '\\' ? (q[i + 1] == 'u' ? 6 : 2) : 1;
        if (n == 1 && (way == 2 || i % 2)) {
            char u[8];
            snprintf(u, sizeof(u), "\\u%04x", (unsigned char) q[i]);
            out += u;
        } else {
            out += q.substr(i, n);
        }
    }
    return out;
}

// a body a server may answer, the key in a message and as the name of a
// property, padded so a stream cuts it far from its ends
static std::string body(const std::string & key, int way) {
    return "{\"error\":{\"message\":\"" + escaped("bad " + key + "\\ then", way) + "\",\"" + escaped(key, way) +
           "\":1.50},\"pad\":\"" + std::string(120, 'a') + "\"}";
}

// a stream of events, as a server streams it: each event a body, its data on
// one line or split over two between two of its members, its lines ending in
// LF or CRLF, a comment and a name among them
static std::string events(const std::string & key, int way) {
    const std::string b    = body(key, way);
    const std::string half = b.substr(0, b.find(",\"pad\""));
    return ": ping\n\nevent: delta\ndata: " + b + "\n\ndata: " + half + "\r\ndata: " + b.substr(half.size()) +
           "\r\n\r\ndata: " + b + "\n\n";
}

int main() {
    for (const std::string & key : KEYS) {
        for (int way = 0; way < 3; way++) {
            const std::string b = body(key, way);
            law(clean_json(seal_body(b, KEYS)), "a body keeps " + key);
            law(seal_body("bad " + key + " then", KEYS).find(key) == std::string::npos, "a text keeps " + key);
            law(seal_body(b, {}).find("1.50") != std::string::npos, "a number is written again");
            const std::string s = events(key, way);
            for (size_t piece = 1; piece <= PIECE; piece++) {
                seal_events e{ KEYS, "" };
                std::string out;
                for (size_t at = 0; at < s.size(); at += piece) {
                    const std::string p = s.substr(at, piece);
                    out += e.feed(p.data(), p.size());
                }
                out += e.end();
                law(clean_events(out), "a stream cut by " + std::to_string(piece) + " keeps " + key);
                seal_events       open{ KEYS, "" };
                const std::string left = "data: " + key + "\n";
                law(open.feed(left.data(), left.size()).empty() && open.end().empty(),
                    "an event left open passes " + key);
            }
        }
    }
    printf("[Laws] seal_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
