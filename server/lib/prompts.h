#pragma once
// prompts.h: what a model reads of this server, from the prompts.json it
// embeds: each tool, its description and the JSON schema of its arguments,
// each module, what it sets, and the notes of the agent; a {mark} of a description stands for what
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

// a note the agent adds to what the model reads: problems, which heads what
// a turn left out
std::string prompts_note(const std::string & note);

// what the modules of the engine miss in prompts.json, empty when every one
// has its text
std::string prompts_check();
