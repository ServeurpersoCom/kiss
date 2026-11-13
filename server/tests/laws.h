#pragma once
// laws.h: the laws a test holds: each one counted, the first broken ones
// told, and the count the test ends on

#include <cstddef>
#include <cstdio>
#include <string>

// the broken laws a test tells one by one, the others only counted
constexpr size_t LAWS_TOLD = 8;

inline const char * g_test  = "";
inline size_t       g_laws  = 0;
inline size_t       g_fails = 0;

// the test the laws to come are those of, by the name of its target
inline void laws_of(const char * test) {
    g_test = test;
}

inline void law(bool kept, const std::string & what) {
    g_laws++;
    if (!kept && g_fails++ < LAWS_TOLD) {
        fprintf(stderr, "[Laws] %s broken: %s\n", g_test, what.c_str());
    }
}

// the count the test ends on, and the code it exits with
inline int laws_told() {
    printf("[Laws] %s: %zu laws, %zu broken\n", g_test, g_laws, g_fails);
    return g_fails ? 1 : 0;
}
