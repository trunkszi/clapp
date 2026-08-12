find_package(benchmark QUIET)

if(NOT benchmark_FOUND)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(benchmark)
endif()

clapp_add_bench(parser parse)
clapp_add_bench(lex tokenize)
clapp_add_bench(output help)
clapp_add_bench(meta command_of)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(clapp_bench_output_help PRIVATE -fconstexpr-steps=8388608)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(clapp_bench_command_of_matrix
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/benches/meta/command_of_timing.py"
                --generated-dir "${CMAKE_BINARY_DIR}/generated"
                --ops-limit --startup
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "clapp: sweeping command_of<T>() compile time"
        USES_TERMINAL
        VERBATIM)
endif()
