#pragma once
// run.h: what the commands reach of the running engine

#include "engine/values.h"

#include <string>

// the running configuration
const layer & running_layer();

// server.conf as a layer of its own: a batch of the administrator over nothing
layer startup_layer();

// the version of the binary
const std::string & engine_version();
