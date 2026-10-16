// file.cpp: the files the tools on files reach

#include "lib/file.h"

#include "lib/utf8.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace fs = std::filesystem;

fs::path file_path(const std::string & cwd, const std::string & given) {
    fs::path path = fs::u8path(given);
    return path.is_relative() ? fs::u8path(cwd) / path : path;
}

std::string file_refused(const std::string & given) {
    return given.find('\0') == std::string::npos ? "" : "Path holds a NUL byte";
}

std::string file_load(const fs::path & path, size_t limit, std::string & text) {
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
    text.clear();
    char buf[FILE_CHUNK];
    for (size_t got; (got = fread(buf, 1, sizeof(buf), f)) > 0;) {
        text.append(buf, got);
        if (text.size() > limit) {
            fclose(f);
            return "File larger than " + std::to_string(limit) + " bytes";
        }
    }
    const bool failed = ferror(f) != 0;
    fclose(f);
    if (failed) {
        return "Cannot read the file";
    }
    if (memchr(text.data(), '\0', std::min(text.size(), FILE_CHUNK))) {
        return "Binary file";
    }
    return "";
}

std::string file_write(const fs::path & path, const std::string & bytes) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return "Cannot make the directory: " + ec.message();
        }
    }
    FILE * f = utf8_fopen(path.u8string().c_str(), "wb");
    if (!f) {
        return std::string("Cannot write the file: ") + strerror(errno);
    }
    const bool written = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const int  error   = errno;
    if (fclose(f) != 0 || !written) {
        return std::string("Cannot write the file: ") + strerror(written ? errno : error);
    }
    return "";
}

std::string file_replace(const fs::path & path, const std::string & bytes) {
    fs::path tmp = path;
    tmp += ".tmp";
    const std::string why = file_write(tmp, bytes);
    if (!why.empty()) {
        return why;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return ec ? "Cannot write the file: " + ec.message() : "";
}

file_found file_find(const std::string & text, const std::string & what) {
    file_found found{ 0, std::string::npos };
    for (size_t at = text.find(what); !what.empty() && at != std::string::npos;
         at        = text.find(what, at + what.size())) {
        if (!found.count++) {
            found.at = at;
        }
    }
    return found;
}
