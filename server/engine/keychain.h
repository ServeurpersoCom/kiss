#pragma once
// keychain.h: the keychain of the server: the one reader of the value of a
// secret beside the engine that holds it, as the header of a route leaves
// and as the key of a client is checked, and the one filter what goes back to
// a client passes, every secret of the running configuration read <removed>:
// the answer of a proxied server, its headers, its body, each event of a
// stream, a refusal of the proxy, and the result of a tool; JSON as decoded,
// any other text as it is written

#include "engine.h"
#include "seal.h"
#include "text.h"

#include <string>

// where a key goes when nothing else says: Header, or Header: Scheme
constexpr const char * BEARER = "Authorization: Bearer";

// where a key goes, as a header name and its scheme, false when the words are
// no Header or Header: Scheme
bool placement(const std::string & auth, std::string & name, std::string & scheme);

// the header that carries the key of an item, placed as auth-header says, false
// without a key or a placement
bool credential(const engine_reader & config,
                const std::string &   auth,
                const std::string &   module,
                const std::string &   item,
                std::string &         header,
                std::string &         value);

// whether a client presents the key of this server, compared in constant time
bool keychain_admits(const std::string & presented);

// every secret a configuration holds, the longest first, so a secret that
// holds another goes whole
words_t keychain_secrets(const engine_reader & config);

// every secret of the running configuration
words_t keychain_running();
