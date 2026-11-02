// quit.cpp: the end of the process

#include "lib/quit.h"

#include "lib/agent.h"
#include "lib/api.h"

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

// a thread started, and whether its work is done, so the next start joins it
// at once
struct started_t {
    std::thread                        thread;
    std::shared_ptr<std::atomic<bool>> done;
};

static std::mutex           g_mutex;
static std::list<started_t> g_started;
static std::atomic<bool>    g_ending{ false };

void quit_thread(std::function<void()> f) {
    const auto                  done = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_started.begin(); it != g_started.end();) {
        if (*it->done) {
            it->thread.join();
            it = g_started.erase(it);
        } else {
            ++it;
        }
    }
    g_started.push_back({ std::thread([f = std::move(f), done] {
                              f();
                              *done = true;
                          }),
                          done });
}

bool quit_ending() {
    return g_ending;
}

void quit_all() {
    g_ending = true;
    api_quit();
    agent_quit();
    // a thread joined may have started another, joined in turn
    for (;;) {
        std::list<started_t> started;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            started.swap(g_started);
        }
        if (started.empty()) {
            return;
        }
        for (started_t & s : started) {
            s.thread.join();
        }
    }
}
