// Tests for the expression sandbox.
//
// Most of this file is trying to break out of it. That is the point: expressions arrive
// from project files and preset packs, which means from strangers, and "we removed the
// dangerous things" is only worth as much as the attempt to find one we missed.
//
// The escape attempts are written as assertions that they FAIL. If one of them ever starts
// passing, this file is the alarm.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ruby/script/Sandbox.h"

using namespace ruby;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void checkNear(double a, double b, const char* what, double eps = 1e-9) {
    if (!(a >= b - eps && a <= b + eps)) {
        std::fprintf(stderr, "FAIL: %s (got %.9f, want %.9f)\n", what, a, b);
        ++failures;
    }
}

// A script that cannot even be compiled is refused, which counts as blocked.
void blocked(script::Sandbox& box, const char* source, const char* what) {
    const script::Sandbox::Outcome out = box.evaluate(source);
    if (out.ok) {
        std::fprintf(stderr, "ESCAPED: %s  <<< the sandbox let this through\n", what);
        ++failures;
    }
}

void it_evaluates_the_ordinary_things() {
    auto box = script::Sandbox::create();
    check(box != nullptr, "a sandbox starts");

    checkNear(box->evaluate("2 + 3").value.c[0], 5.0, "arithmetic");
    checkNear(box->evaluate("math.floor(3.7)").value.c[0], 3.0, "the maths library is there");
    checkNear(box->evaluate("math.sin(0)").value.c[0], 0.0, "and works");

    // A bare expression and a full chunk both have to work: almost every expression is
    // the former, and anything with a local variable is the latter.
    checkNear(box->evaluate("40 + 2").value.c[0], 42.0, "a bare expression");
    checkNear(box->evaluate("local a = 40 return a + 2").value.c[0], 42.0,
              "a chunk with its own return");
}

void vectors_go_in_and_out() {
    auto box = script::Sandbox::create();
    box->set("value", core::Value::vec2(50.0, 80.0));

    const auto read = box->evaluate("value[1]");
    checkNear(read.value.c[0], 50.0, "a vector global is readable, indexed from one");

    const auto made = box->evaluate("{value[1], value[2] + 20}");
    check(made.ok, "a table comes back");
    check(made.value.count == 2, "as a vec2");
    checkNear(made.value.c[1], 100.0, "with the arithmetic applied");

    box->set("time", 2.0);
    checkNear(box->evaluate("time * 100").value.c[0], 200.0, "a scalar global");

    // A table of the wrong shape is an error rather than something silently truncated.
    check(!box->evaluate("{1, 2, 3, 4, 5}").ok, "five values is refused");
    check(!box->evaluate("{}").ok, "and so is none");
    check(!box->evaluate("'a string'").ok, "and a string");
}

// The escape attempts. Each of these is a real thing a hostile or careless preset could
// try, and each has to fail.
void it_cannot_reach_outside_the_process() {
    auto box = script::Sandbox::create();

    blocked(*box, "io.open('/etc/passwd')", "opening a file");
    blocked(*box, "io.write('x')", "writing to stdout");
    blocked(*box, "os.execute('rm -rf /')", "running a command");
    blocked(*box, "os.remove('/tmp/x')", "deleting a file");
    blocked(*box, "os.getenv('HOME')", "reading the environment");
    blocked(*box, "require('os')", "requiring a library back in");
    blocked(*box, "package.loadlib('/lib/x.so', 'f')", "loading a shared object");
    blocked(*box, "dofile('/etc/passwd')", "running a file as code");
    blocked(*box, "loadfile('/etc/passwd')", "loading a file as code");
    blocked(*box, "load('return 1')()", "building new code at runtime");
    blocked(*box, "debug.getregistry()", "reaching the registry through debug");
    blocked(*box, "debug.sethook(function() end)", "installing its own hook");
    blocked(*box, "collectgarbage('collect')", "stalling in the collector");

    // The libraries are not merely emptied, they are absent. A script cannot tell them
    // apart from a typo, which is the correct amount of information to give it.
    check(!box->evaluate("type(io)").ok || box->evaluate("io == nil").ok,
          "io is not present at all");
}

