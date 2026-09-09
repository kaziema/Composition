#include "SandboxImpl.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace ruby::script {
namespace {

// Lua's allocator, with a ceiling.
//
// Refusing an allocation is how you say no to memory the way the count hook says no to
// time. Lua handles a null return from here properly: it raises a normal "not enough
// memory" error, which pcall catches like anything else.
void* cappedAlloc(void* ud, void* ptr, std::size_t osize, std::size_t nsize) {
    auto* impl = static_cast<Sandbox::Impl*>(ud);
    const std::size_t had = (ptr != nullptr) ? osize : 0;

    if (nsize == 0) {
        impl->memoryUsed -= had;
        std::free(ptr);
        return nullptr;
    }
    if (nsize > had && impl->memoryUsed + (nsize - had) > impl->memoryBudget) {
        impl->outOfMemory = true;
        return nullptr;
    }
    void* made = std::realloc(ptr, nsize);
    if (made != nullptr) {
        impl->memoryUsed = impl->memoryUsed - had + nsize;
    }
    return made;
}

Sandbox::Impl* implOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ruby.impl");
    auto* impl = static_cast<Sandbox::Impl*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return impl;
}

// Runs every `budget` instructions. There is no way to ask Lua "have you been too long";
// the count hook IS the mechanism, and erroring from inside it is how you stop a script
// that will not stop itself.
void countHook(lua_State* L, lua_Debug*) {
    if (Sandbox::Impl* impl = implOf(L); impl != nullptr) {
        impl->exhausted = true;
    }
    luaL_error(L, "expression ran too long");
}

// The libraries an expression may see.
//
// io, os, package, debug and coroutine are simply never opened. Not opened and then
// cleared: never opened. There is nothing to find and nothing to miss.
void openSafeLibraries(lua_State* L) {
    static const luaL_Reg kSafe[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* lib = kSafe; lib->func != nullptr; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }

    // The base library brings a few things that reach outside or defeat the point of the
    // rest, so they go. `load` and `dofile` would let a script build new code at runtime,
    // which makes every guarantee above conditional on what that code turns out to be.
    // `collectgarbage` lets a script stall the process without executing many
    // instructions, which walks around the budget rather than through it.
    //
    // `_G` goes too, and that one is subtle. Every run gets a fresh environment whose
    // reads fall through to the real globals, so a bare `wiggle = f` lands harmlessly in
    // the throwaway table. But `_G.wiggle = f` names the real table explicitly and walks
    // straight past that, clobbering wiggle for every other expression in the project.
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require",
                             "collectgarbage", "print", "rawequal", "rawlen",
                             "rawget", "rawset", "_G"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // Determinism. `math.random` without a seed differs run to run, and with a seed it
    // still differs from a render on another machine. Expressions get a seeded generator
    // built for the purpose later; until then there is none, which is honest.
    lua_getglobal(L, LUA_MATHLIBNAME);
    for (const char* name : {"random", "randomseed"}) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
}

}  // namespace

Sandbox::Impl::~Impl() {
    if (L != nullptr) {
        lua_close(L);
    }
}

// Compiles once and remembers it. Compiling per frame would cost more than running.
//
// Tries `return (source)` first so a bare expression like `wiggle(30, 10)` works, then
// falls back to the source as written so a multi-line script with its own `return` also
// works. That ordering matters: the wrapped form is what almost every expression is, and
// trying it second would mean every one of them compiles twice.
int Sandbox::Impl::chunkFor(const std::string& source, std::string* error) {
    if (const auto it = chunks.find(source); it != chunks.end()) {
        return it->second;
    }

    const std::string wrapped = "return (" + source + ")";
    if (luaL_loadbuffer(L, wrapped.c_str(), wrapped.size(), "=expression") != LUA_OK) {
        lua_pop(L, 1);  // the wrapped form's error is noise if the raw form compiles
        if (luaL_loadbuffer(L, source.c_str(), source.size(), "=expression") != LUA_OK) {
            if (error != nullptr) {
                *error = lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1)
                                                        : "could not compile";
            }
            lua_pop(L, 1);
            return LUA_NOREF;
        }
    }
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    chunks.emplace(source, ref);
    return ref;
}

Sandbox::Sandbox() : impl_(std::make_unique<Impl>()) {}
Sandbox::~Sandbox() = default;

std::unique_ptr<Sandbox> Sandbox::create() {
    std::unique_ptr<Sandbox> self(new Sandbox());
    self->impl_->L = lua_newstate(cappedAlloc, self->impl_.get());
    if (self->impl_->L == nullptr) {
        return nullptr;
    }
    lua_State* L = self->impl_->L;

    lua_pushlightuserdata(L, self->impl_.get());
    lua_setfield(L, LUA_REGISTRYINDEX, "ruby.impl");
    lua_pushlightuserdata(L, &self->impl_->inputs);
    lua_setfield(L, LUA_REGISTRYINDEX, "ruby.inputs");

    openSafeLibraries(L);
    installExpressionLibrary(L);
    return self;
}

