// console.cpp: the console of the interactive mode

#include "console.h"

#include "engine/engine.h"
#include "log.h"

#include <iostream>
#include <string>
#include <thread>

// the prompt names the server, as a router names its host: pod#
static void prompt() {
    log_console(engine_get("server name").value_or("") + "# ");
}

void console_start() {
    std::thread([] {
        std::string line;
        prompt();
        while (std::getline(std::cin, line)) {
            const engine_result r = engine_run(line, ENGINE_ADMIN);
            log_console(r.text + "\n");
            prompt();
        }
    }).detach();
}
