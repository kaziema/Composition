find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Directory-scoped: enables AUTOMOC for this library's Q_OBJECT headers.
qt_standard_project_setup()

qt_add_library(comp_ui STATIC
    src/Theme.cpp
    src/Format.cpp
    src/PanelFrame.cpp
    src/EditorToolBar.cpp
    src/ToolIcons.cpp
    src/MainWindow.cpp
    src/TimelineView.cpp
    src/InspectorView.cpp
    src/ProjectPanel.cpp
    src/GpuViewport.cpp
    src/Playback.cpp
    src/DemoProject.cpp
    include/comp/ui/Theme.h
    include/comp/ui/Format.h
    include/comp/ui/PanelFrame.h
    include/comp/ui/EditorToolBar.h
    include/comp/ui/ToolIcons.h
    include/comp/ui/MainWindow.h
    include/comp/ui/TimelineView.h
    include/comp/ui/InspectorView.h
    include/comp/ui/ProjectPanel.h
    include/comp/ui/GpuViewport.h
    include/comp/ui/Playback.h
    include/comp/ui/DemoProject.h
)
add_library(comp::ui ALIAS comp_ui)

target_include_directories(comp_ui PUBLIC include)
# core is PUBLIC: comp_ui's own headers expose core::Project and core::Composition.
target_link_libraries(comp_ui
    PUBLIC Qt6::Widgets comp::core comp::gpu comp::engine comp::media comp::audio comp::beat
    PRIVATE comp_warnings
)
