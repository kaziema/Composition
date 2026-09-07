find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Directory-scoped: enables AUTOMOC for this library's Q_OBJECT headers.
qt_standard_project_setup()

qt_add_library(ruby_ui STATIC
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
    include/ruby/ui/Theme.h
    include/ruby/ui/Format.h
    include/ruby/ui/PanelFrame.h
    include/ruby/ui/EditorToolBar.h
    include/ruby/ui/ToolIcons.h
    include/ruby/ui/MainWindow.h
    include/ruby/ui/TimelineView.h
    include/ruby/ui/InspectorView.h
    include/ruby/ui/ProjectPanel.h
    include/ruby/ui/GpuViewport.h
    include/ruby/ui/Playback.h
    include/ruby/ui/DemoProject.h
)
add_library(ruby::ui ALIAS ruby_ui)

target_include_directories(ruby_ui PUBLIC include)
# core is PUBLIC: ruby_ui's own headers expose core::Project and core::Composition.
target_link_libraries(ruby_ui
    PUBLIC Qt6::Widgets ruby::core ruby::gpu ruby::engine ruby::media ruby::audio ruby::beat
    PRIVATE ruby_warnings
)
