#pragma once
// prompts.h: what a model reads of this server, from the prompts.json it
// embeds: each tool, its description and the JSON schema of its arguments,
// and each module, what it sets; a {mark} of a description stands for what
// only the running server knows

#include <string>
#include <utility>
#include <vector>

using prompt_marks = std::vector<std::pair<std::string, std::string>>;

// the description of a tool, each {mark} replaced by its value
std::string prompts_description(const std::string & tool, const prompt_marks & marks);

// the JSON schema of the arguments of a tool
std::string prompts_input(const std::string & tool);

// what a module sets
std::string prompts_module(const std::string & module);

// what the modules of the engine miss in prompts.json, empty when every one
// has its text
std::string prompts_check();
