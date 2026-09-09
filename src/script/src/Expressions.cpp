// The After Effects compatible surface: what an expression can actually say.
//
// The names and behaviours here are AE's, on purpose. This audience does not write
// expressions from scratch, they paste them from tutorials and Discord, and a `wiggle`
// that takes its arguments in a different order is worse than no wiggle at all.

#include "SandboxImpl.h"

#include <cmath>
#include <cstring>

namespace ruby::script {
namespace {

Inputs* inputsOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ruby.inputs");
    auto* in = static_cast<Inputs*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return in;
}

// --- Deterministic noise -----------------------------------------------------
//
// The whole reason math.random is absent. A render has to be reproducible: the same
// project on the same frame must give the same pixels tomorrow, on another machine, and
// on a render farm. So the "randomness" is a pure function of the seed and the time, and
// nothing else.

std::uint64_t mix(std::uint64_t x) {
    // splitmix64. Cheap, and scatters adjacent seeds well, which matters because
    // consecutive layer ids are the common case and must not wiggle in sympathy.
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// A value in [-1, 1] at integer step `n`.
double noiseAt(std::uint64_t seed, std::int64_t n) {
    const std::uint64_t h = mix(seed ^ mix(static_cast<std::uint64_t>(n)));
    return (static_cast<double>(h >> 11) * 0x1.0p-53) * 2.0 - 1.0;
}

// Smooth value noise. Smoothstep between integer samples rather than linear, because
// linear interpolation gives a visible kink at every whole step, and a wiggle that ticks
// is a wiggle that looks like a bug.
double noise(std::uint64_t seed, double t) {
    const double floored = std::floor(t);
    const double frac = t - floored;
    const auto n = static_cast<std::int64_t>(floored);
    const double a = noiseAt(seed, n);
    const double b = noiseAt(seed, n + 1);
    const double s = frac * frac * (3.0 - 2.0 * frac);
    return a + (b - a) * s;
}

// --- Vectors -----------------------------------------------------------------

core::Value* checkVec(lua_State* L, int index) {
    return static_cast<core::Value*>(luaL_testudata(L, index, kVecMeta));
}

// Reads either a vector or a plain number as a value. Lets every operator below accept
// `v * 2` as readily as `v * other`, which is what AE expressions assume.
bool coerce(lua_State* L, int index, core::Value& out) {
    if (const core::Value* v = checkVec(L, index); v != nullptr) {
        out = *v;
        return true;
    }
    if (lua_isnumber(L, index)) {
        out = core::Value::scalar(lua_tonumber(L, index));
        return true;
    }
    return false;
}

void pushVec(lua_State* L, const core::Value& value) {
    auto* made = static_cast<core::Value*>(lua_newuserdatauv(L, sizeof(core::Value), 0));
    *made = value;
    luaL_setmetatable(L, kVecMeta);
}

// Applies `op` component by component, broadcasting a scalar across all components. The
// result keeps the wider of the two counts so `vec2 + 5` is still a vec2.
int arithmetic(lua_State* L, double (*op)(double, double)) {
    core::Value a;
    core::Value b;
    if (!coerce(L, 1, a) || !coerce(L, 2, b)) {
        return luaL_error(L, "expected a number or a vector");
    }
    core::Value out;
    out.count = a.count > b.count ? a.count : b.count;
    for (int i = 0; i < out.count; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        const double left = a.count == 1 ? a.c[0] : (i < a.count ? a.c[slot] : 0.0);
        const double right = b.count == 1 ? b.c[0] : (i < b.count ? b.c[slot] : 0.0);
        out.c[slot] = op(left, right);
    }
    if (out.count == 1) {
        lua_pushnumber(L, out.c[0]);
    } else {
        pushVec(L, out);
    }
    return 1;
}

int vecAdd(lua_State* L) { return arithmetic(L, [](double x, double y) { return x + y; }); }
int vecSub(lua_State* L) { return arithmetic(L, [](double x, double y) { return x - y; }); }
int vecMul(lua_State* L) { return arithmetic(L, [](double x, double y) { return x * y; }); }
int vecDiv(lua_State* L) { return arithmetic(L, [](double x, double y) { return x / y; }); }

int vecUnm(lua_State* L) {
    core::Value v;
    if (!coerce(L, 1, v)) {
        return luaL_error(L, "expected a vector");
    }
    for (int i = 0; i < v.count; ++i) {
        v.c[static_cast<std::size_t>(i)] = -v.c[static_cast<std::size_t>(i)];
    }
    pushVec(L, v);
    return 1;
}

// Indexed from 1, like everything else in Lua. AE indexes from 0; the paste shim is where
// that gets reconciled, because doing it here would make every hand-written expression
// disagree with the rest of the language.
int vecIndex(lua_State* L) {
    const core::Value* v = checkVec(L, 1);
    if (v == nullptr) {
        return luaL_error(L, "not a vector");
    }
    if (lua_isnumber(L, 2)) {
        const auto i = static_cast<int>(lua_tointeger(L, 2));
        if (i < 1 || i > v->count) {
            return luaL_error(L, "vector index %d is out of range", i);
        }
        lua_pushnumber(L, v->c[static_cast<std::size_t>(i - 1)]);
        return 1;
    }
    // Named access, because `position.x` reads better than `position[1]` and costs
    // nothing to allow.
    if (const char* key = lua_tostring(L, 2); key != nullptr) {
        static const char* kNames = "xyzw";
        if (const char* at = std::strchr(kNames, key[0]);
            at != nullptr && key[1] == '\0') {
            const auto i = static_cast<int>(at - kNames);
            if (i < v->count) {
                lua_pushnumber(L, v->c[static_cast<std::size_t>(i)]);
                return 1;
            }
        }
    }
    lua_pushnil(L);
    return 1;
}

int vecLen(lua_State* L) {
    const core::Value* v = checkVec(L, 1);
    lua_pushinteger(L, v != nullptr ? v->count : 0);
    return 1;
}

int vecEq(lua_State* L) {
    const core::Value* a = checkVec(L, 1);
    const core::Value* b = checkVec(L, 2);
    bool same = a != nullptr && b != nullptr && a->count == b->count;
    for (int i = 0; same && i < a->count; ++i) {
        same = a->c[static_cast<std::size_t>(i)] == b->c[static_cast<std::size_t>(i)];
    }
    lua_pushboolean(L, same);
    return 1;
}

int vecToString(lua_State* L) {
    const core::Value* v = checkVec(L, 1);
    if (v == nullptr) {
        lua_pushstring(L, "vec(?)");
        return 1;
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, "vec(");
    for (int i = 0; i < v->count; ++i) {
        if (i > 0) {
            luaL_addstring(&b, ", ");
        }
        lua_pushfstring(L, "%f", v->c[static_cast<std::size_t>(i)]);
        luaL_addvalue(&b);
    }
    luaL_addstring(&b, ")");
    luaL_pushresult(&b);
    return 1;
}

int makeVec(lua_State* L) {
    const int n = lua_gettop(L);
    if (n < 2 || n > 4) {
        return luaL_error(L, "vec takes 2, 3 or 4 numbers");
    }
    core::Value v;
    v.count = n;
    for (int i = 0; i < n; ++i) {
        v.c[static_cast<std::size_t>(i)] = luaL_checknumber(L, i + 1);
    }
    pushVec(L, v);
    return 1;
}

// --- The expression functions ------------------------------------------------

// wiggle(freq, amp [, octaves [, ampMult [, t]]])
//
// AE's argument order and defaults. Applied to `value`, per component, each component on
// its own seed so x and y move independently rather than in lockstep.
int wiggle(lua_State* L) {
    Inputs* in = inputsOf(L);
    if (in == nullptr) {
        return luaL_error(L, "wiggle is not available here");
    }
    const double freq = luaL_checknumber(L, 1);
    const double amp = luaL_checknumber(L, 2);
    const int octaves = static_cast<int>(luaL_optinteger(L, 3, 1));
    const double ampMult = luaL_optnumber(L, 4, 0.5);
    const double t = luaL_optnumber(L, 5, in->time);

    core::Value out = in->value;
    for (int component = 0; component < out.count; ++component) {
        double offset = 0.0;
        double thisAmp = amp;
        double thisFreq = freq;
        for (int octave = 0; octave < (octaves > 0 ? octaves : 1) && octave < 16; ++octave) {
            const std::uint64_t seed =
                mix(in->seed ^ (static_cast<std::uint64_t>(component) * 0x9E3779B9ULL) ^
                    (static_cast<std::uint64_t>(octave) * 0x85EBCA6BULL));
            offset += noise(seed, t * thisFreq) * thisAmp;
            thisAmp *= ampMult;
            thisFreq *= 2.0;
        }
        out.c[static_cast<std::size_t>(component)] += offset;
    }
    pushValue(L, out);
    return 1;
}

double linearMap(double t, double tMin, double tMax, double a, double b) {
    if (tMax == tMin) {
        return t <= tMin ? a : b;
    }
    const double f = (t - tMin) / (tMax - tMin);
    // Clamped at both ends, as AE's linear() is. An unclamped remap keeps travelling past
    // its endpoints, which is almost never what someone animating a fade wants.
    if (f <= 0.0) return a;
    if (f >= 1.0) return b;
    return a + (b - a) * f;
}

int linear(lua_State* L) {
    const double t = luaL_checknumber(L, 1);
    const double tMin = luaL_checknumber(L, 2);
    const double tMax = luaL_checknumber(L, 3);

    core::Value a;
    core::Value b;
    if (!coerce(L, 4, a) || !coerce(L, 5, b)) {
        return luaL_error(L, "linear expects numbers or vectors for the last two arguments");
    }
    core::Value out;
    out.count = a.count > b.count ? a.count : b.count;
    for (int i = 0; i < out.count; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        out.c[slot] = linearMap(t, tMin, tMax, a.count == 1 ? a.c[0] : a.c[slot],
                                b.count == 1 ? b.c[0] : b.c[slot]);
    }
    pushValue(L, out);
    return 1;
}

int ease(lua_State* L) {
    const double t = luaL_checknumber(L, 1);
    const double tMin = luaL_checknumber(L, 2);
    const double tMax = luaL_checknumber(L, 3);
    core::Value a;
    core::Value b;
    if (!coerce(L, 4, a) || !coerce(L, 5, b)) {
        return luaL_error(L, "ease expects numbers or vectors for the last two arguments");
    }
    const double raw = (tMax == tMin) ? (t <= tMin ? 0.0 : 1.0) : (t - tMin) / (tMax - tMin);
    const double f = raw <= 0.0 ? 0.0 : raw >= 1.0 ? 1.0 : raw * raw * (3.0 - 2.0 * raw);

    core::Value out;
    out.count = a.count > b.count ? a.count : b.count;
    for (int i = 0; i < out.count; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        const double from = a.count == 1 ? a.c[0] : a.c[slot];
        const double to = b.count == 1 ? b.c[0] : b.c[slot];
        out.c[slot] = from + (to - from) * f;
    }
    pushValue(L, out);
    return 1;
}

int clampFn(lua_State* L) {
    const double v = luaL_checknumber(L, 1);
    const double lo = luaL_checknumber(L, 2);
    const double hi = luaL_checknumber(L, 3);
    lua_pushnumber(L, v < lo ? lo : (v > hi ? hi : v));
    return 1;
}

int degreesToRadians(lua_State* L) {
    lua_pushnumber(L, luaL_checknumber(L, 1) * 3.14159265358979323846 / 180.0);
    return 1;
}

int radiansToDegrees(lua_State* L) {
    lua_pushnumber(L, luaL_checknumber(L, 1) * 180.0 / 3.14159265358979323846);
    return 1;
}

int length(lua_State* L) {
    core::Value v;
    if (!coerce(L, 1, v)) {
        return luaL_error(L, "length expects a number or a vector");
    }
    double sum = 0.0;
    for (int i = 0; i < v.count; ++i) {
        const double c = v.c[static_cast<std::size_t>(i)];
        sum += c * c;
    }
    lua_pushnumber(L, std::sqrt(sum));
    return 1;
}

}  // namespace

void pushValue(lua_State* L, const core::Value& value) {
    if (value.count <= 1) {
        lua_pushnumber(L, value.c[0]);
    } else {
        pushVec(L, value);
    }
}

bool readValue(lua_State* L, int index, core::Value& out) {
    if (const core::Value* v = checkVec(L, index); v != nullptr) {
        out = *v;
        return true;
    }
    if (lua_isnumber(L, index)) {
        out = core::Value::scalar(lua_tonumber(L, index));
        return true;
    }
    // A plain table is still accepted: somebody will write `{50, 80}` before they find
    // vec(), and refusing it would be pedantry rather than safety.
    if (lua_istable(L, index)) {
        const auto n = static_cast<int>(luaL_len(L, index));
        if (n < 2 || n > 4) {
            return false;
        }
        core::Value v;
        v.count = n;
        for (int i = 0; i < n; ++i) {
            lua_rawgeti(L, index, i + 1);
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                return false;
            }
            v.c[static_cast<std::size_t>(i)] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        out = v;
        return true;
    }
    return false;
}

void installExpressionLibrary(lua_State* L) {
    luaL_newmetatable(L, kVecMeta);
    static const luaL_Reg kVecMethods[] = {
        {"__add", vecAdd},   {"__sub", vecSub}, {"__mul", vecMul},
        {"__div", vecDiv},   {"__unm", vecUnm}, {"__index", vecIndex},
        {"__len", vecLen},   {"__eq", vecEq},   {"__tostring", vecToString},
        {nullptr, nullptr},
    };
    luaL_setfuncs(L, kVecMethods, 0);

    // Hide the metatable. Without this a script can reach it with `getmetatable(value)`
    // and overwrite `__index`, and every expression evaluated afterwards in this sandbox
    // reads vectors through whatever it replaced it with. One preset quietly breaking
    // every other expression in the project is a worse failure than anything the
    // filesystem guards prevent, because it produces wrong output rather than no output.
    //
    // Setting __metatable makes getmetatable return this string instead and makes
    // setmetatable refuse outright.
    lua_pushliteral(L, "vector");
    lua_setfield(L, -2, "__metatable");

    lua_pop(L, 1);

    static const luaL_Reg kGlobals[] = {
        {"vec", makeVec},
        {"wiggle", wiggle},
        {"linear", linear},
        {"ease", ease},
        {"clamp", clampFn},
        {"degreesToRadians", degreesToRadians},
        {"radiansToDegrees", radiansToDegrees},
        {"length", length},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* fn = kGlobals; fn->func != nullptr; ++fn) {
        lua_pushcfunction(L, fn->func);
        lua_setglobal(L, fn->name);
    }
}

}  // namespace ruby::script
