// place.cpp: the place the tools on files act on: the machine of the server
// through the file system, or the machine a shell reaches through scripts of
// a POSIX shell, run as the tools of the shells run theirs

#include "lib/place.h"

#include "lib/base64.h"
#include "lib/file.h"
#include "lib/job.h"
#include "lib/utf8.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// the bytes of a base64 line, as base64 writes them, and the bytes of a line
// a script writes at most besides its data
static const size_t BASE64_LINE = 76;
static const size_t SLACK       = 4096;

place place_of(const engine_reader & config) {
    place p;
    p.cwd            = config.get("server working-directory").value_or("");
    const auto shell = config.get("files shell");
    if (shell && !shell->empty()) {
        p.shell   = config.get("shells command", *shell).value_or("");
        p.timeout = std::stoi(config.get("shells timeout", *shell).value_or("0"));
    }
    return p;
}

// a word a POSIX shell reads back as it is
static std::string quoted(const std::string & s) {
    std::string out = "'";
    for (char c : s) {
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }
    return out + "'";
}

static std::string trimmed(const std::string & s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    const size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// a script run by the shell of the place, its output within budget; the
// message of what kept it from ending well, empty when it exits 0
static std::string run(const place &                 p,
                       const std::string &           script,
                       size_t                        budget,
                       std::string &                 output,
                       const std::function<bool()> & tick) {
    const job_result r = job_run(p.shell, p.cwd, script, p.timeout, budget, tick);
    output             = r.output;
    if (!r.started) {
        return "Cannot start " + p.shell + ": " + r.error;
    }
    if (r.cancelled) {
        return "Cancelled";
    }
    if (r.timed_out) {
        return "Killed after " + std::to_string(p.timeout) + " s";
    }
    if (r.truncated) {
        return "Output larger than " + std::to_string(budget) + " bytes";
    }
    if (r.exit_code != 0) {
        const std::string why = trimmed(output);
        return why.empty() ? "Exit code " + std::to_string(r.exit_code) : why;
    }
    return "";
}

// the bytes base64 writes for size bytes, its line breaks counted
static size_t encoded(size_t size) {
    const size_t text = (size + 2) / 3 * 4;
    return text + text / BASE64_LINE + SLACK;
}

place_kind place_stat(const place &                 p,
                      const std::string &           given,
                      uintmax_t &                   size,
                      std::string &                 why,
                      const std::function<bool()> & tick) {
    size = 0;
    if (p.shell.empty()) {
        std::error_code       ec;
        const fs::path        path = file_path(p.cwd, given);
        const fs::file_status st   = fs::status(path, ec);
        if (ec || !fs::exists(st)) {
            return PLACE_NONE;
        }
        if (fs::is_directory(st)) {
            return PLACE_DIRECTORY;
        }
        if (!fs::is_regular_file(st)) {
            return PLACE_OTHER;
        }
        size = fs::file_size(path, ec);
        return PLACE_FILE;
    }
    const std::string script = "p=" + quoted(given) +
                               "\n"
                               "if [ -d \"$p\" ]; then echo d\n"
                               "elif [ -f \"$p\" ]; then echo f; wc -c < \"$p\"\n"
                               "elif [ -e \"$p\" ]; then echo o\n"
                               "else echo n; fi\n";
    std::string output;
    why = run(p, script, SLACK, output, tick);
    if (!why.empty()) {
        return PLACE_NONE;
    }
    const std::string kind = trimmed(output.substr(0, output.find('\n')));
    if (kind == "f") {
        const std::string n = trimmed(output.substr(output.find('\n') + 1));
        if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos) {
            why = trimmed(output);
            return PLACE_NONE;
        }
        size = std::stoull(n);
        return PLACE_FILE;
    }
    return kind == "d" ? PLACE_DIRECTORY : kind == "o" ? PLACE_OTHER : PLACE_NONE;
}

std::string place_read(const place &                 p,
                       const std::string &           given,
                       size_t                        limit,
                       std::string &                 bytes,
                       const std::function<bool()> & tick) {
    bytes.clear();
    if (p.shell.empty()) {
        const fs::path        path = file_path(p.cwd, given);
        std::error_code       ec;
        const fs::file_status st = fs::status(path, ec);
        if (ec || !fs::exists(st)) {
            return "No such file or directory: " + path.u8string();
        }
        if (!fs::is_regular_file(st)) {
            return "Not a regular file: " + path.u8string();
        }
        FILE * f = utf8_fopen(path.u8string().c_str(), "rb");
        if (!f) {
            return std::string("Cannot read the file: ") + strerror(errno);
        }
        char buf[FILE_CHUNK];
        for (size_t got; (got = fread(buf, 1, sizeof(buf), f)) > 0;) {
            bytes.append(buf, got);
            if (bytes.size() > limit) {
                fclose(f);
                return "File larger than " + std::to_string(limit) + " bytes";
            }
        }
        const bool failed = ferror(f) != 0;
        fclose(f);
        return failed ? "Cannot read the file" : "";
    }
    const std::string n      = std::to_string(limit);
    const std::string script = "p=" + quoted(given) +
                               "\n"
                               "if [ ! -e \"$p\" ]; then echo \"No such file or directory: $p\"; exit 1; fi\n"
                               "if [ ! -f \"$p\" ]; then echo \"Not a regular file: $p\"; exit 1; fi\n"
                               "n=$(wc -c < \"$p\") || exit 1\n"
                               "if [ \"$n\" -gt " +
                               n + " ]; then echo \"File larger than " + n +
                               " bytes\"; exit 1; fi\n"
                               "base64 < \"$p\"\n";
    std::string       output;
    const std::string why = run(p, script, encoded(limit), output, tick);
    if (!why.empty()) {
        return why;
    }
    return base64_decode(output, bytes) ? "" : "Cannot read the file: " + trimmed(output);
}