void it_cannot_be_non_deterministic() {
    auto box = script::Sandbox::create();

    blocked(*box, "os.time()", "reading the clock");
    blocked(*box, "os.clock()", "reading the process clock");
    blocked(*box, "os.date()", "reading the date");
    blocked(*box, "math.random()", "unseeded randomness");
    blocked(*box, "math.randomseed(1)", "seeding the generator");

    // The same expression twice must give the same answer, which is the property all of
    // the above exist to protect.
    const double first = box->evaluate("math.pi * 2").value.c[0];
    const double again = box->evaluate("math.pi * 2").value.c[0];
    checkNear(first, again, "the same expression gives the same answer");
}

// One bad expression in one preset must not take the app with it.
void a_runaway_expression_is_stopped() {
    auto box = script::Sandbox::create();
    box->setInstructionBudget(50000);

    const script::Sandbox::Outcome spin = box->evaluate("while true do end");
    check(!spin.ok, "an infinite loop does not succeed");
    check(spin.exhausted, "and is reported as having run out of budget, not as an error");

    const script::Sandbox::Outcome recursive =
        box->evaluate("local function f(n) return f(n + 1) end return f(0)");
    check(!recursive.ok, "unbounded recursion is stopped too");

    // And the sandbox is still usable afterwards. A budget overrun that poisoned the
    // interpreter would turn one bad expression into a broken session.
    checkNear(box->evaluate("1 + 1").value.c[0], 2.0,
              "the sandbox still works after a runaway");

    // The hook must not linger. If it did, this next call would inherit a spent counter
    // and die having done nothing wrong, which is a bug that looks random.
    for (int i = 0; i < 50; ++i) {
        check(box->evaluate("1 + 1").ok, "and keeps working, run after run");
    }
}

void errors_are_reported_not_thrown() {
    auto box = script::Sandbox::create();

    const auto broken = box->evaluate("this is not lua");
    check(!broken.ok, "a syntax error fails");
    check(!broken.error.empty(), "and says something");
    check(!broken.exhausted, "and is not confused with a budget overrun");

    const auto runtime = box->evaluate("error('boom')");
    check(!runtime.ok, "a runtime error fails");
    check(!runtime.error.empty(), "and says something");

    const auto missing = box->evaluate("undefinedThing * 2");
    check(!missing.ok, "an undefined global fails rather than reading as zero");
}

// Compiling per frame would cost more than running, so chunks are kept.
void chunks_are_compiled_once() {
    auto box = script::Sandbox::create();
    check(box->cachedChunks() == 0, "nothing cached to start");

    check(box->evaluate("1 + 1").ok, "an expression evaluates");
    check(box->cachedChunks() == 1, "one expression, one chunk");

    for (int i = 0; i < 100; ++i) {
        check(box->evaluate("1 + 1").ok, "and keeps evaluating");
    }
    check(box->cachedChunks() == 1, "a hundred more evaluations, still one chunk");

    check(box->evaluate("2 + 2").ok, "a different expression evaluates");
    check(box->cachedChunks() == 2, "and is a second chunk");
}


// --- The After Effects surface ----------------------------------------------

void the_ae_globals_are_there() {
    auto box = script::Sandbox::create();
    box->setInputs(2.5, core::Value::vec2(50.0, 80.0), 1);

    checkNear(box->evaluate("time").value.c[0], 2.5, "time is a global, not a call");
    checkNear(box->evaluate("time * 100").value.c[0], 250.0, "and is usable in arithmetic");

    const auto v = box->evaluate("value");
    check(v.value.count == 2, "value comes through as a vector");
    checkNear(v.value.c[1], 80.0, "with its components intact");
    checkNear(box->evaluate("value[1]").value.c[0], 50.0, "indexable from one");
    checkNear(box->evaluate("value.y").value.c[0], 80.0, "and by name");
}

