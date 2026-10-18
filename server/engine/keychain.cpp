// keychain.cpp: the placement of a key, the check of a key presented and the
// list of the secrets

#include "engine/keychain.h"

#include "engine/schema.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>

// a character of a header name or of a scheme, a token of HTTP
static bool tchar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           std::strchr("!#$%&'*+-.^_`|~", c) != nullptr;
}

static bool token(const std::string & s) {
    for (char c : s) {
        if (!tchar(c)) {
            return false;
        }
    }
    return !s.empty();
}

bool placement(const std::string & auth, std::string & name, std::string & scheme) {
    const size_t colon = auth.find(':');
    name               = trim(auth.substr(0, colon));
    scheme             = colon == std::string::npos ? "" : trim(auth.substr(colon + 1));
    return token(name) && (scheme.empty() || token(scheme));
}

bool credential(const engine_reader & config,
                const std::string &   auth,
                const std::string &   module,
                const std::string &   item,
                std::string &         header,
                std::string &         value) {
    std::string scheme;
    const auto  key = config.reveal(module + " key", item);
    if (!key || key->empty() || !placement(auth, header, scheme)) {
        return false;
    }
    value = scheme.empty() ? *key : scheme + " " + *key;
    return true;
}

bool keychain_admits(const std::string & presented) {
    std::optional<std::string> key;
    engine_with([&](const engine_reader & config) { key = config.reveal("server key"); });
    if (!key) {
        return false;
    }
    unsigned char diff = (unsigned char) (presented.size() != key->size());
    for (size_t i = 0; i < presented.size() || i < key->size(); i++) {
        diff |= (unsigned char) ((i < presented.size() ? presented[i] : 0) ^ (i < key->size() ? (*key)[i] : 0));
    }
    return diff == 0;
}

// the longest first, so a secret that holds another goes whole
static void longest_first(words_t & secrets) {
    std::sort(secrets.begin(), secrets.end(), [](const std::string & a, const std::string & b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    secrets.erase(std::unique(secrets.begin(), secrets.end()), secrets.end());
}

words_t keychain_secrets(const engine_reader & config) {
    words_t secrets;
    for (const engine_module & m : schema_modules()) {
        for (const engine_key & k : m.keys) {
            if (k.kind != ENGINE_SECRET) {
                continue;
            }
            for (const std::string & item : k.named ? config.names(m.name) : words_t{ "" }) {
                const auto value = config.reveal(m.name + " " + k.name, item);
                if (value && !value->empty()) {
                    secrets.push_back(*value);
                }
            }
        }
    }
    longest_first(secrets);
    return secrets;
}

// every secret this server has known: it only grows, so a secret the
// configuration drops stays sealed in a conversation or an output that still
// holds it
static std::mutex g_known_mutex;
static words_t    g_known;

words_t keychain_known() {
    words_t now;
    engine_with([&](const engine_reader & config) { now = keychain_secrets(config); });
    std::lock_guard<std::mutex> lock(g_known_mutex);
    g_known.insert(g_known.end(), now.begin(), now.end());
    longest_first(g_known);
    return g_known;
}
