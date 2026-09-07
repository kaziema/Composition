// Tests for keyframes, easing, and property evaluation.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ruby/core/Animation.h"

using namespace ruby::core;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void checkNear(double a, double b, const char* what, double eps = 1e-6) {
    if (std::fabs(a - b) > eps) {
        std::fprintf(stderr, "FAIL: %s (got %.8f, want %.8f)\n", what, a, b);
        ++failures;
    }
}

const TimeContext kAt30{30.0, 120.0, false};

// Whatever the easing, a segment has to start where it starts and end where it ends.
// Everything downstream (the graph editor, retiming, preset application) assumes it.
void easing_pins_both_endpoints() {
    const double eases[][3] = {{0.0, 0.0, 0.0}, {0.68, 1.0, 0.0}, {0.5, 0.5, 0.12},
                              {1.0, 1.0, 0.4}, {0.0, 1.0, 0.0}};
    for (const auto& e : eases) {
        checkNear(easeCurve(0.0, e[0], e[1], e[2]), 0.0, "ease starts at 0");
        checkNear(easeCurve(1.0, e[0], e[1], e[2]), 1.0, "ease ends at 1");
    }
}

void linear_ease_is_close_to_identity() {
    for (double t = 0.0; t <= 1.0; t += 0.1) {
        checkNear(easeCurve(t, 0.0, 0.0, 0.0), t, "no-ease curve is identity", 1e-3);
    }
}

// Ease-out means "leave slowly", so early in the segment the value should still be
// near its start. This is the difference between a preset that snaps and one that flows.
void ease_out_holds_near_the_start() {
    const double eased = easeCurve(0.25, 0.9, 0.0, 0.0);
    check(eased < 0.25, "heavy ease-out is behind linear at t=0.25");
}

// The assistant's OVER tile: the value sails past its target, then settles.
void overshoot_goes_past_one_then_settles() {
    bool exceeded = false;
    for (double t = 0.0; t <= 1.0; t += 0.01) {
        if (easeCurve(t, 0.0, 0.0, 0.5) > 1.0001) {
            exceeded = true;
        }
    }
    check(exceeded, "overshoot exceeds 1.0 somewhere in the segment");
    checkNear(easeCurve(1.0, 0.0, 0.0, 0.5), 1.0, "overshoot still lands on target");
}

void unanimated_property_returns_its_static_value() {
    Property p;
    p.staticValue = Value::vec2(60.0, 812.0);
    check(!p.animated(), "a property with no keys is not animated");
    check(approxEqual(p.evaluate(3.14, kAt30), Value::vec2(60.0, 812.0)),
          "unanimated property evaluates to its static value");
}

void evaluation_clamps_outside_the_keyed_range() {
    Property p;
    p.addKey({TimeValue::seconds(1.0), Value::scalar(10.0), Interpolation::Linear, 0, 0, 0},
             kAt30);
    p.addKey({TimeValue::seconds(2.0), Value::scalar(20.0), Interpolation::Linear, 0, 0, 0},
             kAt30);

    checkNear(p.evaluate(0.0, kAt30).x(), 10.0, "before the first key holds the first value");
    checkNear(p.evaluate(9.0, kAt30).x(), 20.0, "after the last key holds the last value");
    checkNear(p.evaluate(1.5, kAt30).x(), 15.0, "linear midpoint");
}

void hold_interpolation_steps() {
    Property p;
    p.addKey({TimeValue::seconds(0.0), Value::scalar(0.0), Interpolation::Hold, 0, 0, 0},
             kAt30);
    p.addKey({TimeValue::seconds(1.0), Value::scalar(100.0), Interpolation::Hold, 0, 0, 0},
             kAt30);

    checkNear(p.evaluate(0.99, kAt30).x(), 0.0, "hold does not interpolate");
    checkNear(p.evaluate(1.0, kAt30).x(), 100.0, "hold steps at the next key");
}

void keys_stay_sorted_and_replace_in_place() {
    Property p;
    p.addKey({TimeValue::seconds(2.0), Value::scalar(2.0), Interpolation::Linear, 0, 0, 0},
             kAt30);
    p.addKey({TimeValue::seconds(0.5), Value::scalar(0.5), Interpolation::Linear, 0, 0, 0},
             kAt30);
    p.addKey({TimeValue::seconds(1.0), Value::scalar(1.0), Interpolation::Linear, 0, 0, 0},
             kAt30);

    check(p.keys.size() == 3, "three distinct times give three keys");
    checkNear(p.keys[0].value.x(), 0.5, "sorted by time regardless of insertion order");
    checkNear(p.keys[1].value.x(), 1.0, "sorted, middle");
    checkNear(p.keys[2].value.x(), 2.0, "sorted, last");

    // Setting a key where one already exists overwrites rather than duplicating,
    // which is what the timeline's keyframe navigator does.
    p.addKey({TimeValue::seconds(1.0), Value::scalar(99.0), Interpolation::Linear, 0, 0, 0},
             kAt30);
    check(p.keys.size() == 3, "re-keying at an existing time does not duplicate");
    checkNear(p.keys[1].value.x(), 99.0, "re-keying replaces the value");
}

// The D3 payoff. The same keyframes, authored in beats, produce a faster animation
// on a faster song without anyone editing anything.
void beat_authored_keys_retime_with_tempo() {
    Property p;
    p.addKey({TimeValue::beats(0.0), Value::scalar(0.0), Interpolation::Linear, 0, 0, 0},
             kAt30);
    p.addKey({TimeValue::beats(4.0), Value::scalar(100.0), Interpolation::Linear, 0, 0, 0},
             kAt30);

    const TimeContext slow{30.0, 90.0, true};   // 4 beats = 2.667s
    const TimeContext fast{30.0, 174.0, true};  // 4 beats = 1.379s

    checkNear(p.evaluate(2.667, slow).x(), 100.0, "one bar at 90bpm is finished by 2.667s",
              0.05);
    check(p.evaluate(2.667, fast).x() >= 100.0, "same bar at 174bpm finished much earlier");
    checkNear(p.evaluate(1.379, fast).x(), 100.0, "one bar at 174bpm is finished by 1.379s",
              0.05);
    check(p.evaluate(1.379, slow).x() < 60.0, "at 90bpm that bar is only half done");
}

void lerp_respects_component_count() {
    const Value a = Value::vec2(0.0, 10.0);
    const Value b = Value::vec2(100.0, 20.0);
    const Value mid = lerp(a, b, 0.5);
    check(mid.count == 2, "lerp keeps the component count");
    checkNear(mid.c[0], 50.0, "lerp component 0");
    checkNear(mid.c[1], 15.0, "lerp component 1");
}

}  // namespace

int main() {
    easing_pins_both_endpoints();
    linear_ease_is_close_to_identity();
    ease_out_holds_near_the_start();
    overshoot_goes_past_one_then_settles();
    unanimated_property_returns_its_static_value();
    evaluation_clamps_outside_the_keyed_range();
    hold_interpolation_steps();
    keys_stay_sorted_and_replace_in_place();
    beat_authored_keys_retime_with_tempo();
    lerp_respects_component_count();

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("animation: all checks passed");
    return EXIT_SUCCESS;
}
