cmake_minimum_required(VERSION 4.3)
project(clapp_subproject_consumer LANGUAGES CXX)

if(NOT DEFINED CLAPP_SOURCE_DIR OR CLAPP_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "CLAPP_SOURCE_DIR is required")
endif()

set(CLAPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLAPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLAPP_BUILD_BENCHES OFF CACHE BOOL "" FORCE)
set(CLAPP_BUILD_INSTALL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${CLAPP_SOURCE_DIR}" clapp)

add_executable(clapp_subproject_consumer main.cpp)
target_link_libraries(clapp_subproject_consumer PRIVATE clapp::clapp)
