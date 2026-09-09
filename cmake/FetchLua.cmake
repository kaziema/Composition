# Lua 5.4, MIT.
#
# For expressions: `core::Property::expression` has existed and been serialised since the
# document model was written, and nothing has ever evaluated it. See NOTEBOOK F5 for why
# Lua rather than JavaScript, and for the four things that have to be true before this
# ships (sandbox, determinism, one state per thread, no allocation in the hot path).
#
# Lua ships no CMake of its own, so the source list is written out here. It is a short,
# stable list: it has barely changed across 5.4 point releases, and pinning the tag means
# it cannot change under us. lua.c and luac.c are deliberately absent, being the standalone
# interpreter and compiler rather than the library.
include(FetchContent)

FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
)
FetchContent_MakeAvailable(lua)

set(RUBY_LUA_SOURCES
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c
    ltm.c lundump.c lvm.c lzio.c
    lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c
    loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c
)
list(TRANSFORM RUBY_LUA_SOURCES PREPEND "${lua_SOURCE_DIR}/src/")

add_library(lua STATIC ${RUBY_LUA_SOURCES})

# SYSTEM, like miniaudio: our warning set is for our code. Lua is thirty years of very
# deliberate C and we are not going to fix its casts.
target_include_directories(lua SYSTEM PUBLIC "${lua_SOURCE_DIR}/src")

# The io and os libraries are compiled but never opened: the sandbox decides what a script
# can reach, and it does that by choosing which libraries to open rather than by trusting
# a blocklist. Compiling them costs a few kilobytes and keeps us on unmodified upstream
# source, which matters more, because a patched Lua is one nobody else can audit.
# One of these, never both: LUA_USE_MACOSX already implies LUA_USE_POSIX, and setting
# both makes Lua's own config header redefine the macro on every translation unit.
if(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()
