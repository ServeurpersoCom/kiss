// files.cpp: the laws of the files the tools on files reach, run as the build
// ends: a path is taken from the working directory and refused with a NUL in
// it, a text loads whole with its bytes as they are and refuses a binary or a
// file past its limit, bytes are written in place with their directories made
// and the mode and the link of the file kept, and a string is found where it
// first occurs, its count telling whether it is unique

#include "file.h"

#include <cstdio>
#include <random>
#include <string>

namespace fs = std::filesystem;

static size_t g_laws  = 0;
static size_t g_fails = 0;

static void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < 8) {
        fprintf(stderr, "files_test: %s\n", what.c_str());
    }
}

static std::string loaded(const fs::path & path, size_t limit = 1 << 20) {
    std::string       text;
    const std::string why = file_load(path, limit, text);
    return why.empty() ? text : "! " + why;
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("files_test." + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const std::string cwd = root.u8string();

    law(file_path(cwd, "a/b.txt") == root / "a/b.txt", "a relative path starts at the working directory");
    law(file_path(cwd, (root / "x").u8string()) == root / "x", "an absolute path stays as it is");
    law(file_refused(std::string("f.txt\0.sh", 9)) == "Path holds a NUL byte" && file_refused("f.txt").empty(),
        "a path holding a NUL names no file");

    const std::string bytes = "\xEF\xBB\xBFone\r\ntwo\r\n\xC3\xA9t\xC3\xA9";
    law(file_write(root / "deep/er/f.txt", bytes).empty(), "a file is written with its directories made");
    law(loaded(root / "deep/er/f.txt") == bytes, "the bytes load back as written, BOM, CRLF and UTF-8 kept");
    law(file_write(root / "deep/er/f.txt", "short").empty() && loaded(root / "deep/er/f.txt") == "short",
        "a file written over holds the new bytes alone");
    law(file_write(root / "empty", "").empty() && loaded(root / "empty").empty(), "an empty text loads empty");

    law(loaded(root / "none").rfind("! No such file or directory: ", 0) == 0, "a missing file is named");
    law(loaded(root / "deep").rfind("! Not a regular file: ", 0) == 0, "a directory does not load");
    law(loaded(root / "deep/er/f.txt", 4) == "! File larger than 4 bytes", "a file past the limit does not load");
    file_write(root / "bin", std::string("ab\0cd", 5));
    law(loaded(root / "bin") == "! Binary file", "a NUL in the first chunk tells a binary");

#if !defined(_WIN32)
    // modes and links as POSIX has them
    const fs::perms mode = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec;
    file_write(root / "run.sh", "echo a\n");
    fs::permissions(root / "run.sh", mode);
    file_write(root / "run.sh", "echo b\n");
    law((fs::status(root / "run.sh").permissions() & fs::perms::mask) == mode, "a file written over keeps its mode");

    fs::create_symlink("run.sh", root / "link");
    file_write(root / "link", "echo c\n");
    law(fs::is_symlink(root / "link") && loaded(root / "run.sh") == "echo c\n",
        "a link written through stays a link and its target holds the bytes");
#endif

    law(file_write(root / "deep", "x").rfind("Cannot write the file: ", 0) == 0, "a directory is not written over");

    const file_found once = file_find("a\nbc\nd", "bc");
    law(once.count == 1 && once.at == 2, "a unique string is found where it starts");
    law(file_find("abc", "x").count == 0, "a missing string counts none");
    law(file_find("abab ab", "ab").count == 3 && file_find("abab ab", "ab").at == 0, "every place counts");
    law(file_find("aaaa", "aa").count == 2, "places never overlap");
    law(file_find("a\r\nb", "a\nb").count == 0, "bytes match as they are, LF never matches CRLF");

    fs::remove_all(root);
    printf("files_test: %zu laws, %zu broken\n", g_laws, g_fails);
    return g_fails ? 1 : 0;
}
