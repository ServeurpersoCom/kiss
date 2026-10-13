#pragma once
// file.h: the files the tools on files reach: a path as a tool takes it, a
// text loaded whole, bytes written over a file in place, and where a string
// occurs in a text

#include <filesystem>
#include <string>

// the bytes read at a time, and the first ones that tell an image or a binary
constexpr size_t FILE_CHUNK = 65536;

// a path relative to the working directory, or absolute
std::filesystem::path file_path(const std::string & cwd, const std::string & given);

// the whole of a text file of at most limit bytes, a NUL in its first chunk
// telling a binary; the message of what keeps it from loading, empty when
// it loads
std::string file_load(const std::filesystem::path & path, size_t limit, std::string & text);

// the bytes over the file, written in place, so its mode, its owner and its
// links stay, its parent directories made first; the message of the failure,
// empty when it is written
std::string file_write(const std::filesystem::path & path, const std::string & bytes);

// the times what occurs in text, none overlapping, and where the first starts
struct file_found {
    size_t count;
    size_t at;
};

file_found file_find(const std::string & text, const std::string & what);
