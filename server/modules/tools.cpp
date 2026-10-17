// tools.cpp: the tools module: every setting of a tool the agent of this
// server calls, the twin of the tools module of the page; a tool of this
// server is named here by its own name, bash, view, as an interface of a
// router is, the name <hostname>_server_bash being how a client sees it

#include "engine/engine.h"
#include "engine/text.h"

// off, the model never sees the tool; consent, each call asks the user; on
static const char * const USES[] = { "off", "consent", "on" };

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

    engine_module m;
    m.name = "tools";
    m.keys = { rounds, use };
    return m;
}

ENGINE_MODULE(make_tools);
