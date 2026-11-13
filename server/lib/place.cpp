// place.cpp: the place the tools on files act on: the machine of the server
// through the file system, or the machine a shell reaches through scripts of
// a POSIX shell, run as the tools of the shells run theirs; a script answers
// between a line KISS-BEGIN and a line KISS-END, so what a wrapper prints
// around it, a banner or a warning on the merged output, stays out

#include "lib/place.h"

#include "engine/text.h"
#include "lib/base64.h"
#include "lib/file.h"
#include "lib/job.h"
#include "lib/utf8.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

// the bytes of a base64 line, as base64 writes them, and the bytes a script
// writes at most besides its data
static const size_t BASE64_LINE = 76;
static const size_t SLACK       = 4096;

static const char * const BEGIN = "KISS-BEGIN\n";
static const char * const END   = "\nKISS-END";

// the shell function a script fails with, why framed as its answer
static const char * const FAIL = "fail() { printf 'KISS-BEGIN\\n%s\\nKISS-END\\n' \"$1\"; exit 1; }\n";

place place_of(const engine_reader & config) {
    place p;
    p.cwd            = config.get("server working-directory").value_or("");
    const auto shell = config.get("files shell");
    if (!shell) {
        return p;
    }
    const std::vector<std::string> names = config.names("shells");
    p.shell                              = config.get("shells command", *shell).value_or("");
    p.timeout                            = std::stoi(*config.get("shells timeout", *shell));
    if (std::find(names.begin(), names.end(), *shell) == names.end() || p.shell.empty()) {
        p.broken = "files shell " + *shell + " names no shell";
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

// a script run by the shell of the place, its output within budget, and the
// answer it frames; the message of what kept it from ending well, empty when
// it exits 0 with its answer framed
static std::string run(const place &                 p,
                       const std::string &           script,
                       size_t                        budget,
                       std::string &                 answer,
                       const std::function<bool()> & tick) {
    const job_result r = job_run(p.shell, p.cwd, script, p.timeout, budget, tick);
    if (!r.started) {
        return "Cannot start " + p.shell + ": " + r.error;
    }
    if (r.cancelled) {
        return CANCELLED;
    }
    if (r.timed_out) {
        return "Killed after " + std::to_string(p.timeout) + " s";
    }
    if (r.truncated) {
        return "Output larger than " + std::to_string(budget) + " bytes";
    }
    // the first line KISS-BEGIN, as a wrapper prints before the script, and
    // the last line KISS-END, as a name in a listing may hold one
    const std::string & out  = r.output;
    size_t              from = std::string::npos;
    if (out.compare(0, strlen(BEGIN), BEGIN) == 0) {
        from = strlen(BEGIN);
    } else if (const size_t line = out.find(std::string("\n") + BEGIN); line != std::string::npos) {
        from = line + 1 + strlen(BEGIN);
    }
    const size_t to     = out.rfind(END);
    const bool   framed = from != std::string::npos && to != std::string::npos && to + 1 >= from;
    answer              = framed && to >= from ? out.substr(from, to - from) : "";
    // a script that fails frames why, a shell that fails tells it its own way
    if (r.exit_code != 0) {
        const std::string why = trim(framed ? answer : out);
        return why.empty() ? "Exit code " + std::to_string(r.exit_code) : why;
    }
    return framed ? "" : "The shell answered nothing the server reads: " + trim(out);
}

// the bytes base64 writes for size bytes, its line breaks counted
static size_t encoded(size_t size) {
    const size_t text = (size + 2) / 3 * 4;
    return text + text / BASE64_LINE + SLACK;
}

// a count as a script prints it, none for another text
static std::optional<uintmax_t> count(const std::string & s) {
    const std::string n = trim(s);
    if (n.empty() || n.size() > 19 || n.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    return std::stoull(n);
}

place_kind place_stat(const place &                 p,
                      const std::string &           given,
                      uintmax_t &                   size,
                      std::string &                 why,
                      const std::function<bool()> & tick) {
    size = 0;
    why  = p.broken;
    if (!why.empty()) {
        return PLACE_NONE;
    }
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
                               "echo KISS-BEGIN\n"
                               "if [ -d \"$p\" ]; then echo d 0\n"
                               "elif [ -f \"$p\" ]; then echo f $(wc -c < \"$p\")\n"
                               "elif [ -e \"$p\" ]; then echo o 0\n"
                               "else echo n 0; fi\n"
                               "echo KISS-END\n";
    std::string answer;
    why = run(p, script, SLACK, answer, tick);
    if (!why.empty()) {
        return PLACE_NONE;
    }
    const std::string              kind = answer.substr(0, 2);
    const std::optional<uintmax_t> n    = answer.size() > 2 ? count(answer.substr(2)) : std::nullopt;
    if (!n || (kind != "d " && kind != "f " && kind != "o " && kind != "n ")) {
        why = "The shell answered no kind of file: " + trim(answer);
        return PLACE_NONE;
    }
    size = *n;
    return kind == "f " ? PLACE_FILE : kind == "d " ? PLACE_DIRECTORY : kind == "o " ? PLACE_OTHER : PLACE_NONE;
}

std::string place_read(const place &                 p,
                       const std::string &           given,
                       size_t                        limit,
                       std::string &                 bytes,
                       const std::function<bool()> & tick) {
    bytes.clear();
    if (!p.broken.empty()) {
        return p.broken;
    }
    if (p.shell.empty()) {
        return file_bytes(file_path(p.cwd, given), limit, bytes);
    }
    const std::string n      = std::to_string(limit);
    const std::string script = "p=" + quoted(given) + "\n" + FAIL +
                               "[ -e \"$p\" ] || fail \"No such file or directory: $p\"\n"
                               "[ -f \"$p\" ] || fail \"Not a regular file: $p\"\n"
                               "n=$(wc -c < \"$p\") || exit 1\n"
                               "[ $n -le " +
                               n + " ] || fail \"File larger than " + n +
                               " bytes\"\n"
                               "echo KISS-BEGIN\n"
                               "echo $n\n"
                               "base64 < \"$p\" || exit 1\n"
                               "echo KISS-END\n";
    std::string       answer;
    const std::string why = run(p, script, encoded(limit), answer, tick);
    if (!why.empty()) {
        return why;
    }
    const size_t                   nl   = answer.find('\n');
    const std::optional<uintmax_t> size = count(answer.substr(0, nl));
    const std::string              text = nl == std::string::npos ? "" : answer.substr(nl + 1);
    if (!size || !base64_decode(text, bytes) || bytes.size() != *size) {
        bytes.clear();
        return "Cannot read the file: the shell answered other bytes than the file holds";
    }
    return "";
}

std::string place_write(const place &                 p,
                        const std::string &           given,
                        const std::string &           bytes,
                        bool &                        existed,
                        const std::function<bool()> & tick) {
    existed = false;
    if (!p.broken.empty()) {
        return p.broken;
    }
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
                               "if [ -e \"$p\" ]; then e=existed; else e=created; fi\n"
                               "d=$(dirname -- \"$p\") && mkdir -p -- \"$d\" && base64 -d > \"$p\" <<'KISS_BYTES' || "
                               "exit 1\n" +
                               lines +
                               "KISS_BYTES\n"
                               "echo KISS-BEGIN\n"
                               "echo $e $(wc -c < \"$p\")\n"
                               "echo KISS-END\n";
    std::string       answer;
    const std::string why = run(p, script, SLACK, answer, tick);
    if (!why.empty()) {
        return "Cannot write the file: " + why;
    }
    const size_t                   sp   = answer.find(' ');
    const std::string              told = answer.substr(0, sp);
    const std::optional<uintmax_t> size = sp == std::string::npos ? std::nullopt : count(answer.substr(sp + 1));
    if ((told != "existed" && told != "created") || !size || *size != bytes.size()) {
        return "Cannot write the file: the shell wrote other bytes than were sent";
    }
    existed = told == "existed";
    return "";
}

// every entry below the directory the shell starts in, one record each:
// type, size and path apart by spaces, the records apart by NUL
static const char * const WALK = R"sh(
command -v wc > /dev/null 2>&1 || fail 'wc is found nowhere on the far side'
find . -mindepth 1 -prune > /dev/null 2>&1 || fail 'find lists nothing here, missing or without -mindepth'
nl='
'

# what is no regular file, its type told by test alone
kinds='for f do
    if [ -L "$f" ]; then t=l; elif [ -d "$f" ]; then t=d; else t=o; fi
    printf "%s 0 %s\0" $t "$f"
done'

# files counted one by one, the count through arithmetic, as a wc that reads
# stdin pads it with blanks
alone='for f do
    s=$(wc -c < "$f") || s=0
    printf "f %s %s\0" $((s)) "$f"
done'
export alone

# a batch counted by one wc, which tells a file it could not read by a line
# less, the batch then counted one by one
batch='out=$(wc -c -- "$@")
if [ "$(printf "%s\n" "$out" | wc -l)" -eq $(($# > 1 ? $# + 1 : $#)) ]; then
    printf "%s\n" "$out" | { for f do read -r s rest; printf "f %s %s\0" "$s" "$f"; done; }
else
    eval "$alone"
fi'

echo KISS-BEGIN
find . -mindepth 1 ! -type f -exec sh -c "$kinds" sh {} + 2> /dev/null
# a name with a line break is counted alone, its line in wc being two
find . -mindepth 1 -type f -name "*$nl*" -exec sh -c "$alone" sh {} + 2> /dev/null
find . -mindepth 1 -type f ! -name "*$nl*" -exec sh -c "$batch" sh {} + 2> /dev/null
echo
echo KISS-END
)sh";

std::string place_walk(const place &                                    p,
                       const std::string &                              given,
                       size_t                                           limit,
                       const std::function<bool(const place_entry &)> & each,
                       const std::function<bool()> &                    tick) {
    if (!p.broken.empty()) {
        return p.broken;
    }
    if (p.shell.empty()) {
        const fs::path                   root = file_path(p.cwd, given);
        std::error_code                  ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            return "Cannot list the directory: " + ec.message();
        }
        for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
            if (!tick()) {
                return CANCELLED;
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
                return CANCELLED;
            }
        }
        return "";
    }
    std::string       answer;
    const std::string why = run(p, FAIL + ("cd " + quoted(given) + " || exit 1\n") + WALK, limit, answer, tick);
    if (!why.empty()) {
        return why;
    }
    for (size_t from = 0; from < answer.size();) {
        const size_t      nul    = answer.find('\0', from);
        const std::string record = answer.substr(from, nul == std::string::npos ? std::string::npos : nul - from);
        from                     = nul == std::string::npos ? answer.size() : nul + 1;
        if (nul == std::string::npos && trim(record).empty()) {
            break;
        }
        // t size ./path
        const size_t sp = record.find(' ', 2);
        if (record.size() < 2 || std::string("fdlo").find(record[0]) == std::string::npos || record[1] != ' ' ||
            sp == std::string::npos || record.compare(sp + 1, 2, "./") != 0 || !count(record.substr(2, sp - 2))) {
            return "The shell answered an entry the server reads not: " + trim(record);
        }
        place_entry e;
        e.rel     = record.substr(sp + 3);
        e.depth   = (int) std::count(e.rel.begin(), e.rel.end(), '/');
        e.regular = record[0] == 'f';
        e.size    = e.regular ? *count(record.substr(2, sp - 2)) : 0;
        if (!each(e)) {
            return CANCELLED;
        }
    }
    return "";
}
