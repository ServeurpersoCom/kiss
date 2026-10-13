// archive.cpp: the startup-config, server.conf as a file

#include "engine/archive.h"

#include "engine/text.h"
#include "utf8.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

static std::string g_path;

void archive_open(const std::string & path) {
    g_path = path;
}

const std::string & archive_path() {
    return g_path;
}

std::string archive_read(bool & found) {
    found    = false;
    FILE * f = utf8_fopen(g_path.c_str(), "rb");
    if (!f) {
        return "";
    }
    found = true;
    std::string text;
    char        buf[4096];
    size_t      n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    fclose(f);
    return text;
}

std::string archive_write(const layer & l) {
    const std::string tmp = g_path + ".tmp";
    FILE *            f   = utf8_fopen(tmp.c_str(), "wb");
    if (!f) {
        fail("cannot write " + tmp);
    }
    const std::string text = join(layer_lines(l, true), "\n") + "\n";
    const bool        ok   = fwrite(text.data(), 1, text.size(), f) == text.size();
    if (fclose(f) != 0 || !ok) {
        fail("cannot write " + tmp);
    }
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(g_path), ec);
    if (ec) {
        fail("cannot write " + g_path + ": " + ec.message());
    }
    return comment(std::string("copied ") + RUNNING_CONFIG + " to " + STARTUP_CONFIG);
}

std::string archive_erase() {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(g_path), ec);
    if (ec) {
        fail("cannot erase " + g_path + ": " + ec.message());
    }
    return comment(std::string("erased ") + STARTUP_CONFIG + ", the server starts on its factory configuration");
}
