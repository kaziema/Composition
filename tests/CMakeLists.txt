function(ruby_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ruby::core ruby_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ruby_add_test(test_units)
ruby_add_test(test_identity)
ruby_add_test(test_animation)
ruby_add_test(test_document)
ruby_add_test(test_project)

add_executable(test_projectio test_projectio.cpp)
target_link_libraries(test_projectio PRIVATE ruby::io ruby_warnings)
add_test(NAME test_projectio COMMAND test_projectio)

add_executable(test_mediapool test_mediapool.cpp)
target_link_libraries(test_mediapool PRIVATE ruby::io ruby_warnings)
add_test(NAME test_mediapool COMMAND test_mediapool)

add_executable(test_peakcache test_peakcache.cpp)
target_link_libraries(test_peakcache PRIVATE ruby::media ruby_warnings)
add_test(NAME test_peakcache COMMAND test_peakcache)

# Needs an audio device; reports 77 and is recorded as SKIPPED without one.
add_executable(test_audiooutput test_audiooutput.cpp)
target_link_libraries(test_audiooutput PRIVATE ruby::audio ruby_warnings)
add_test(NAME test_audiooutput COMMAND test_audiooutput)
set_tests_properties(test_audiooutput PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_history test_history.cpp)
target_link_libraries(test_history PRIVATE ruby::io ruby_warnings)
add_test(NAME test_history COMMAND test_history)

add_executable(test_transport test_transport.cpp)
target_link_libraries(test_transport PRIVATE ruby::engine ruby_warnings)
add_test(NAME test_transport COMMAND test_transport)

add_executable(test_effects test_effects.cpp)
target_link_libraries(test_effects PRIVATE ruby::engine ruby_warnings)
add_test(NAME test_effects COMMAND test_effects)

# The GPU test brings up a real adapter, so it needs the Dawn runtime beside it.
add_executable(test_gpu test_gpu.cpp)
target_link_libraries(test_gpu PRIVATE ruby::gpu ruby_warnings)
target_copy_webgpu_binaries(test_gpu)
add_test(NAME test_gpu COMMAND test_gpu)

# Decoding needs a real file. RUBY_TEST_VIDEO can be overridden; the test reports a
# skip rather than failing when it is unset.
set(RUBY_TEST_VIDEO "" CACHE STRING "Path to a video file for the media test")
add_executable(test_media test_media.cpp)
target_link_libraries(test_media PRIVATE ruby::media ruby_warnings)
add_test(NAME test_media COMMAND test_media)
if(RUBY_TEST_VIDEO)
    set_tests_properties(test_media PROPERTIES
        ENVIRONMENT "RUBY_TEST_VIDEO=${RUBY_TEST_VIDEO}")
endif()

# UI tests need Qt and run offscreen.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(test_menus test_menus.cpp)
target_link_libraries(test_menus PRIVATE ruby::ui ruby_warnings)
add_test(NAME test_menus COMMAND test_menus)
set_tests_properties(test_menus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
