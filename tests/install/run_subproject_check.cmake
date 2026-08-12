cmake_minimum_required(VERSION 4.3)

foreach (variable CLAPP_SOURCE_DIR CLAPP_SUBPROJECT_WORK_DIR CLAPP_CXX_COMPILER)
    if (NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "run_subproject_check.cmake: -D${variable}=... is required")
    endif ()
endforeach ()

cmake_path(ABSOLUTE_PATH CLAPP_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE source_dir)
cmake_path(ABSOLUTE_PATH CLAPP_SUBPROJECT_WORK_DIR NORMALIZE OUTPUT_VARIABLE work_dir)
if (work_dir STREQUAL "/" OR work_dir STREQUAL source_dir)
    message(FATAL_ERROR "run_subproject_check.cmake: unsafe CLAPP_SUBPROJECT_WORK_DIR")
endif ()

set(build_dir "${work_dir}/build")
set(source_project "${source_dir}/tests/install/subproject")
file(MAKE_DIRECTORY "${work_dir}")

set(generator_args "")
if (DEFINED CLAPP_GENERATOR AND NOT CLAPP_GENERATOR STREQUAL "")
    set(generator_args -G "${CLAPP_GENERATOR}")
endif ()

function(run_checked label)
    execute_process(COMMAND ${ARGN}
            RESULT_VARIABLE rc
            OUTPUT_VARIABLE out
            ERROR_VARIABLE err)
    if (NOT rc EQUAL 0)
        message(FATAL_ERROR
                "clapp add_subdirectory gate: ${label} failed (exit ${rc})\n"
                "command: ${ARGN}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
    endif ()
    set(last_stdout "${out}" PARENT_SCOPE)
endfunction()

run_checked("configure" "${CMAKE_COMMAND}" --fresh
        -S "${source_project}" -B "${build_dir}" ${generator_args}
        "-DCLAPP_SOURCE_DIR=${source_dir}"
        "-DCMAKE_CXX_COMPILER=${CLAPP_CXX_COMPILER}")
run_checked("build" "${CMAKE_COMMAND}" --build "${build_dir}")

find_program(consumer_exe NAMES clapp_subproject_consumer
        PATHS "${build_dir}" "${build_dir}/Debug" "${build_dir}/Release"
        NO_DEFAULT_PATH)
if (NOT consumer_exe)
    message(FATAL_ERROR "clapp add_subdirectory gate: consumer executable not found")
endif ()

run_checked("ordinary run" "${consumer_exe}" -n Alice -v)
if (NOT last_stdout STREQUAL "hello Alice verbose=1\n")
    message(FATAL_ERROR
            "clapp add_subdirectory gate: ordinary stdout mismatch\n--- actual ---\n${last_stdout}")
endif ()

run_checked("help run" "${consumer_exe}" --help)
set(expected_help
        "add_subdirectory clapp consumer smoke test

Usage: subproject-consumer [OPTIONS]

Options:
  -n, --name <name>  Name to greet [default: world]
  -v, --verbose      Be verbose
  -h, --help         Print help
  -V, --version      Print version
")
if (NOT last_stdout STREQUAL expected_help)
    message(FATAL_ERROR
            "clapp add_subdirectory gate: help stdout mismatch\n"
            "--- actual ---\n${last_stdout}\n--- expected ---\n${expected_help}")
endif ()

message(STATUS "clapp add_subdirectory gate: PASS")
