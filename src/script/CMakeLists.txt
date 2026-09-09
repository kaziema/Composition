add_library(ruby_script STATIC
    src/Sandbox.cpp
    src/Expressions.cpp
    src/LuaHost.cpp
    src/AeImport.cpp
)
add_library(ruby::script ALIAS ruby_script)

target_include_directories(ruby_script PUBLIC include)

# lua is PRIVATE: nothing outside this module should be able to reach a lua_State. The
# whole value of the sandbox is that it is the only door.
target_link_libraries(ruby_script
    PUBLIC ruby::core
    PRIVATE lua ruby_warnings
)
