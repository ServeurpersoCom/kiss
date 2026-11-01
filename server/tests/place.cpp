// place.cpp: the laws of the place the tools on files act on, run as the
// build ends: the machine of the server and a POSIX shell that reaches the
// same directory answer alike, what a path names and its size, the bytes of a
// file whatever they are, a file written in place with its directories made,
// telling whether it was there, and every entry below a directory with its
// depth and the size of a regular file; a file past the limit loads through
// neither; a machine with no POSIX shell keeps the laws of the server alone;
// the configuration gives the tools the shell files shell names, else the
// machine of the server

#include "lib/place.h"

#include "engine/engine.h"
#include "lib/job.h"
#include "server/server.conf.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "[Laws] place_test broken: %s\n", what.c_str());
    }
}

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

// what a place tells of a path, as one line
static std::string stat(const place & at, const std::string & given) {
    uintmax_t        size = 0;
    std::string      why;
    const place_kind kind  = place_stat(at, given, size, why, ALIVE);
    const char *     names = "nfdo";
    return why.empty() ? std::string(1, names[kind]) + " " + std::to_string(size) : "! " + why;
}

int main() {
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

    place host;
    host.cwd = box.u8string();
    place shell;
    shell.cwd                 = root.u8string();
    shell.shell               = "sh -c \"cd box && exec sh -s\"";
    shell.timeout             = 30;
    std::vector<place> places = { host };
    if (!job_find(shell.shell).empty()) {
        places.push_back(shell);
    } else {
        printf("[Laws] place_test: no POSIX shell here, the laws of the server alone\n");
    }

    for (const place & at : places) {
        const std::string where = at.shell.empty() ? "the server: " : "a shell: ";
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
    }

    const std::vector<std::string> server = walked(host, ".");
    law(std::find(server.begin(), server.end(), "deep/er 1") != server.end() &&
            std::find(server.begin(), server.end(), "deep/er/est/c.txt 3 file 3000") != server.end() &&
            std::find(server.begin(), server.end(), "link 0") != server.end(),
        "the server walks every entry with its depth, a link no regular file");
    if (places.size() > 1) {
        law(walked(shell, ".") == server, "a shell walks the entries the server walks");
    }

    std::ofstream(root / "server.conf") << "set shells command box \"sh -s\"\nset shells timeout box 7\n";
    engine_factory(std::string(reinterpret_cast<const char *>(server_server_conf), server_server_conf_len));
    engine_start((root / "server.conf").u8string(), "test");
    const auto given = [] {
        place p;
        engine_with([&](const engine_reader & config) { p = place_of(config); });
        return p;
    };
    law(given().shell.empty(), "the tools on files act on the machine of the server by default");
    engine_run("set files shell box", ENGINE_ADMIN);
    law(given().shell == "sh -s" && given().timeout == 7, "the tools on files act through the shell files shell names");

    fs::remove_all(root);
    printf("[Laws] place_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
