// seal.cpp: a text, JSON and a stream of events, every secret read <removed>

#include "seal.h"

#include "yyjson.h"

#include <algorithm>
#include <cstring>
#include <memory>

std::string seal_text(const std::string & text, const words_t & secrets) {
    std::string out = text;
    for (const std::string & s : secrets) {
        for (size_t at = out.find(s); at != std::string::npos; at = out.find(s, at + std::strlen(REMOVED))) {
            out.replace(at, s.size(), REMOVED);
        }
    }
    return out;
}

// a JSON value copied into doc, every string and every name of a property
// filtered as decoded, a number kept as it was written
static yyjson_mut_val * sealed(yyjson_mut_doc * doc, yyjson_val * v, const words_t & secrets) {
    if (yyjson_is_str(v)) {
        const std::string t = seal_text(std::string(yyjson_get_str(v), yyjson_get_len(v)), secrets);
        return yyjson_mut_strncpy(doc, t.data(), t.size());
    }
    if (yyjson_is_arr(v)) {
        yyjson_mut_val * out = yyjson_mut_arr(doc);
        size_t           i, n;
        yyjson_val *     x;
        yyjson_arr_foreach(v, i, n, x) {
            yyjson_mut_arr_append(out, sealed(doc, x, secrets));
        }
        return out;
    }
    if (yyjson_is_obj(v)) {
        yyjson_mut_val * out = yyjson_mut_obj(doc);
        size_t           i, n;
        yyjson_val *     k;
        yyjson_val *     x;
        yyjson_obj_foreach(v, i, n, k, x) {
            yyjson_mut_obj_add(out, sealed(doc, k, secrets), sealed(doc, x, secrets));
        }
        return out;
    }
    return yyjson_val_mut_copy(doc, v);
}

bool seal_json(const std::string & text, const words_t & secrets, std::string & out) {
    const std::shared_ptr<yyjson_doc> in(yyjson_read(text.data(), text.size(), YYJSON_READ_NUMBER_AS_RAW),
                                         yyjson_doc_free);
    if (!in) {
        return false;
    }
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_doc_set_root(doc.get(), sealed(doc.get(), yyjson_doc_get_root(in.get()), secrets));
    size_t                      len = 0;
    const std::shared_ptr<char> json(yyjson_mut_write(doc.get(), 0, &len), free);
    if (!json) {
        return false;
    }
    out.assign(json.get(), len);
    return true;
}

std::string seal_body(const std::string & body, const words_t & secrets) {
    std::string json;
    return seal_json(body, secrets, json) ? json : seal_text(body, secrets);
}

// where the first event of a stream ends, after the blank line that closes
// it, a line ending in CRLF, LF or CR; npos while it is not whole
static size_t event_end(const std::string & s) {
    for (size_t at = 0;;) {
        const size_t eol = s.find_first_of("\r\n", at);
        if (eol == std::string::npos || (s[eol] == '\r' && eol + 1 == s.size())) {
            return std::string::npos;
        }
        const size_t next = eol + (s.compare(eol, 2, "\r\n") == 0 ? 2 : 1);
        if (eol == at) {
            return next;
        }
        at = next;
    }
}

// an event sealed: its data lines joined as a client joins them, sealed as
// JSON when they are, its other lines as they are written, every line ending
// in LF
static std::string sealed_event(const std::string & event, const words_t & secrets) {
    words_t     data;
    std::string out;
    for (size_t at = 0; at < event.size();) {
        const size_t      eol  = std::min(event.find_first_of("\r\n", at), event.size());
        const std::string line = event.substr(at, eol - at);
        at                     = eol + (event.compare(eol, 2, "\r\n") == 0 ? 2 : 1);
        if (starts_with(line, "data:")) {
            data.push_back(line.substr(line.compare(5, 1, " ") == 0 ? 6 : 5));
        } else if (!line.empty()) {
            out += seal_text(line, secrets) + "\n";
        }
    }
    std::string json;
    if (!data.empty() && seal_json(join(data, "\n"), secrets, json)) {
        out += "data: " + json + "\n";
    } else {
        for (const std::string & d : data) {
            out += "data: " + seal_text(d, secrets) + "\n";
        }
    }
    return out + "\n";
}

std::string seal_events::feed(const char * data, size_t n) {
    pending.append(data, n);
    std::string out;
    for (size_t end; (end = event_end(pending)) != std::string::npos; pending.erase(0, end)) {
        out += sealed_event(pending.substr(0, end), secrets);
    }
    return out;
}

std::string seal_events::end() {
    pending.clear();
    return "";
}
