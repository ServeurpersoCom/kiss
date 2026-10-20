#pragma once
// prompts.h: what a model reads of this server, from the prompts.json it
// embeds: each tool, its description and the JSON schema of its arguments,
// each module and what it sets; and the config tool
// of the page, from the prompts.json of the page it embeds too; a {mark} of a
// description stands for what only the running server knows

#include <string>
#include <utility>
#include <vector>

using prompt_marks = std::vector<std::pair<std::string, std::string>>;

// the description of a tool, each {mark} replaced by its value
std::string prompts_description(const std::string & tool, const prompt_marks & marks);

// the JSON schema of the arguments of a tool
std::string prompts_input(const std::string & tool);

// the description of the config tool of the page, as the page reads it, each
// {mark} replaced by its value
std::string prompts_page_description(const prompt_marks & marks);

// the JSON schema of the arguments of the config tool of the page
std::string prompts_page_input();

// what a module sets
std::string prompts_module(const std::string & module);

// a text of the compaction of a history, from the prompts.json of the page:
// ask, what a summary asks the model, or summary, the text a summary is read
// in, {summary} standing for it
std::string prompts_compaction(const std::string & name);

// what the modules of the engine miss in prompts.json, empty when every one
// has its text
std::string prompts_check();
