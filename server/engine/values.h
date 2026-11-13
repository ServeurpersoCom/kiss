#pragma once
// values.h: the layers of the configuration and how a module reads them

#include "engine/engine.h"
#include "engine/text.h"

#include <map>
#include <set>

// a layer of the configuration: every stored key it holds, with its value,
// or none where it takes back the value of the factory configuration, as IOS
// keeps no ip http server
using layer = std::map<std::string, std::optional<std::string>>;

// the factory configuration the binary embeds, below every other layer
layer & factory_layer();

// a configuration as a module reads it: a layer over the factory
// configuration, then the reset value of the key, which a layer that takes a
// key back gives at once
struct reader : engine_reader {
    const layer & over;

    explicit reader(const layer & l) : over(l) {}

    std::optional<std::string> get(const std::string & key, const std::string & item) const override;
    std::optional<std::string> reveal(const std::string & key, const std::string & item) const override;
    std::vector<std::string>   names(const std::string & module) const override;
};

// whether the factory configuration holds a stored key, the only one a
// layer takes back
bool factory_holds(const std::string & s);

// the lines of a layer, sorted, a key taken back as its no line; secrets in
// view only when they are to be written
words_t layer_lines(const layer & l, bool secrets);

// what a change does, line by line: the old line after -, the new one after +,
// every value resolved through the layers, a register existing while some
// layer names its item; a secret replaced by another reads as one line, its
// values out of view
words_t diff(const layer & from, const layer & to);

// the stored keys whose value differs between two layers
std::set<std::string> changed(const layer & a, const layer & b);
