find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Directory-scoped: enables AUTOMOC for this library's Q_OBJECT headers.
qt_standard_project_setup()

qt_add_library(comp_ui STATIC
    src/Theme.cpp
    src/PanelFrame.cpp
    src/EditorToolBar.cpp
    src/ToolIcons.cpp
    src/MainWindow.cpp
    src/TimelineView.cpp
    src/DemoProject.cpp
    include/comp/ui/Theme.h
    include/comp/ui/PanelFrame.h
    include/comp/ui/EditorToolBar.h
    include/comp/ui/ToolIcons.h
    include/comp/ui/MainWindow.h
    include/comp/ui/TimelineView.h
    include/comp/ui/DemoProject.h
)
add_library(comp::ui ALIAS comp_ui)

target_include_directories(comp_ui PUBLIC include)
# core is PUBLIC: comp_ui's own headers expose core::Project and core::Composition.
target_link_libraries(comp_ui
    PUBLIC Qt6::Widgets comp::core
    PRIVATE comp::gpu comp_warnings
)