void Sandbox::setInstructionBudget(int instructions) {
    impl_->budget = instructions > 0 ? instructions : 1;
}

void Sandbox::setInputs(double time, const core::Value& value, std::uint64_t seed) {
    impl_->inputs.time = time;
    impl_->inputs.value = value;
    impl_->inputs.seed = seed;

    // `time` and `value` are globals rather than function calls because that is what they
    // are in After Effects, and a pasted expression saying `value + 20` has to work.
    lua_pushnumber(impl_->L, time);
    lua_setglobal(impl_->L, "time");
    pushValue(impl_->L, value);
    lua_setglobal(impl_->L, "value");
}

void Sandbox::setProperty(const core::Property* prop, const core::TimeContext* ctx) {
    impl_->inputs.property = prop;
    impl_->inputs.ctx = ctx;
}

std::size_t Sandbox::cachedChunks() const noexcept { return impl_->chunks.size(); }

void Sandbox::set(const char* name, double value) {
    lua_pushnumber(impl_->L, value);
    lua_setglobal(impl_->L, name);
}

void Sandbox::set(const char* name, const core::Value& value) {
    lua_State* L = impl_->L;
    if (value.count <= 1) {
        lua_pushnumber(L, value.c[0]);
        lua_setglobal(L, name);
        return;
    }
    // A vector is a table indexed from 1, because that is what Lua code will expect. AE's
    // expressions index from 0; the paste shim is where that gets reconciled, not here.
    lua_createtable(L, value.count, 0);
    for (int i = 0; i < value.count; ++i) {
        lua_pushnumber(L, value.c[static_cast<std::size_t>(i)]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, name);
}

Sandbox::Outcome Sandbox::evaluate(const std::string& source) {
    Outcome out;
    lua_State* L = impl_->L;

    const int ref = impl_->chunkFor(source, &out.error);
    if (ref == LUA_NOREF) {
        return out;
    }

    impl_->exhausted = false;
    impl_->outOfMemory = false;
    lua_sethook(L, countHook, LUA_MASKCOUNT, impl_->budget);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

    // Every run gets a FRESH environment, and this is the most important line in the file.
    //
    // Without it, an expression that writes a global writes it for the whole sandbox:
    // `wiggle = function() return 999 end` in one preset silently replaces wiggle for
    // every other expression in the project. That is not a way out of the process, it is
    // a way to corrupt everybody else's output, which is worse.
    //
    // The fresh table falls through to the real globals for reads, so time, value, wiggle
    // and the rest are all visible. Writes land in the throwaway table and go with it.
    lua_newtable(L);                       // env
    lua_newtable(L);                       // metatable
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");

    // And hide this metatable, or `getmetatable(_ENV).__index` hands back the real global
    // table and everything above is undone by one line. Same fix as the vector type: the
    // pattern is that anything holding a reference to something shared needs it.
    lua_pushliteral(L, "environment");
    lua_setfield(L, -2, "__metatable");

    lua_setmetatable(L, -2);
    lua_setupvalue(L, -2, 1);              // _ENV is a main chunk's first upvalue

    const int status = lua_pcall(L, 0, 1, 0);

    // Always cleared, including on the error path. A hook left installed would fire during
    // the next evaluation with a stale count and kill an expression that had done nothing
    // wrong, which is the kind of bug that looks random.
    lua_sethook(L, nullptr, 0, 0);

    if (status != LUA_OK) {
        out.exhausted = impl_->exhausted || impl_->outOfMemory;
        out.error = lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "failed";
        lua_pop(L, 1);
        return out;
    }

    if (readValue(L, -1, out.value)) {
        // A non-finite result is refused rather than handed on. `0/0` is a NaN that
        // spreads through every calculation it touches and ends up as a layer that
        // silently does not draw, with nothing anywhere saying why. Falling back to the
        // keyframed value is both recoverable and visible.
        bool finite = true;
        for (int i = 0; i < out.value.count; ++i) {
            if (!std::isfinite(out.value.c[static_cast<std::size_t>(i)])) {
                finite = false;
            }
        }
        if (finite) {
            out.ok = true;
        } else {
            out.error = "expression produced a value that is not a finite number";
        }
    } else {
        out.error = "expression did not return a number or a vector";
    }
    lua_pop(L, 1);
    return out;
}

}  // namespace ruby::script