std::string place_write(const place &                 p,
                        const std::string &           given,
                        const std::string &           bytes,
                        bool &                        existed,
                        const std::function<bool()> & tick) {
    if (p.shell.empty()) {
        const fs::path  path = file_path(p.cwd, given);
        std::error_code ec;
        existed = fs::exists(path, ec);
        return file_write(path, bytes);
    }
    std::string       lines;
    const std::string text = base64_encode(bytes);
    for (size_t at = 0; at < text.size(); at += BASE64_LINE) {
        lines += text.substr(at, BASE64_LINE) + "\n";
    }
    const std::string script = "p=" + quoted(given) +
                               "\n"
                               "if [ -e \"$p\" ]; then echo existed; else echo created; fi\n"
                               "d=$(dirname -- \"$p\") && mkdir -p -- \"$d\" && base64 -d > \"$p\" <<'KISS_BYTES'\n" +
                               lines + "KISS_BYTES\n";
    std::string       output;
    const std::string why   = run(p, script, SLACK, output, tick);
    const std::string first = output.substr(0, output.find('\n'));
    existed                 = trimmed(first) == "existed";
    if (!why.empty()) {
        const std::string rest = trimmed(output.substr(first.size()));
        return "Cannot write the file: " + (rest.empty() ? why : rest);
    }
    return "";
}

// an entry line of ls -ln, its fields then its name: the type, the size, the
// name, a link without what it points to; false for a line that is none
static bool entry_of(const std::string & line, char & type, uintmax_t & size, std::string & name) {
    std::vector<std::pair<size_t, size_t>> fields;
    size_t                                 at   = 0;
    // mode, links, owner, group, size, month, day, time; a device takes its
    // major and minor numbers where a file takes its size
    const size_t                           want = line.empty() || (line[0] != 'c' && line[0] != 'b') ? 8 : 9;
    while (fields.size() < want) {
        const size_t a = line.find_first_not_of(' ', at);
        if (a == std::string::npos) {
            return false;
        }
        const size_t b = line.find(' ', a);
        if (b == std::string::npos) {
            return false;
        }
        fields.push_back({ a, b - a });
        at = b;
    }
    type                    = line[0];
    const std::string bytes = line.substr(fields[4].first, fields[4].second);
    size = type == '-' && bytes.find_first_not_of("0123456789") == std::string::npos ? std::stoull(bytes) : 0;
    name = line.substr(at + 1);
    if (type == 'l') {
        name = name.substr(0, name.find(" -> "));
    }
    return !name.empty();
}

std::string place_walk(const place &                                    p,
                       const std::string &                              given,
                       size_t                                           limit,
                       const std::function<bool(const place_entry &)> & each,
                       const std::function<bool()> &                    tick) {
    if (p.shell.empty()) {
        const fs::path  root = file_path(p.cwd, given);
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!tick()) {
                return "Cancelled";
            }
            place_entry     e;
            std::error_code fe;
            e.rel     = it->path().lexically_relative(root).generic_u8string();
            e.depth   = it.depth();
            e.regular = it->is_regular_file(fe) && !it->is_symlink(fe);
            e.size    = e.regular ? it->file_size(fe) : 0;
            if (fe) {
                e.regular = false;
                e.size    = 0;
            }
            if (!each(e)) {
                return "Cancelled";
            }
        }
        return "";
    }
    const std::string script = "cd " + quoted(given) + " || exit 1\nLC_ALL=C.UTF-8 ls -lnRA . 2>/dev/null\nexit 0\n";
    std::string       output;
    const std::string why = run(p, script, limit, output, tick);
    if (!why.empty()) {
        return why;
    }
    std::string dir;
    bool        header = true;
    size_t      from   = 0;
    while (from < output.size()) {
        const size_t      nl   = output.find('\n', from);
        const std::string line = output.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        from                   = nl == std::string::npos ? output.size() : nl + 1;
        if (line.empty()) {
            header = true;
            continue;
        }
        if (header && line.back() == ':') {
            const std::string d = line.substr(0, line.size() - 1);
            dir                 = d == "." ? "" : d.substr(0, 2) == "./" ? d.substr(2) : d;
            header              = false;
            continue;
        }
        header = false;
        char        type;
        place_entry e;
        std::string name;
        if (line.rfind("total ", 0) == 0 || !entry_of(line, type, e.size, name)) {
            continue;
        }
        e.rel     = dir.empty() ? name : dir + "/" + name;
        e.depth   = (int) std::count(e.rel.begin(), e.rel.end(), '/');
        e.regular = type == '-';
        if (!each(e)) {
            return "Cancelled";
        }
    }
    return "";
}
