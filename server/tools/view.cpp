// view.cpp: the view tool: a directory two levels deep with the size of each
// entry, an image as an image block, a text file with numbered lines

#include "output.h"
#include "tools.h"
#include "utf8.h"
#include "yyjson.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

// the bytes read at a time, and the first ones that tell an image or a binary
static const size_t CHUNK = 65536;

// the milliseconds between two ticks
static const int TICK_MS = 1000;

struct viewer {
    std::string cwd;
    size_t      output;
    size_t      image;
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
    if (n < 1024) {
        return std::to_string(n);
    }
    const char * units = "KMGTPE";
    double       v     = (double) n / 1024;
    int          u     = 0;
    while (v >= 1024 && u < 5) {
        v /= 1024;
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

// a count with its noun: 1 line, 3 lines
static std::string counted(long long n, const char * one, const char * many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// an entry the listing shows: no hidden part, nothing below node_modules
static bool shown(const fs::path & rel) {
    const std::string last = rel.filename().u8string();
    for (const fs::path & part : rel) {
        const std::string s = part.u8string();
        if (!s.empty() && s[0] == '.') {
            return false;
        }
        if (s == "node_modules" && s != last) {
            return false;
        }
    }
    return true;
}

// every entry two levels deep, sorted by path, each with the size of all it
// holds, as find and du -sh list them
static tool_result listing(const fs::path & root, const std::string & given, size_t budget, ticker & t) {
    std::map<std::string, uintmax_t> sizes = {
        { "", 0 }
    };
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!t.alive()) {
            return { "Cancelled", true };
        }
        const fs::path rel = it->path().lexically_relative(root);
        if (it.depth() <= 1 && shown(rel)) {
            sizes.emplace(rel.generic_u8string(), 0);
        }
        std::error_code fe;
        if (!it->is_regular_file(fe) || it->is_symlink(fe)) {
            continue;
        }
        const uintmax_t size = it->file_size(fe);
        if (fe) {
            continue;
        }
        // the file counts in the root and in its two first ancestors
        fs::path prefix;
        sizes[""] += size;
        int depth = 0;
        for (const fs::path & part : rel) {
            if (++depth > 2) {
                break;
            }
            prefix /= part;
            auto at = sizes.find(prefix.generic_u8string());
            if (at != sizes.end()) {
                at->second += size;
            }
        }
    }
    output_buffer kept(budget);
    for (const auto & [rel, size] : sizes) {
        const std::string path = rel.empty() ? given : (fs::u8path(given) / fs::u8path(rel)).u8string();
        const std::string line = human(size) + "\t" + path + "\n";
        kept.add(line.data(), line.size());
    }
    return { kept.text() + counted((long long) sizes.size() - 1, "entry", "entries"), false };
}

static std::string base64(const std::string & in) {
    static const char * table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string         out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const size_t   n = std::min<size_t>(3, in.size() - i);
        const uint32_t v = ((uint32_t) (unsigned char) in[i] << 16) |
                           (n > 1 ? (uint32_t) (unsigned char) in[i + 1] << 8 : 0) |
                           (n > 2 ? (uint32_t) (unsigned char) in[i + 2] : 0);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += n > 1 ? table[(v >> 6) & 63] : '=';
        out += n > 2 ? table[v & 63] : '=';
    }
    return out;
}

// the MIME type of an image by its first bytes, empty for anything else
static const char * image_type(const std::string & head) {
    if (head.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) {
        return "image/png";
    }
    if (head.compare(0, 3, "\xff\xd8\xff") == 0) {
        return "image/jpeg";
    }
    if (head.compare(0, 6, "GIF87a") == 0 || head.compare(0, 6, "GIF89a") == 0) {
        return "image/gif";
    }
    if (head.size() >= 12 && head.compare(0, 4, "RIFF") == 0 && head.compare(8, 4, "WEBP") == 0) {
        return "image/webp";
    }
    return "";
}

