// tools.cpp: the sources of the tools, each registered by its file

#include "tools.h"

static std::vector<tool_source> & sources() {
    static std::vector<tool_source> list;
    return list;
}

bool tools_register(tool_source source) {
    sources().push_back(source);
    return true;
}

std::vector<tool> tools_list(const engine_reader & config) {
    std::vector<tool> out;
    for (tool_source source : sources()) {
        for (tool & t : source(config)) {
            out.push_back(std::move(t));
        }
    }
    return out;
}
