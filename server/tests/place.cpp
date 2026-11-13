// place.cpp: the laws of the place the tools on files act on, run as the build
// ends: the machine of the server and a POSIX shell that reaches the same
// directory answer alike, what a path names and its size, the bytes of a file
// whatever they are, a file written in place with its directories made,
// telling whether it was there, and every entry below a directory with its
// depth and the size of a regular file; a file past the limit loads through
// neither; a wrapper that prints around the shell, an empty line first or any
// other, changes nothing, nor does a wc that pads its counts or loses a line
// of a batch, a far side without find or wc lists nothing and says so, a file
// is no directory to walk, a line it slips into the answer fails the read, and
// a shell that frames no answer reads nothing; a machine with no POSIX shell
// keeps the laws of the server alone; the configuration gives the tools the
// shell files shell names, else the machine of the server, and fails every
// call when it names no shell; view lists a directory of the place two levels
// deep, its hidden entries counted, node_modules closed; a command that closes
// its output runs on to its end

#include "lib/place.h"

#include "engine/engine.h"
#include "factory.h"
#include "laws.h"
#include "lib/base64.h"
#include "lib/job.h"
#include "lib/json.h"
#include "lib/quit.h"
#include "lib/tools.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const auto ALIVE = [] {
    return true;
};

static std::string read(const fs::path & p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static void put(const fs::path & p, const std::string & bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
}

// every entry below a directory, sorted, as a line each
static std::vector<std::string> walked(const place & at, const std::string & given) {
    std::vector<std::string> out;
    const std::string        why = place_walk(
        at, given, 1 << 20,
        [&](const place_entry & e) {
            out.push_back(e.rel + " " + std::to_string(e.depth) + (e.regular ? " file " + std::to_string(e.size) : ""));
            return true;
        },
        ALIVE);
    if (!why.empty()) {
        out.push_back("! " + why);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// a word a POSIX shell reads back as it is
static std::string sh_quoted(const std::string & s) {
    std::string out = "'";
    for (char c : s) {
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }
    return out + "'";
}

// what a place tells of a path, as one line
static std::string stat(const place & at, const std::string & given) {
    uintmax_t        size = 0;
    std::string      why;
    const place_kind kind  = place_stat(at, given, size, why, ALIVE);
    const char *     names = "nfdo";
    return why.empty() ? std::string(1, names[kind]) + " " + std::to_string(size) : "! " + why;
}

int main() {
    laws_of("place_test");
    const fs::path root = fs::temp_directory_path() / ("place_test." + std::to_string(std::random_device{}()));
    const fs::path box  = root / "box";
    std::string    binary;
    for (int i = 0; i < 256; i++) {
        binary += (char) i;
    }
    binary += std::string("\r\n\n\0end", 7);
    put(box / "a.txt", "alpha\n");
    put(box / "bin", binary);
    put(box / "with space/b it's.txt", "beta");
    put(box / "deep/er/est/c.txt", std::string(3000, 'c'));
    put(box / ".hidden/h.txt", "hidden");
    put(box / "node_modules/m/x.js", "x");
    put(box / "été.txt", "ascii in utf-8 named");
    std::error_code ec;
    fs::create_symlink("a.txt", box / "link", ec);
    const bool linked = !ec;

    place host;
    host.cwd = box.u8string();
    place shell;
    shell.cwd                 = root.u8string();
    shell.shell               = "sh -c \"cd box && exec sh -s\"";
    shell.timeout             = 30;
    // the same shell behind a wrapper that prints around it
    place noisy               = shell;
    noisy.shell               = "sh -c \"echo QUJD; cd box && sh -s; r=$?; echo QUJD; exit $r\"";
    // a wrapper whose first line is empty
    place blank               = shell;
    blank.shell               = "sh -c \"echo; cd box && sh -s\"";
    std::vector<place> places = { host };
    if (!job_find(shell.shell).empty()) {
        // names only a frame of NUL tells apart, a POSIX machine holding them
        put(box / "odd/new\nline", "ab");
        put(box / "odd/x -> y", "abc");
        put(box / "odd/ lead", "a");
        put(box / "odd/back\\slash", "abcd");
        put(box / "odd/KISS-END", "e");
        fs::create_symlink("x -> y", box / "odd/lnk -> z", ec);
        places.push_back(shell);
        places.push_back(noisy);
        places.push_back(blank);
        const job_result quiet = job_run("sh -s", root.u8string(),
                                         "exec > /dev/null 2>&1\ni=0\nwhile [ $i -lt 20000 ]; do i=$((i + 1)); done\n"
                                         "echo done > late.txt\n",
                                         60, 4096, ALIVE);
        law(quiet.exit_code == 0 && fs::exists(root / "late.txt"),
            "a command that closes its output runs on to its end");
    } else {
        printf("[Laws] %s: no POSIX shell here, the laws of the server alone\n", g_test);
    }

    for (const place & at : places) {
        const std::string where = at.shell.empty()        ? "the server: " :
                                  at.shell == shell.shell ? "a shell: " :
                                  at.shell == blank.shell ? "a shell after an empty line: " :
                                                            "a wrapped shell: ";
        law(stat(at, "a.txt") == "f 6" && stat(at, "deep") == "d 0" && stat(at, "nothing") == "n 0",
            where + "a path names a file with its size, a directory, or nothing");
        std::string bytes;
        law(place_read(at, "bin", 1 << 20, bytes, ALIVE).empty() && bytes == binary,
            where + "every byte of a file reads as it is");
        law(place_read(at, "with space/b it's.txt", 1 << 20, bytes, ALIVE).empty() && bytes == "beta",
            where + "a path with a space and a quote reads");
        law(place_read(at, "deep/er/est/c.txt", 100, bytes, ALIVE) == "File larger than 100 bytes",
            where + "a file past the limit loads not");
        law(!place_read(at, "nothing", 100, bytes, ALIVE).empty(), where + "a file that is not there loads not");
        bool existed = true;
        law(place_write(at, "new/dir/n.bin", binary, existed, ALIVE).empty() && !existed &&
                read(box / "new/dir/n.bin") == binary,
            where + "a file is written byte for byte with its directories made");
        law(place_write(at, "new/dir/n.bin", "", existed, ALIVE).empty() && existed &&
                read(box / "new/dir/n.bin").empty(),
            where + "a file is written over, empty, telling it was there");
        law(!place_write(at, "deep", "x", existed, ALIVE).empty(), where + "a directory is not written over");
        fs::remove_all(box / "new");
        const std::vector<std::string> file = walked(at, "a.txt");
        law(file.size() == 1 && file[0].rfind("! ", 0) == 0, where + "a file is no directory to walk");
    }

    const std::vector<std::string> server = walked(host, ".");
    law(std::find(server.begin(), server.end(), "deep/er 1") != server.end() &&
            std::find(server.begin(), server.end(), "deep/er/est/c.txt 3 file 3000") != server.end(),
        "the server walks every entry with its depth and the size of a file");
    if (linked) {
        law(std::find(server.begin(), server.end(), "link 0") != server.end(), "a link is no regular file");
    }
    if (places.size() > 1) {
        law(walked(shell, ".") == server, "a shell walks the entries the server walks, whatever their names");
        law(walked(noisy, ".") == server && walked(blank, ".") == server, "a wrapped shell walks them the same");
        // a wc that pads its counts with blanks, as the one of macOS does
        // when it reads stdin, first on the PATH
        const fs::path pads = root / "pads";
        put(pads / "wc", "#!/bin/sh\n" + sh_quoted(job_find("wc")) + " \"$@\" | sed 's/^/   /'\n");
        fs::permissions(pads / "wc", fs::perms::owner_all, fs::perm_options::add, ec);
        place padded = shell;
        padded.shell = "sh -c \"PATH=" + sh_quoted(pads.u8string()) + ":$PATH; cd box && exec sh -s\"";
        law(walked(padded, ".") == server, "a shell whose wc pads its counts walks them the same");
        // a wc that loses the first line of a batch, as one that cannot read a
        // file does, the batch then counted one by one
        const fs::path loses = root / "loses";
        put(loses / "wc", "#!/bin/sh\nif [ $# -gt 1 ]; then " + sh_quoted(job_find("wc")) + " \"$@\" | sed 1d; else " +
                              sh_quoted(job_find("wc")) + " \"$@\"; fi\n");
        fs::permissions(loses / "wc", fs::perms::owner_all, fs::perm_options::add, ec);
        place losing = shell;
        losing.shell = "sh -c \"PATH=" + sh_quoted(loses.u8string()) + ":$PATH; cd box && exec sh -s\"";
        law(walked(losing, ".") == server, "a batch wc loses a line of is counted one by one");
        // a far side with wc and no find, then with neither: a copy of wc in a
        // directory of its own, the paths quoted for the shell
        const fs::path bin = root / "bin";
        fs::create_directories(bin);
        const bool copied = fs::copy_file(job_find("wc"), bin / "wc", ec) && !ec;
        place      bare   = shell;
        bare.shell =
            "sh -c \"PATH=" + sh_quoted(bin.u8string()) + "; cd box && exec " + sh_quoted(job_find("sh")) + " -s\"";
        if (copied) {
            law(walked(bare, ".") ==
                    std::vector<std::string>{ "! find lists nothing here, missing or without -mindepth" },
                "a far side without find lists nothing and says so");
        }
        fs::remove(bin / "wc", ec);
        law(walked(bare, ".") == std::vector<std::string>{ "! wc is found nowhere on the far side" },
            "a far side without wc lists nothing and says so");
        place mute = shell;
        mute.shell = "sh -c \"cat > /dev/null; echo QUJD\"";
        std::string bytes;
        law(place_read(mute, "a.txt", 100, bytes, ALIVE).rfind("The shell answered nothing the server reads", 0) == 0,
            "a shell that frames no answer reads nothing");
        place slipping = shell;
        slipping.shell = "sh -c \"cd box && sh -s | awk 'NR == 3 { print substr($0, 1, 4) } { print }'\"";
        law(place_read(slipping, "bin", 1 << 20, bytes, ALIVE).rfind("Cannot read the file", 0) == 0 && bytes.empty(),
            "a line a wrapper slips into the answer fails the read, never a byte more");
    }
    std::string decoded;
    law(base64_decode(base64_encode(binary), decoded) && decoded == binary, "base64 gives back every byte");
    law(!base64_decode("QUJD\nWARN: late", decoded), "base64 refuses a byte outside its alphabet");

    std::ofstream(root / "server.conf") << "set shells command box \"sh -s\"\nset shells timeout box 7\n";
    laws_factory();
    engine_start((root / "server.conf").u8string(), "test");
    const auto given = [] {
        place p;
        engine_with([&](const engine_reader & config) { p = place_of(config); });
        return p;
    };
    law(given().shell.empty(), "the tools on files act on the machine of the server by default");
    const fs::path seen = root / "seen";
    put(seen / "a.txt", "a");
    put(seen / ".hidden", "h");
    put(seen / "sub" / "b.txt", "b");
    put(seen / "node_modules" / "inner" / "pkg.js", "p");
    std::vector<tool> tools;
    engine_with([&](const engine_reader & config) { tools = tools_list(config); });
    const auto     view = std::find_if(tools.begin(), tools.end(), [](const tool & t) { return t.suffix == "view"; });
    const json_doc args = json_read("{\"path\":" + json_quote(seen.u8string()) + "}");
    const tool_outcome listed = view->run(yyjson_doc_get_root(args.get()), {}, ALIVE);
    const auto         ends   = [&](const std::string & tail) {
        return listed.text.size() >= tail.size() &&
               listed.text.compare(listed.text.size() - tail.size(), tail.size(), tail) == 0;
    };
    law(listed.ok && listed.text.find("b.txt\n") != std::string::npos &&
            listed.text.find("node_modules\n") != std::string::npos && listed.text.find("inner") == std::string::npos &&
            ends("4 entries, 1 hidden"),
        "view lists a directory two levels deep, its hidden entries counted, node_modules closed");
    engine_run("set files shell box", ENGINE_ADMIN);
    law(given().shell == "sh -s" && given().timeout == 7 && given().broken.empty(),
        "the tools on files act through the shell files shell names");
    engine_run("set files shell vm", ENGINE_ADMIN);
    std::string bytes;
    law(given().broken == "files shell vm names no shell" &&
            place_read(given(), "a.txt", 100, bytes, ALIVE) == "files shell vm names no shell",
        "a files shell that names no shell fails every call, never on the machine of the server");

    fs::remove_all(root);
    const int code = laws_told();
    quit_all();
    return code;
}
