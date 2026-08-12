cmake_minimum_required(VERSION 4.3)

foreach(variable CLAPP_SOURCE_DIR CLAPP_WORK_DIR CLAPP_CXX_COMPILER)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "run_install_check.cmake: ${variable} is required")
    endif()
endforeach()

cmake_path(ABSOLUTE_PATH CLAPP_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE source_dir)
cmake_path(ABSOLUTE_PATH CLAPP_WORK_DIR NORMALIZE OUTPUT_VARIABLE work_dir)
cmake_path(GET work_dir ROOT_PATH filesystem_root)
if(work_dir STREQUAL filesystem_root OR work_dir STREQUAL source_dir)
    message(FATAL_ERROR "run_install_check.cmake: unsafe work directory")
endif()

set(build_dir "${work_dir}/build")
set(prefix_dir "${work_dir}/prefix")
set(consumer_build_dir "${work_dir}/consumer-build")
set(consumer_source_dir "${CMAKE_CURRENT_LIST_DIR}/consumer")

set(generator_args "")
if(DEFINED CLAPP_GENERATOR AND NOT CLAPP_GENERATOR STREQUAL "")
    set(generator_args -G "${CLAPP_GENERATOR}")
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "clapp install gate: ${label} failed (exit ${result})\n${output}\n${error}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

run_checked("configure" "${CMAKE_COMMAND}"
    -S "${source_dir}" -B "${build_dir}" ${generator_args}
    "-DCMAKE_CXX_COMPILER=${CLAPP_CXX_COMPILER}"
    "-DCMAKE_INSTALL_PREFIX=${prefix_dir}"
    -DCLAPP_BUILD_TESTS=OFF
    -DCLAPP_BUILD_EXAMPLES=OFF
    -DCLAPP_BUILD_BENCHES=OFF
    -DCLAPP_BUILD_INSTALL_TEST=OFF)
run_checked("build" "${CMAKE_COMMAND}" --build "${build_dir}")
run_checked("install" "${CMAKE_COMMAND}" --install "${build_dir}")

run_checked("consumer configure" "${CMAKE_COMMAND}"
    -S "${consumer_source_dir}" -B "${consumer_build_dir}" ${generator_args}
    "-DCMAKE_CXX_COMPILER=${CLAPP_CXX_COMPILER}"
    "-DCMAKE_PREFIX_PATH=${prefix_dir}")
run_checked("consumer build" "${CMAKE_COMMAND}" --build "${consumer_build_dir}")

find_program(consumer NAMES consumer
    PATHS "${consumer_build_dir}" "${consumer_build_dir}/Debug" "${consumer_build_dir}/Release"
    NO_DEFAULT_PATH)
if(NOT consumer)
    message(FATAL_ERROR "clapp install gate: consumer executable not found")
endif()

run_checked("consumer run" "${consumer}" -n Alice -v)
string(STRIP "${last_output}" output)
if(NOT output STREQUAL "hello Alice verbose=1")
    message(FATAL_ERROR "clapp install gate: unexpected consumer output: ${output}")
endif()

message(STATUS "clapp install gate: PASS")
