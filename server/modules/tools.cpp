// tools.cpp: the tools module: every setting of a tool the agent of this
// server calls, the twin of the tools module of the page; a tool of this
// server is named here by its own name, bash, view, as an interface of a
// router is, the name <hostname>_server_bash being how a client sees it; its
// items, the tools of this server, of the pages and of every MCP server

#include "lib/tools.h"

#include "engine/engine.h"
#include "engine/keychain.h"
#include "engine/text.h"
#include "lib/agent.h"
#include "lib/mcp.h"
#include "lib/remote.h"

#include <thread>

// off, the model never sees the tool; consent, each call asks the user; on
static const char * const USES[] = { "off", "consent", "on" };

// the tools of this server, then the one the pages serve, then those of every
// MCP server at once, by server, each asked in a thread of its own, one that
// fails holding why
static std::vector<engine_group> items(const engine_reader & config) {
    std::vector<engine_group> out = {
        { "KiSS",  {},                    "" },
        { "pages", { AGENT_CLIENT_TOOL }, "" }
    };
    for (const tool & t : tools_list(config)) {
        out[0].names.push_back(t.suffix);
    }
    const words_t             names = config.names("mcp");
    std::vector<remote>       servers;
    std::vector<engine_group> mcp(names.size());
    for (const std::string & n : names) {
        servers.push_back(remote_of(config, "mcp", n, BEARER));
    }
    std::vector<std::thread> asking;
    for (size_t i = 0; i < servers.size(); i++) {
        asking.emplace_back([&servers, &mcp, i] {
            mcp[i].group = "mcp " + servers[i].name;
            try {
                api_stop stop;
                for (const mcp_tool & t : mcp_tools(servers[i], stop)) {
                    mcp[i].names.push_back(t.name);
                }
            } catch (const std::exception & e) {
                mcp[i].error = e.what();
            }
        });
    }
    for (std::thread & t : asking) {
        t.join();
    }
    out.insert(out.end(), mcp.begin(), mcp.end());
    return out;
}

static engine_module make_tools() {
    // the rounds of tool calls a turn takes at most
    engine_key rounds("max-rounds", ENGINE_NUMBER);
    rounds.min     = 1;
    rounds.integer = true;
    rounds.reset   = "1000";

    engine_key use("use", ENGINE_ENUM);
    use.named  = true;
    use.values = words_t(std::begin(USES), std::end(USES));
    use.reset  = "consent";
    use.guard  = true;

    engine_module m;
    m.name  = "tools";
    m.keys  = { rounds, use };
    m.items = items;
    return m;
}

ENGINE_MODULE(make_tools);
