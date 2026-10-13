#pragma once
// seal.h: the seal of the keychain: every secret it is given read <removed>,
// in a text as it is written, in JSON as decoded whatever escapes wrote it,
// the names of its properties included, and in a stream of events event by
// event

#include "text.h"

#include <string>

// a text with every secret replaced by <removed>, written as it is
std::string seal_text(const std::string & text, const words_t & secrets);

// a JSON text written again with every string and every name of a property
// sealed as decoded, a number as it was written; false when the text is no
// JSON
bool seal_json(const std::string & text, const words_t & secrets, std::string & out);

// a whole body: JSON as decoded, any other text as it is written
std::string seal_body(const std::string & body, const words_t & secrets);

// a stream of events sealed event by event, each once the blank line that
// closes it came, its data as a client joins it
struct seal_events {
    words_t     secrets;
    std::string pending;

    // the events the bytes complete, sealed
    std::string feed(const char * data, size_t n);
    // nothing: an event the stream leaves open is dropped, as a client drops it
    std::string end();
};
