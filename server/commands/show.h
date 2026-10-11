#pragma once
// show.h: what show prints of a module, which show running-config all prints
// for every module

#include "engine/values.h"

// the values the words name, defaults included, as set lines to paste back
std::string show_module(const reader &      config,
                        const std::string & module,
                        const std::string & key,
                        const std::string & item);
