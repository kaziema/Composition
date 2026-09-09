#pragma once

// Internal to the script module. The lua_State does not leave this directory: the whole
// value of the sandbox is that it is the only door, and a header that hands out the state
// would quietly become a second one.

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdint>
#include <map>
#include <string>

#include "ruby/core/Document.h"
#include "ruby/script/Sandbox.h"

namespace ruby::script {

// What an expression is being evaluated *for*. Set before each run.
struct Inputs {
    double time = 0.0;
    core::Value value;

    // Stable per layer and property, and constant across frames. This is what makes
    // wiggle deterministic: the same layer wiggles the same way every render, on every
    // machine, while two layers with identical expressions wiggle differently.
    std::uint64_t seed = 0;

    // Borrowed, valid only for the duration of one evaluate() call. loopOut and friends
    // read the keyframes through these; nothing holds on to them.
    const core::Property* property = nullptr;
    const core::TimeContext* ctx = nullptr;
};

class Sandbox::Impl {
public:
    lua_State* L = nullptr;
    int budget = 200000;
    bool exhausted = false;

    // Bytes Lua may hold. The instruction budget bounds TIME and does nothing about
    // memory: `('x'):rep(1000):gsub('x', ('y'):rep(1000))` builds a megabyte in a handful
    // of instructions, and the same line with bigger numbers builds a gigabyte.
    std::size_t memoryBudget = 64ULL * 1024 * 1024;
    std::size_t memoryUsed = 0;
    bool outOfMemory = false;
    Inputs inputs;
    std::map<std::string, int> chunks;  // source -> registry reference

    ~Impl();

    int chunkFor(const std::string& source, std::string* error);
};

// The name of the vector metatable in the registry. A table carrying it gets arithmetic,
// so `value + vec(0, 50)` works the way the same line does in After Effects.
inline constexpr const char* kVecMeta = "ruby.vec";

// Pushes a core::Value as a number or as a vector table with the metatable attached.
void pushValue(lua_State* L, const core::Value& value);

// Reads a number or vector table back off the stack. Returns false for anything else.
bool readValue(lua_State* L, int index, core::Value& out);

// Registers time, value, wiggle, the easing helpers and the vector type.
void installExpressionLibrary(lua_State* L);

}  // namespace ruby::script
