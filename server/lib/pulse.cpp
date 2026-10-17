// pulse.cpp: the clock of one turn of the agent

#include "lib/pulse.h"

#include "yyjson.h"

#include <chrono>
#include <cmath>
#include <memory>

double pulse_now() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

pulse pulse_start(double now) {
    pulse p;
    p.since = now;
    p.start = now;
    return p;
}

void pulse_mark(pulse & p, const char * phase, const std::string & name, double now) {
    if (p.phase == phase && p.name == name) {
        return;
    }
    p.phase = phase;
    p.name  = name;
    p.since = now;
}

void pulse_retry(pulse & p, const std::string & name, int status, int attempt, int attempts, double now) {
    pulse_mark(p, PHASE_RETRYING, name, now);
    p.status   = status;
    p.attempt  = attempt;
    p.attempts = attempts;
}

void pulse_chunk(pulse & p, bool token, double now) {
    if (p.first < 0) {
        p.first = now;
    }
    p.last = now;
    p.tokens += token ? 1 : 0;
}

void pulse_close(pulse & p, std::optional<double> usage) {
    if (p.first < 0) {
        return;
    }
    p.total += usage ? *usage : p.tokens;
    p.generation += p.last - p.first;
    p.first  = -1;
    p.tokens = 0;
}

void pulse_held(pulse & p, double ms) {
    p.since += ms;
    p.start += ms;
}

std::optional<conversation_stats> pulse_stats(pulse & p, double now) {
    const double last = p.last;
    pulse_close(p, std::nullopt);
    if (last < 0) {
        return std::nullopt;
    }
    return conversation_stats{ p.total, std::round(p.generation), std::round(now - p.start) };
}

std::string pulse_json(const pulse & p) {
    // a moment of pulse_now() on the clock of conversation_now()
    const double shift = conversation_now() - pulse_now();
    const auto   at    = [&](double t) {
        return (int64_t) std::llround(t + shift);
    };
    const std::shared_ptr<yyjson_mut_doc> doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_val *                      o = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), o);
    yyjson_mut_obj_add_int(doc.get(), o, "round", p.round);
    yyjson_mut_obj_add_strcpy(doc.get(), o, "phase", p.phase.c_str());
    yyjson_mut_obj_add_strncpy(doc.get(), o, "name", p.name.data(), p.name.size());
    yyjson_mut_obj_add_int(doc.get(), o, "status", p.status);
    yyjson_mut_obj_add_int(doc.get(), o, "attempt", p.attempt);
    yyjson_mut_obj_add_int(doc.get(), o, "attempts", p.attempts);
    yyjson_mut_obj_add_int(doc.get(), o, "since", at(p.since));
    if (p.first < 0) {
        yyjson_mut_obj_add_null(doc.get(), o, "first");
        yyjson_mut_obj_add_null(doc.get(), o, "last");
    } else {
        yyjson_mut_obj_add_int(doc.get(), o, "first", at(p.first));
        yyjson_mut_obj_add_int(doc.get(), o, "last", at(p.last));
    }
    yyjson_mut_obj_add_int(doc.get(), o, "tokens", (int64_t) std::llround(p.tokens));
    const std::shared_ptr<char> text(yyjson_mut_write(doc.get(), 0, nullptr), free);
    return text.get();
}
