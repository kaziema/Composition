find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(composition
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(composition
    PRIVATE
        Qt6::Widgets
        comp::core
        comp::gpu
        comp_warnings
)

set_target_properties(composition PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
