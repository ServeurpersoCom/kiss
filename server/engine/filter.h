#pragma once
// filter.h: the filter after a pipe, as IOS reads it

#include "engine/text.h"

#include <functional>

using filter_t = std::function<words_t(const words_t &)>;

// the filters a pipe takes
const words_t & filter_names();

// the filter a pipe spells, compiled once: a bad name or pattern fails here,
// before anything runs
filter_t compile_filter(const std::string & spec);
