find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(ruby
    main.cpp
)

target_link_libraries(ruby
    PRIVATE
        ruby::ui
        ruby::core
        ruby_warnings
)

# Puts libwebgpu_dawn.dylib beside the executable so it resolves at runtime.
target_copy_webgpu_binaries(ruby)

set_target_properties(ruby PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
