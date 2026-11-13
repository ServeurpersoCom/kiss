// view.cpp: the view tool: a directory two levels deep with the size of each
// entry, an image as an image block, a text file with numbered lines

#include "lib/base64.h"
#include "lib/file.h"
#include "lib/image.h"
#include "lib/job.h"
#include "lib/json.h"
#include "lib/output.h"
#include "lib/place.h"
#include "lib/prompts.h"
#include "lib/tools.h"
#include "lib/utf8.h"
#include "yyjson.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

// the milliseconds between two ticks
static const int TICK_MS = 1000;

// the pixels of a megapixel
static const size_t MEGAPIXEL = 1000000;

// the bytes a size counts by: 1024 make a K, 1024 K an M
static const uintmax_t KILO = 1024;

// the directory of packages a listing never opens
static const char * const PACKAGES = PACKAGES;

static const char * const NAME = "view";

struct viewer {
    place        at;
    size_t       output;
    // the bytes of a file view loads whole, an image
    size_t       load;
    image_limits limits;
};

// calls tick once a second at most; false once the client is gone
struct ticker {
    const tool_tick &                     tick;
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();

    bool alive() {
        const auto now = std::chrono::steady_clock::now();
        if (now < next) {
            return true;
        }
        next = now + std::chrono::milliseconds(TICK_MS);
        return tick();
    }
};

// a size as du -h writes it: 512, 4.0K, 12K, 1.5M
static std::string human(uintmax_t n) {
    if (n < KILO) {
        return std::to_string(n);
    }
    const char * units = "KMGTPE";
    double       v     = (double) n / KILO;
    int          u     = 0;
    while (v >= KILO && u < 5) {
        v /= KILO;
        u++;
    }
    char buf[32];
    if (std::ceil(v * 10) / 10 < 10) {
        snprintf(buf, sizeof(buf), "%.1f%c", std::ceil(v * 10) / 10, units[u]);
    } else {
        snprintf(buf, sizeof(buf), "%.0f%c", std::ceil(v), units[u]);
    }
    return buf;
}

// an entry the listing shows: no hidden part, nothing below node_modules
static bool shown(const fs::path & rel) {
    const std::string last = rel.filename().u8string();
    for (const fs::path & part : rel) {
        const std::string s = part.u8string();
        if (!s.empty() && s[0] == '.') {
            return false;
        }
        if (s == PACKAGES && s != last) {
            return false;
        }
    }
    return true;
}

// every entry two levels deep, sorted by path, each with the size of all it
// holds, as find and du -sh list them, then the count of the hidden ones
static tool_outcome listing(const viewer & v, const std::string & given, ticker & t) {
    std::map<std::string, uintmax_t> sizes = {
        { "", 0 }
    };
    long long         hidden = 0;
    const std::string why    = place_walk(
        v.at, given, v.load,
        [&](const place_entry & e) {
            const fs::path rel = fs::u8path(e.rel);
            if (e.depth <= 1 && shown(rel)) {
                sizes.emplace(rel.generic_u8string(), 0);
            } else if (e.depth <= 1 && rel.filename().u8string()[0] == '.' && shown(rel.parent_path()) &&
                       rel.parent_path().filename() != PACKAGES) {
                hidden++;
            }
            if (!e.regular) {
                return true;
            }
            // the file counts in the root and in its two first ancestors
            fs::path prefix;
            sizes[""] += e.size;
            int depth = 0;
            for (const fs::path & part : rel) {
                if (++depth > 2) {
                    break;
                }
                prefix /= part;
                auto at = sizes.find(prefix.generic_u8string());
                if (at != sizes.end()) {
                    at->second += e.size;
                }
            }
            return true;
        },
        [&] { return t.alive(); });
    if (!why.empty()) {
        return { false, why };
    }
    output_buffer kept(v.output);
    for (const auto & [rel, size] : sizes) {
        const std::string path = rel.empty() ? given : (fs::u8path(given) / fs::u8path(rel)).u8string();
        const std::string line = human(size) + " " + path + "\n";
        kept.add(line.data(), line.size());
    }
    const std::string tail = hidden ? ", " + std::to_string(hidden) + " hidden" : "";
    return { true, kept.text() + output_counted((long long) sizes.size() - 1, "entry", "entries") + tail };
}

// an image as the result tells it: its type, its size and its bytes
static std::string described(const std::string & mime, int w, int h, size_t bytes) {
    return mime + " " + std::to_string(w) + "x" + std::to_string(h) + ", " + std::to_string(bytes) + " bytes";
}

// the bytes of a file read into a buffer, as many as it holds, 0 at its end
using reader = std::function<size_t(char *, size_t)>;

