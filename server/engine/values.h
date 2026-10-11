#pragma once
// values.h: the layers of the configuration and how a module reads them

#include "engine/engine.h"
#include "engine/text.h"

#include <map>
#include <set>

using values_t = std::map<std::string, std::string>;

// a layer of the configuration: the lines without a platform, the lines of
// this platform, and the lines of the other platforms, kept to be written
// back whole
struct layer {
    values_t                        all;
    values_t                        own;
    std::map<std::string, values_t> foreign;

    bool operator==(const layer & o) const { return all == o.all && own == o.own && foreign == o.foreign; }
};

// the values of a platform within a layer: own for this platform, all for none
values_t & scope(layer & l, const std::string & platform);

// the defaults the binary embeds, below every other layer
layer & defaults_layer();

// a configuration as a module reads it: a layer over the defaults, each
// taking the value of this platform before the one of none, then the
// fallback of the key
struct reader : engine_reader {
    const layer & over;

    explicit reader(const layer & l) : over(l) {}

    // where a value is looked for, the first that holds it winning
    std::vector<const values_t *> scopes() const;

    std::optional<std::string> get(const std::string & key, const std::string & item) const override;
    std::vector<std::string>   names(const std::string & module) const override;
};

// the lines of a layer: none, then this platform, then the others, sorted;
// secrets in view only when they are to be written
words_t layer_lines(const layer & l, bool secrets);

// what a change does, line by line: the old line after -, the new one after
// +, every value of this platform resolved through the layers and the lines
// of the other platforms as they are; a secret replaced by another reads as
// one line, its values out of view
words_t diff(const layer & from, const layer & to);

// the stored keys whose value differs between two layers, foreign ones aside
std::set<std::string> changed(const layer & a, const layer & b);
