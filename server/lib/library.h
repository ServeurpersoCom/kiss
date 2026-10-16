#pragma once
// library.h: the conversations of the server, each the file <id>.json of the
// directory it lives in, holding that conversation alone, as the page writes
// one; read once as the server starts

#include "lib/conversation.h"

#include <string>
#include <vector>

// the conversations of a directory, a file that does not read logged and left
// out; none while it does not exist
void library_open(const std::string & dir);

// the conversations, the newest first
std::vector<conversation> library_list();

// the conversations gone, file and all; throws what keeps one from going,
// those before it gone
void library_remove(const std::vector<std::string> & ids);