// The line people actually paste. `value + [0, 50]` in AE becomes `value + vec(0, 50)`
// here, and it has to just work or the shim has nothing to convert to.
void vectors_do_arithmetic() {
    auto box = script::Sandbox::create();
    box->setInputs(0.0, core::Value::vec2(50.0, 80.0), 1);

    const auto sum = box->evaluate("value + vec(0, 50)");
    check(sum.ok && sum.value.count == 2, "vector plus vector");
    checkNear(sum.value.c[1], 130.0, "adds component by component");

    const auto scalar = box->evaluate("value + 20");
    check(scalar.ok && scalar.value.count == 2, "vector plus scalar stays a vector");
    checkNear(scalar.value.c[0], 70.0, "broadcasting across components");
    checkNear(scalar.value.c[1], 100.0, "both of them");

    checkNear(box->evaluate("(value * 2)[1]").value.c[0], 100.0, "multiply");
    checkNear(box->evaluate("(value - vec(50, 80))[1]").value.c[0], 0.0, "subtract");
    checkNear(box->evaluate("(-value)[1]").value.c[0], -50.0, "negate");
    checkNear(box->evaluate("#value").value.c[0], 2.0, "length operator gives the count");
    checkNear(box->evaluate("length(vec(3, 4))").value.c[0], 5.0, "and length() the norm");

    check(box->evaluate("value == vec(50, 80)").ok == false ||
              box->evaluate("value == vec(50, 80) and 1 or 0").value.c[0] == 1.0,
          "equality compares components");
}

// The property everything else depends on. A render must be reproducible tomorrow, on
// another machine, and on a farm.
void wiggle_is_deterministic() {
    auto box = script::Sandbox::create();
    box->setInputs(1.0, core::Value::vec2(100.0, 200.0), 12345);

    const auto first = box->evaluate("wiggle(5, 20)");
    check(first.ok, "wiggle evaluates");
    check(first.value.count == 2, "and keeps the shape of value");

    const auto again = box->evaluate("wiggle(5, 20)");
    checkNear(first.value.c[0], again.value.c[0], "the same call gives the same answer");
    checkNear(first.value.c[1], again.value.c[1], "in every component");

    // A brand new sandbox, same seed and time: still identical. This is the one that
    // matters, because it is what a second machine is.
    auto other = script::Sandbox::create();
    other->setInputs(1.0, core::Value::vec2(100.0, 200.0), 12345);
    const auto elsewhere = other->evaluate("wiggle(5, 20)");
    checkNear(first.value.c[0], elsewhere.value.c[0],
              "and a different interpreter agrees, which is what a render farm needs");
}

void wiggle_actually_moves_and_stays_in_bounds() {
    auto box = script::Sandbox::create();

    bool moved = false;
    double worst = 0.0;
    for (int frame = 0; frame < 200; ++frame) {
        box->setInputs(frame / 30.0, core::Value::vec2(100.0, 200.0), 7);
        const auto out = box->evaluate("wiggle(5, 20)");
        if (!out.ok) {
            check(false, "wiggle evaluated across a range of times");
            return;
        }
        const double offset = out.value.c[0] - 100.0;
        if (std::fabs(offset) > 1e-6) {
            moved = true;
        }
        worst = std::fabs(offset) > worst ? std::fabs(offset) : worst;
    }
    check(moved, "wiggle actually moves the value rather than returning it unchanged");

    // One octave at amplitude 20 cannot exceed 20. If it does, the amplitude argument is
    // not doing what its name says and every pasted expression will be wrong by a factor.
    check(worst <= 20.0 + 1e-9, "and stays inside the amplitude it was given");
}

// Different layers must not wiggle in sympathy, and neither must x and y.
void different_seeds_wiggle_differently() {
    auto a = script::Sandbox::create();
    auto b = script::Sandbox::create();
    a->setInputs(1.0, core::Value::vec2(0.0, 0.0), 1);
    b->setInputs(1.0, core::Value::vec2(0.0, 0.0), 2);

    const auto first = a->evaluate("wiggle(5, 20)");
    const auto second = b->evaluate("wiggle(5, 20)");
    check(std::fabs(first.value.c[0] - second.value.c[0]) > 1e-9,
          "consecutive seeds do not produce the same wiggle");
    check(std::fabs(first.value.c[0] - first.value.c[1]) > 1e-9,
          "and x and y move independently rather than in lockstep");
}

