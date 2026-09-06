function(comp_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE comp::core comp_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

comp_add_test(test_units)
comp_add_test(test_identity)

# UI tests need Qt and run offscreen.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(test_menus test_menus.cpp)
target_link_libraries(test_menus PRIVATE comp::ui comp_warnings)
add_test(NAME test_menus COMMAND test_menus)
set_tests_properties(test_menus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
