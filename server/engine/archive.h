#pragma once
// archive.h: the startup-config, server.conf as a file

#include "engine/values.h"

#include <string>

// the file the startup-config lives in
void                archive_open(const std::string & path);
const std::string & archive_path();

// the text of the file, found false when it does not exist yet
std::string archive_read(bool & found);

// the file written whole, through a file beside it renamed over it, so a
// failure never leaves half a file; the note that says so
std::string archive_write(const layer & l);