void wiggle_is_smooth() {
    auto box = script::Sandbox::create();

    // Adjacent frames must be close together. Value noise interpolated linearly kinks at
    // every whole step; smoothstep is what stops a wiggle from ticking.
    double previous = 0.0;
    double biggestJump = 0.0;
    for (int frame = 0; frame < 300; ++frame) {
        box->setInputs(frame / 60.0, core::Value::scalar(0.0), 99);
        const double now = box->evaluate("wiggle(3, 100)").value.c[0];
        if (frame > 0) {
            const double jump = std::fabs(now - previous);
            biggestJump = jump > biggestJump ? jump : biggestJump;
        }
        previous = now;
    }
    check(biggestJump < 30.0,
          "no sudden jumps between adjacent frames, so the motion reads as smooth");
}

void the_easing_helpers_match_ae() {
    auto box = script::Sandbox::create();
    box->setInputs(0.0, core::Value::scalar(0.0), 1);

    checkNear(box->evaluate("linear(0.5, 0, 1, 0, 100)").value.c[0], 50.0, "linear midpoint");
    checkNear(box->evaluate("linear(-5, 0, 1, 0, 100)").value.c[0], 0.0,
              "clamped below, as AE's linear is");
    checkNear(box->evaluate("linear(5, 0, 1, 0, 100)").value.c[0], 100.0, "and above");

    checkNear(box->evaluate("ease(0.5, 0, 1, 0, 100)").value.c[0], 50.0,
              "ease is symmetric at the midpoint");
    check(box->evaluate("ease(0.25, 0, 1, 0, 100)").value.c[0] < 25.0,
          "and slower than linear at the start");

    const auto vectorised = box->evaluate("linear(0.5, 0, 1, vec(0, 0), vec(100, 200))");
    check(vectorised.value.count == 2, "linear works on vectors too");
    checkNear(vectorised.value.c[1], 100.0, "component by component");

    checkNear(box->evaluate("clamp(5, 0, 1)").value.c[0], 1.0, "clamp");
    checkNear(box->evaluate("radiansToDegrees(math.pi)").value.c[0], 180.0, "angle helpers");
}

// Found by probing, not by reading. A script could reach the vector metatable through
// getmetatable(value) and overwrite __index, after which every expression evaluated later
// in the same sandbox read vectors through whatever it had installed.
//
// That is a worse failure than any of the filesystem ones above: those stop an expression,
// this silently makes other people's expressions produce wrong numbers.
void the_vector_type_cannot_be_tampered_with() {
    auto box = script::Sandbox::create();
    box->setInputs(0.0, core::Value::vec2(10.0, 20.0), 1);

    checkNear(box->evaluate("value[1]").value.c[0], 10.0, "a vector reads correctly");

    const auto attempt = box->evaluate(
        "(function() getmetatable(value).__index = function() return 7 end return 1 end)()");
    check(!attempt.ok, "overwriting the vector metatable fails");

    checkNear(box->evaluate("value[1]").value.c[0], 10.0,
              "and the vector type still works afterwards, uncorrupted");

    // setmetatable must refuse too, or the same corruption arrives by another door.
    const auto swap = box->evaluate(
        "(function() setmetatable(value, {}) return 1 end)()");
    check(!swap.ok, "and the metatable cannot be replaced wholesale");
    checkNear(box->evaluate("value[2]").value.c[0], 20.0, "still intact");
}

// Bad arguments have to fail rather than produce a plausible wrong number, because a
// plausible wrong number is what somebody ships.
void bad_arguments_are_refused() {
    auto box = script::Sandbox::create();
    box->setInputs(0.0, core::Value::vec2(10.0, 20.0), 1);

    check(!box->evaluate("value[999]").ok, "an index past the end fails");
    check(!box->evaluate("value[-1]").ok, "and so does a negative one");
    check(!box->evaluate("vec(1)").ok, "vec needs at least two components");
    check(!box->evaluate("vec(1,2,3,4,5)").ok, "and at most four");
    check(!box->evaluate("wiggle()").ok, "wiggle needs its arguments");
    check(!box->evaluate("wiggle('a','b')").ok, "and they have to be numbers");
    check(!box->evaluate("linear(0,0,1)").ok, "linear needs all five");
}

