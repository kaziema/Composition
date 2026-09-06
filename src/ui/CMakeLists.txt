find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Directory-scoped: enables AUTOMOC for this library's Q_OBJECT headers.
qt_standard_project_setup()

qt_add_library(comp_ui STATIC
    src/Theme.cpp
    src/PanelFrame.cpp
    src/EditorToolBar.cpp
    src/MainWindow.cpp
    include/comp/ui/Theme.h
    include/comp/ui/PanelFrame.h
    include/comp/ui/EditorToolBar.h
    include/comp/ui/MainWindow.h
)
add_library(comp::ui ALIAS comp_ui)

target_include_directories(comp_ui PUBLIC include)
target_link_libraries(comp_ui
    PUBLIC Qt6::Widgets
    PRIVATE comp::core comp::gpu comp_warnings
)
