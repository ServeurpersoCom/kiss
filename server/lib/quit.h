#pragma once
// quit.h: the end of the process, through main like any program: the work in
// flight stopped, then every thread it started joined, before its statics go

#include <functional>

// runs f on a thread the process joins as it ends
void quit_thread(std::function<void()> f);

// whether the process ends: what waits looks at it, and what starts once it
// does stops at once
bool quit_ending();

// ends the process: every request out closed, every turn stopped, then every
// thread quit_thread started joined, those they start too
void quit_all();
