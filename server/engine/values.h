#pragma once
// values.h: the layers of the configuration and how a module reads them

#include "engine/engine.h"
#include "engine/text.h"

#include <map>
#include <set>

using values_t = std::map<std::string, std::string>;

// a layer of the configuration: every stored key it holds, with its value
using layer = values_t;

// the factory configuration the binary embeds, below every other layer
layer & factory_layer();

// a configuration as a module reads it: a layer over the factory
// configuration, then the reset value of the key
struct reader : engine_reader {
    const layer & over;

    explicit reader(const layer & l) : over(l) {}

    std::optional<std::string> get(const std::string & key, const std::string & item) const override;
    std::optional<std::string> reveal(const std::string & key, const std::string & item) const override;
    std::vector<std::string>   names(const std::string & module) const override;
};

// the items of a collection one layer holds, sorted
words_t layer_names(const layer & l, const std::string & module);

// the lines of a layer, sorted; secrets in view only when they are to be
// written
words_t layer_lines(const layer & l, bool secrets);

// what a change does, line by line: the old line after -, the new one after
// +, every value resolved through the layers, a register existing while some
// layer names its item; a secret replaced by another
// reads as one line, its values out of view
words_t diff(const layer & from, const layer & to);

// the stored keys whose value differs between two layers
std::set<std::string> changed(const layer & a, const layer & b);
