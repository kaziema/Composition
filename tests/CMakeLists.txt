function(comp_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE comp::core comp_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

comp_add_test(test_units)
comp_add_test(test_identity)
comp_add_test(test_animation)
comp_add_test(test_document)

add_executable(test_transport test_transport.cpp)
target_link_libraries(test_transport PRIVATE comp::engine comp_warnings)
add_test(NAME test_transport COMMAND test_transport)

add_executable(test_effects test_effects.cpp)
target_link_libraries(test_effects PRIVATE comp::engine comp_warnings)
add_test(NAME test_effects COMMAND test_effects)

# The GPU test brings up a real adapter, so it needs the Dawn runtime beside it.
add_executable(test_gpu test_gpu.cpp)
target_link_libraries(test_gpu PRIVATE comp::gpu comp_warnings)
target_copy_webgpu_binaries(test_gpu)
add_test(NAME test_gpu COMMAND test_gpu)

# Decoding needs a real file. COMP_TEST_VIDEO can be overridden; the test reports a
# skip rather than failing when it is unset.
set(COMP_TEST_VIDEO "" CACHE STRING "Path to a video file for the media test")
add_executable(test_media test_media.cpp)
target_link_libraries(test_media PRIVATE comp::media comp_warnings)
add_test(NAME test_media COMMAND test_media)
if(COMP_TEST_VIDEO)
    set_tests_properties(test_media PROPERTIES
        ENVIRONMENT "COMP_TEST_VIDEO=${COMP_TEST_VIDEO}")
endif()

# UI tests need Qt and run offscreen.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(test_menus test_menus.cpp)
target_link_libraries(test_menus PRIVATE comp::ui comp_warnings)
add_test(NAME test_menus COMMAND test_menus)
set_tests_properties(test_menus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
