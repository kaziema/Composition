function(comp_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE comp::core comp_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

comp_add_test(test_units)
comp_add_test(test_identity)
