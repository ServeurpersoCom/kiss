#pragma once
// place.h: the place the tools on files act on: the machine of the server,
// a path read from its working directory, or the machine a shell reaches, a
// path read by a POSIX shell there, its answer framed, the bytes going
// through base64 both ways and their count checked, the names of a listing
// apart by NUL, the one byte no name holds

#include "engine/engine.h"

#include <cstdint>
#include <functional>
#include <string>

struct place {
    // the working directory of the server, a relative path on its machine
    // starting there
    std::string cwd;
    // the command line of the shell, empty for the machine of the server
    std::string shell;
    // the seconds a run of the shell may take
    int         timeout = 0;
    // what every call answers when the configuration names a shell that is
    // none, so the tools fail rather than act on the machine of the server
    std::string broken;
};

// the place the configuration gives the tools on files: the machine the
// shell files shell names reaches, else the machine of the server; a files
// shell that names no shell breaks the place
place place_of(const engine_reader & config);

enum place_kind {
    PLACE_NONE,
    PLACE_FILE,
    PLACE_DIRECTORY,
    PLACE_OTHER,
};

// an entry below a directory: its path relative to it, how deep, 0 for one
// right in it, and the size of a regular file that is no link
struct place_entry {
    std::string rel;
    int         depth   = 0;
    bool        regular = false;
    uintmax_t   size    = 0;
};

// tick is called while a run lasts, false meaning the client is gone

// what a path names, the size of a regular file; the message of a failure in
// why
place_kind place_stat(const place &                 p,
                      const std::string &           given,
                      uintmax_t &                   size,
                      std::string &                 why,
                      const std::function<bool()> & tick);

// the bytes of a regular file of at most limit bytes; the message of what
// keeps it from loading, empty when it loads
std::string place_read(const place &                 p,
                       const std::string &           given,
                       size_t                        limit,
                       std::string &                 bytes,
                       const std::function<bool()> & tick);

// the bytes over the file, written in place, its parent directories made,
// existed telling whether a file was there; the message of the failure,
// empty when it is written
std::string place_write(const place &                 p,
                        const std::string &           given,
                        const std::string &           bytes,
                        bool &                        existed,
                        const std::function<bool()> & tick);

// every entry below a directory, each handed to each, false stopping the walk
// with the message Cancelled; the message of a failure, empty when the walk
// ends
std::string place_walk(const place &                                    p,
                       const std::string &                              given,
                       size_t                                           limit,
                       const std::function<bool(const place_entry &)> & each,
                       const std::function<bool()> &                    tick);
