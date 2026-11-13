// quit.cpp: the end of the process

#include "lib/quit.h"

#include "lib/agent.h"
#include "lib/api.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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

// the milliseconds between two looks at whether every thread is done
static const int TICK_MS = 100;

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
    // the stops are made again until every thread is done, as a stop finds a
    // request only once it went out; then every thread is joined, a thread
    // joined maybe having started another
    for (;;) {
        api_quit();
        agent_quit();
        std::list<started_t> started;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_started.empty()) {
                return;
            }
            const bool done =
                std::all_of(g_started.begin(), g_started.end(), [](const started_t & t) { return t.done->load(); });
            if (done) {
                started.swap(g_started);
            }
        }
        if (started.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
        }
        for (started_t & t : started) {
            t.thread.join();
        }
    }
}
