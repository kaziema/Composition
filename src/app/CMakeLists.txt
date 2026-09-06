find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(composition
    main.cpp
)

target_link_libraries(composition
    PRIVATE
        comp::ui
        comp::core
        comp_warnings
)

# Puts libwebgpu_dawn.dylib beside the executable so it resolves at runtime.
target_copy_webgpu_binaries(composition)

set_target_properties(composition PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
