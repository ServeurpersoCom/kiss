#pragma once
// run.h: what the commands reach of the running engine

#include "engine/values.h"

#include <string>

// the running configuration
const layer & running_layer();

// server.conf as a layer of its own, run block by block by the administrator
// over nothing; skipped, when given, takes each block that stays out and why
layer startup_layer(words_t * skipped = nullptr);

// the version of the binary
const std::string & engine_version();