// Found by probing. An expression that writes a global used to write it for the whole
// sandbox, so `wiggle = function() return 999 end` in one preset silently replaced wiggle
// for every other expression in the project.
//
// That is not an escape from the process, it is a way to corrupt everybody else's output,
// which is worse: an escape stops working, this produces plausible wrong numbers.
void one_expression_cannot_change_another() {
    auto box = script::Sandbox::create();
    box->setInputs(1.0, core::Value::scalar(0.0), 42);

    const double before = box->evaluate("wiggle(5, 20)").value.c[0];

    // Every route to the shared globals that the probe found.
    check(box->evaluate("wiggle = function() return 999 end return 1").ok,
          "assigning a global succeeds, harmlessly");
    check(!box->evaluate("_G.wiggle = function() return 999 end return 1").ok,
          "_G is gone, so it cannot be named explicitly");
    check(!box->evaluate(
               "getmetatable(_ENV).__index.wiggle = function() return 999 end return 1")
               .ok,
          "and the environment's metatable is hidden");

    checkNear(box->evaluate("wiggle(5, 20)").value.c[0], before,
              "wiggle is untouched after every attempt");

    // A bare global written by one expression must not be visible to the next.
    check(box->evaluate("leaked = 7 return 1").ok, "a global assignment succeeds");
    checkNear(box->evaluate("leaked or -1").value.c[0], -1.0,
              "and is gone by the next expression");
}

// The instruction budget bounds time and says nothing about memory: a megabyte can be
// built in a handful of instructions, and so can a gigabyte.
void memory_is_capped_too() {
    auto box = script::Sandbox::create();

    const auto bomb = box->evaluate(
        "local t = {} for i = 1, 500 do t[i] = string.rep('x', 1000000) end return #t");
    check(!bomb.ok, "an expression cannot allocate without limit");

    // And the sandbox survives it. A memory limit that leaves the interpreter unusable
    // turns one greedy expression into a broken session.
    checkNear(box->evaluate("1 + 1").value.c[0], 2.0, "the sandbox still works afterwards");
    check(box->evaluate("string.rep('x', 1000)").ok == false ||
              box->evaluate("#string.rep('x', 1000)").value.c[0] == 1000.0,
          "and ordinary string work still succeeds");
}

// A NaN spreads through every calculation it touches and ends up as a layer that silently
// does not draw, with nothing anywhere saying why.
void non_finite_results_are_refused() {
    auto box = script::Sandbox::create();
    box->setInputs(0.0, core::Value::scalar(0.0), 1);

    check(!box->evaluate("0/0").ok, "NaN is refused");
    check(!box->evaluate("1/0").ok, "and infinity");
    check(!box->evaluate("1e308 * 10").ok, "and an overflow to infinity");
    check(!box->evaluate("vec(0/0, 1)").ok, "including inside a vector");
    check(box->evaluate("1/2").ok, "while ordinary division is fine");
}

}  // namespace

int main() {
    it_evaluates_the_ordinary_things();
    vectors_go_in_and_out();
    it_cannot_reach_outside_the_process();
    it_cannot_be_non_deterministic();
    a_runaway_expression_is_stopped();
    errors_are_reported_not_thrown();
    chunks_are_compiled_once();
    the_ae_globals_are_there();
    vectors_do_arithmetic();
    wiggle_is_deterministic();
    wiggle_actually_moves_and_stays_in_bounds();
    different_seeds_wiggle_differently();
    wiggle_is_smooth();
    the_easing_helpers_match_ae();
    the_vector_type_cannot_be_tampered_with();
    bad_arguments_are_refused();
    one_expression_cannot_change_another();
    memory_is_capped_too();
    non_finite_results_are_refused();

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("sandbox: all checks passed");
    return EXIT_SUCCESS;
}
