include_guard(GLOBAL)

function(clapp_add_unit_test module name)
    set(target "clapp_units_${module}_${name}")
    add_executable(${target} "${PROJECT_SOURCE_DIR}/tests/units/${module}/${name}_test.cpp")
    target_link_libraries(${target} PRIVATE clapp::clapp clapp::warnings clapp::sanitizers clapp::test_support)
    add_test(NAME "units.${module}.${name}" COMMAND ${target})
    set_tests_properties("units.${module}.${name}" PROPERTIES LABELS "units;${module}")
endfunction()

function(clapp_add_example path)
    string(REPLACE "/" "_" flat "${path}")
    set(target "clapp_example_${flat}")
    add_executable(${target} "${PROJECT_SOURCE_DIR}/examples/${path}.cpp")
    target_link_libraries(${target} PRIVATE clapp::clapp clapp::warnings clapp::sanitizers)
    # e2e needs a stable path to find it
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/examples")
endfunction()

function(clapp_add_e2e axis path)
    cmake_parse_arguments(A "" "DRIVES" "" ${ARGN})

    string(REPLACE "/" "_" flat "${axis}_${path}")
    string(REPLACE "/" "." dotted "${axis}.${path}")
    set(target "clapp_e2e_${flat}")

    add_executable(${target} "${PROJECT_SOURCE_DIR}/tests/e2e/${axis}/${path}_e2e.cpp")
    target_link_libraries(${target} PRIVATE clapp::clapp clapp::warnings clapp::sanitizers clapp::test_support)

    if(A_DRIVES)
        string(REPLACE "/" "_" drives_flat "${A_DRIVES}")
        set(driven "clapp_example_${drives_flat}")
        if(NOT TARGET ${driven})
            message(STATUS "clapp: skipping e2e.${dotted} (driven example target ${driven} does not exist)")
            return()
        endif()
        add_dependencies(${target} ${driven})
        target_compile_definitions(${target} PRIVATE
            CLAPP_E2E_BINARY="$<TARGET_FILE:${driven}>")
    endif()

    add_test(NAME "e2e.${dotted}" COMMAND ${target})
    set_tests_properties("e2e.${dotted}" PROPERTIES LABELS "e2e;${axis}")
endfunction()

function(clapp_add_bench module name)
    set(target "clapp_bench_${module}_${name}")
    add_executable(${target} "${PROJECT_SOURCE_DIR}/benches/${module}/${name}_bench.cpp")
    target_link_libraries(${target} PRIVATE clapp::clapp clapp::warnings)
    if(TARGET benchmark::benchmark)
        target_link_libraries(${target} PRIVATE benchmark::benchmark)
    endif()
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/benches")
endfunction()

function(clapp_add_multi_tu_test name)
    set(sources "")
    foreach(src IN LISTS ARGN)
        list(APPEND sources "${PROJECT_SOURCE_DIR}/tests/units/${src}")
    endforeach()

    set(target "clapp_units_${name}")
    add_executable(${target} ${sources})
    target_link_libraries(${target} PRIVATE clapp::clapp clapp::warnings clapp::sanitizers clapp::test_support)
    add_test(NAME "units.${name}" COMMAND ${target})
    set_tests_properties("units.${name}" PROPERTIES LABELS "units;umbrella")
endfunction()

function(clapp_add_compile_fail_test module name)
    cmake_parse_arguments(A "" "EXPECT;PATTERN;PATTERN_GNU;PATTERN_CLANG" "" ${ARGN})

    set(source "${PROJECT_SOURCE_DIR}/tests/units/${module}/compile_fail/${name}_test.cpp")

    if(NOT A_EXPECT)
        set(A_EXPECT "rejected")
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND A_PATTERN_GNU)
        set(pattern "${A_PATTERN_GNU}")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND A_PATTERN_CLANG)
        set(pattern "${A_PATTERN_CLANG}")
    else()
        set(pattern "${A_PATTERN}")
    endif()

    if(A_EXPECT STREQUAL "rejected" AND NOT pattern)
        message(FATAL_ERROR
            "clapp: compile_fail.${module}.${name} has no PATTERN for "
            "${CMAKE_CXX_COMPILER_ID}. A compile-fail test without one passes for any "
            "compiler error at all, including a typo in the snippet.")
    endif()

    set(flags "-std=c++26")
    foreach(flag IN LISTS CLAPP_REFLECTION_FLAGS CLAPP_SANITIZER_FLAGS)
        if(flag)
            string(APPEND flags "|${flag}")
        endif()
    endforeach()
    if(MSVC)
        string(APPEND flags "|/EHs-c-")
    else()
        string(APPEND flags "|-fno-exceptions")
    endif()
    string(APPEND flags "|-fsyntax-only")
    string(APPEND flags "|-fdiagnostics-color=never")
    string(APPEND flags "|-I${PROJECT_SOURCE_DIR}/include")
    string(APPEND flags "|-I${PROJECT_BINARY_DIR}/generated")
    string(APPEND flags "|-I${PROJECT_SOURCE_DIR}/tests")

    add_test(NAME "compile_fail.${module}.${name}"
             COMMAND "${CMAKE_COMMAND}"
                     "-DCLAPP_CF_NAME=${module}.${name}"
                     "-DCLAPP_CF_SOURCE=${source}"
                     "-DCLAPP_CF_COMPILER=${CMAKE_CXX_COMPILER}"
                     "-DCLAPP_CF_FLAGS=${flags}"
                     "-DCLAPP_CF_EXPECT=${A_EXPECT}"
                     "-DCLAPP_CF_PATTERN=${pattern}"
                     "-DCLAPP_CF_NO_EXCEPTIONS=ON"
                     -P "${PROJECT_SOURCE_DIR}/cmake/ClappCompileFail.cmake")
    set_tests_properties("compile_fail.${module}.${name}" PROPERTIES
        LABELS "compile_fail;${module}")
endfunction()