// the lines first to last of a text file, numbered, last -1 for the end of
// the file; the file streams through, so its size bounds nothing but time
static tool_result
file(FILE * f, uintmax_t size, const viewer & v, long long first, long long last, bool ranged, ticker & t) {
    std::string head(std::min<uintmax_t>(size, CHUNK), '\0');
    head.resize(fread(&head[0], 1, head.size(), f));
    const std::string mime = image_type(head);
    if (!mime.empty()) {
        if (size > v.image) {
            return { "Image larger than " + std::to_string(v.image) + " bytes", true };
        }
        std::string data = head;
        char        buf[CHUNK];
        for (size_t got; (got = fread(buf, 1, sizeof(buf), f)) > 0;) {
            data.append(buf, got);
        }
        return { base64(data), std::string(mime) };
    }
    if (memchr(head.data(), '\0', head.size())) {
        return { "Binary file", true };
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
            return { "Cancelled", true };
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
        chunk.resize(CHUNK);
        chunk.resize(fread(&chunk[0], 1, CHUNK, f));
    }
    if (!line.empty()) {
        emit();
    }
    const long long total = number - 1;
    if (ranged && first > total) {
        return { "Line " + std::to_string(first) + " is past the end, the file has " + counted(total, "line", "lines"),
                 true };
    }
    if (!ranged) {
        return { kept.text() + counted(total, "line", "lines"), false };
    }
    const long long end = last < 0 || last > total ? total : last;
    return { kept.text() + "Lines " + std::to_string(first) + "-" + std::to_string(end) + " of " +
                 std::to_string(total),
             false };
}

static tool_result view(const viewer & v, yyjson_val * args, const tool_tick & tick) {
    const std::string given = yyjson_get_str(yyjson_obj_get(args, "path"));
    yyjson_val *      range = yyjson_obj_get(args, "view_range");
    long long         first = 1, last = -1;
    const bool        ranged = range && !yyjson_is_null(range);
    if (ranged) {
        yyjson_val * a = yyjson_arr_get(range, 0);
        yyjson_val * b = yyjson_arr_get(range, 1);
        if (!yyjson_is_arr(range) || yyjson_arr_size(range) != 2 || !yyjson_is_int(a) || !yyjson_is_int(b)) {
            return { "View_range is two line numbers, [start, end]", true };
        }
        first = std::max<long long>(1, yyjson_get_sint(a));
        last  = yyjson_get_sint(b);
        if (last != -1 && last < first) {
            return { "View_range ends before it starts", true };
        }
    }

    fs::path path = fs::u8path(given);
    if (path.is_relative()) {
        path = fs::u8path(v.cwd) / path;
    }
    std::error_code       ec;
    const fs::file_status st = fs::status(path, ec);
    if (ec || !fs::exists(st)) {
        return { "No such file or directory", true };
    }
    ticker t{ tick };
    if (fs::is_directory(st)) {
        return listing(path, given, v.output, t);
    }
    if (!fs::is_regular_file(st)) {
        return { "Not a regular file or a directory", true };
    }
    const uintmax_t size = fs::file_size(path, ec);
    FILE *          f    = utf8_fopen(path.u8string().c_str(), "rb");
    if (ec || !f) {
        return { std::string("Cannot read the file: ") + strerror(errno), true };
    }
    const tool_result r = file(f, size, v, first, last, ranged, t);
    fclose(f);
    return r;
}

static std::vector<tool> view_tool(const engine_reader & config) {
    viewer v;
    v.cwd    = config.get("server cwd").value_or("");
    v.output = (size_t) std::stoull(config.get("view output").value_or("0"));
    v.image  = (size_t) std::stoull(config.get("view image").value_or("0"));

    tool t;
    t.suffix      = "view";
    t.description = "Views a file or a directory on " + std::string(engine_platform()) + ", the path relative to " +
                    v.cwd +
                    " or absolute. A directory lists every entry two levels deep with the size of all it holds, "
                    "hidden entries and the content of node_modules left out. A PNG, JPEG, GIF or WebP image of " +
                    std::to_string(v.image) +
                    " bytes at most comes back as an image. A text file comes back with numbered lines, the head "
                    "and the tail of " +
                    std::to_string(v.output) +
                    " bytes at most, then its line count; view_range picks lines [start, end], end -1 being the "
                    "last. Another binary file is refused.";
    t.input = R"({"type":"object","properties":{)"
              R"("description":{"type":"string","description":"Why I need to view this"},)"
              R"("path":{"type":"string","description":"The file or the directory"},)"
              R"("view_range":{"type":"array","items":{"type":"integer"},"minItems":2,"maxItems":2,)"
              R"("description":"Lines [start, end] of a text file, from 1, end -1 for the last"}},)"
              R"("required":["description","path"],"additionalProperties":false})";
    t.call  = [v](yyjson_val * args, const tool_tick & tick) {
        return view(v, args, tick);
    };
    return { t };
}

SERVER_TOOLS(view_tool);