// the lines first to last of a text file, numbered, last -1 for the end of
// the file; the file streams through, so its size bounds nothing but time
static tool_outcome
file(const reader & read, uintmax_t size, const viewer & v, long long first, long long last, bool ranged, ticker & t) {
    std::string head(FILE_CHUNK, '\0');
    head.resize(read(&head[0], head.size()));
    if (!image_type(head).empty()) {
        if (size > v.load) {
            return { false, "Image larger than " + std::to_string(v.load) + " bytes" };
        }
        std::string data = head;
        char        buf[FILE_CHUNK];
        for (size_t got; (got = read(buf, sizeof(buf))) > 0;) {
            if (!t.alive()) {
                return { false, CANCELLED };
            }
            data.append(buf, got);
        }
        const image_sent sent = image_fit(data, v.limits);
        if (!sent.error.empty()) {
            return { false, sent.error };
        }
        tool_outcome r(base64_encode(sent.data), sent.mime);
        r.text = "Image " + described(sent.source_mime, sent.source_width, sent.source_height, data.size());
        if (sent.made) {
            r.text += ", sent as " + described(sent.mime, sent.width, sent.height, sent.data.size());
            r.text += sent.transparent ? ", its transparency laid on white" : "";
        }
        return r;
    }
    if (file_binary(head)) {
        return { false, FILE_BINARY };
    }

    output_buffer kept(v.output);
    long long     number = 1;
    std::string   line;
    std::string   chunk = head;
    const auto    emit  = [&]() {
        if (number >= first && (last < 0 || number <= last)) {
            const std::string numbered = std::to_string(number) + "\t" + line + "\n";
            kept.add(numbered.data(), numbered.size());
        }
        line.clear();
        number++;
    };
    while (!chunk.empty()) {
        if (!t.alive()) {
            return { false, CANCELLED };
        }
        for (size_t at = 0;;) {
            const size_t nl = chunk.find('\n', at);
            if (nl == std::string::npos) {
                line.append(chunk, at, std::string::npos);
                break;
            }
            line.append(chunk, at, nl - at);
            emit();
            at = nl + 1;
        }
        chunk.resize(FILE_CHUNK);
        chunk.resize(read(&chunk[0], FILE_CHUNK));
    }
    if (!line.empty()) {
        emit();
    }
    const long long total = number - 1;
    if (ranged && first > total) {
        return { false, "Line " + std::to_string(first) + " is past the end, the file has " +
                            output_counted(total, "line", "lines") };
    }
    if (!ranged) {
        return { true, kept.text() + output_counted(total, "line", "lines") };
    }
    const long long end = last < 0 || last > total ? total : last;
    return { true, kept.text() + "Lines " + std::to_string(first) + "-" + std::to_string(end) + " of " +
                       std::to_string(total) };
}

static tool_outcome view(const viewer & v, yyjson_val * args, const tool_tick & tick) {
    yyjson_val *      named = yyjson_obj_get(args, "path");
    const std::string given = json_text(named);
    if (!file_refused(given).empty()) {
        return { false, file_refused(given) };
    }
    yyjson_val * range = yyjson_obj_get(args, "view_range");
    long long    first = 1, last = -1;
    const bool   ranged = range && !yyjson_is_null(range);
    if (ranged) {
        yyjson_val * a = yyjson_arr_get(range, 0);
        yyjson_val * b = yyjson_arr_get(range, 1);
        if (!yyjson_is_arr(range) || yyjson_arr_size(range) != 2 || !yyjson_is_int(a) || !yyjson_is_int(b)) {
            return { false, "The view_range is not two line numbers, [start, end]" };
        }
        first = std::max<long long>(1, yyjson_get_sint(a));
        last  = yyjson_get_sint(b);
        if (last != -1 && last < first) {
            return { false, "The view_range ends before it starts" };
        }
    }

    ticker     t{ tick };
    const auto alive = [&] {
        return t.alive();
    };
    uintmax_t         size = 0;
    std::string       why;
    const place_kind  kind  = place_stat(v.at, given, size, why, alive);
    const std::string shown = v.at.shell.empty() ? file_path(v.at.cwd, given).u8string() : given;
    if (!why.empty()) {
        return { false, why };
    }
    if (kind == PLACE_NONE) {
        return { false, "No such file or directory: " + shown };
    }
    if (kind == PLACE_DIRECTORY) {
        return listing(v, given, t);
    }
    if (kind != PLACE_FILE) {
        return { false, "Neither a regular file nor a directory: " + shown };
    }
    if (!v.at.shell.empty()) {
        std::string       bytes;
        const std::string failed = place_read(v.at, given, v.load, bytes, alive);
        if (!failed.empty()) {
            return { false, failed };
        }
        size_t at = 0;
        return file(
            [&](char * buf, size_t n) {
                const size_t got = std::min(n, bytes.size() - at);
                memcpy(buf, bytes.data() + at, got);
                at += got;
                return got;
            },
            size, v, first, last, ranged, t);
    }
    FILE * f = utf8_fopen(shown.c_str(), "rb");
    if (!f) {
        return { false, std::string("Cannot read the file: ") + strerror(errno) };
    }
    const tool_outcome r =
        file([f](char * buf, size_t n) { return fread(buf, 1, n, f); }, size, v, first, last, ranged, t);
    fclose(f);
    return r;
}

static std::vector<tool> view_tool(const engine_reader & config) {
    viewer v;
    v.at             = place_of(config);
    v.output         = (size_t) std::stoull(*config.get("files max-output"));
    v.load           = (size_t) std::stoull(*config.get("files max-load"));
    v.limits.pixels  = (size_t) std::stoull(*config.get("files max-megapixels")) * MEGAPIXEL;
    v.limits.side    = std::stoi(*config.get("files max-dimension"));
    v.limits.quality = std::stoi(*config.get("files jpeg-quality"));

    tool t;
    t.suffix      = NAME;
    t.description = prompts_description(NAME, {
                                                  { "hostname", config.get("server hostname").value_or("") }
    });
    t.input       = prompts_input(NAME);
    t.run         = [v](yyjson_val * args, const engine_typed &, const tool_tick & tick) {
        return view(v, args, tick);
    };
    return { t };
}

SERVER_TOOLS(view_tool);
