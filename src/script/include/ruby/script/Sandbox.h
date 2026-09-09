#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ruby/core/Animation.h"

namespace ruby::script {

// A Lua interpreter that expressions run inside.
//
// Expressions arrive from project files, from preset packs, and eventually from anywhere
// somebody downloads a pack. So this is not "Lua with a few things removed" — it is a
// deliberately small environment built by choosing what to put IN. A blocklist is a
// promise you have thought of everything; an allowlist is a promise you have thought of
// what you allowed, which is a much smaller claim and a much easier one to keep.
//
// Three things this guarantees, and they are the reason it exists rather than a bare
// lua_State:
//
//   1. No reach outside the process. No file system, no network, no subprocesses, no
//      loading more code at runtime.
//   2. No infinite loops. Every run has an instruction budget and is stopped when it is
//      spent, so one bad expression in one preset cannot hang the app.
//   3. No non-determinism. No clock, no unseeded randomness. The same project has to
//      render the same twice or an export cannot be trusted, and a render farm cannot
//      exist at all.
//
// NOT thread safe, deliberately. A lua_State belongs to one thread. When rendering goes
// parallel, each thread gets its own Sandbox rather than this growing a mutex, because a
// lock here would sit in the hot path of every animated property.
class Sandbox {
public:
    ~Sandbox();

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    // Null only if Lua itself fails to start, which in practice means out of memory.
    [[nodiscard]] static std::unique_ptr<Sandbox> create();

    struct Outcome {
        bool ok = false;
        core::Value value;
        std::string error;  // set when ok is false, phrased for a user to read

        // True when the run was stopped for exceeding its instruction budget, as opposed
        // to failing. Worth telling apart: a syntax error is the author's mistake to fix,
        // a budget overrun might just be an expensive expression.
        bool exhausted = false;
    };

    // Evaluates `source` and converts what it returns.
    //
    // A number becomes a scalar, and a table of two to four numbers becomes a vector, so
    // `[value[0], value[1] + 20]` written as `{value[1], value[2] + 20}` lands on a vec2.
    [[nodiscard]] Outcome evaluate(const std::string& source);

    // What the next expression is being evaluated for: the current time, the property's
    // own value, and a seed that must be stable per layer and property and constant
    // across frames.
    //
    // The seed is what makes wiggle deterministic. Two layers with identical expressions
    // wiggle differently because their seeds differ; the same layer wiggles identically
    // on every render, on every machine, because its seed does not.
    void setInputs(double time, const core::Value& value, std::uint64_t seed);

    // Any other global a script can read.
    void set(const char* name, double value);
    void set(const char* name, const core::Value& value);

    // How many Lua instructions a single evaluation may execute.
    //
    // Default 200,000: far more than any real expression needs, far less than a human
    // notices, and low enough that `while true do end` stops rather than hanging. An
    // expression runs per property per frame, so this is a ceiling, not a target.
    void setInstructionBudget(int instructions);

    // How many compiled chunks are being kept. Expressions are compiled once and reused,
    // because compiling per frame would dominate the cost of evaluating.
    [[nodiscard]] std::size_t cachedChunks() const noexcept;

    // Public so the module's own translation units can share it. Defined in an internal
    // header that never leaves src/, so the lua_State still has exactly one door.
    class Impl;

private:
    Sandbox();

    std::unique_ptr<Impl> impl_;
};

}  // namespace ruby::script
