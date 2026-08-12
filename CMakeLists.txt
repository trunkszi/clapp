cmake_minimum_required(VERSION 4.3.0)

project(clapp
        VERSION 0.1.0
        LANGUAGES CXX
        DESCRIPTION "Command Line Argument Parser for Plus-plus"
        HOMEPAGE_URL "https://github.com/quincy/clapp")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (PROJECT_IS_TOP_LEVEL)
    set(CLAPP_DEFAULT_DEV ON)
else ()
    set(CLAPP_DEFAULT_DEV OFF)
endif ()

option(CLAPP_BUILD_TESTS "Build unit and end-to-end tests" ${CLAPP_DEFAULT_DEV})
option(CLAPP_BUILD_EXAMPLES "Build examples" ${CLAPP_DEFAULT_DEV})
option(CLAPP_BUILD_BENCHES "Build benchmarks" OFF)
option(CLAPP_WERROR "Treat warnings as errors" OFF)
option(CLAPP_USE_CATCH2 "Use Catch2 instead of the built-in minimal test harness" OFF)

if (PROJECT_IS_TOP_LEVEL AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CLAPP_BUILD_TESTS ON CACHE BOOL "Build unit and end-to-end tests" FORCE)
    set(CLAPP_BUILD_EXAMPLES ON CACHE BOOL "Build examples" FORCE)
    set(CLAPP_BUILD_BENCHES ON CACHE BOOL "Build benchmarks" FORCE)
endif ()

option(CLAPP_BUILD_INSTALL_TEST "Register the find_package(clapp) install/export gate" ON)

set(CLAPP_SANITIZER "none" CACHE STRING "none / address / undefined / thread")
set_property(CACHE CLAPP_SANITIZER PROPERTY STRINGS none address undefined thread)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable exceptions for this build tree; clapp also exports the requirement.
if (MSVC)
    add_compile_options(/EHs-c-)
    add_compile_definitions(_HAS_EXCEPTIONS=0)
else ()
    add_compile_options(-fno-exceptions)
endif ()

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif ()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(ClappWarnings)
include(ClappSanitizers)
include(ClappReflection)
include(ClappTargets)

clapp_detect_reflection()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.hpp.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/clapp/config.hpp"
        @ONLY)

add_subdirectory(src)

if (CLAPP_BUILD_TESTS AND NOT CLAPP_BUILD_EXAMPLES)
    message(STATUS "clapp: CLAPP_BUILD_TESTS=ON, automatically enabling CLAPP_BUILD_EXAMPLES (e2e depends on example binaries)")
    set(CLAPP_BUILD_EXAMPLES ON)
endif ()

if (CLAPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

if (CLAPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (CLAPP_BUILD_BENCHES)
    add_subdirectory(benches)
endif ()

set_target_properties(clapp_warnings PROPERTIES EXPORT_NAME warnings)
set_target_properties(clapp_reflection_flags PROPERTIES EXPORT_NAME reflection_flags)

install(TARGETS clapp clapp_warnings clapp_reflection_flags
        EXPORT clappTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/clapp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/clapp/config.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/clapp")

install(EXPORT clappTargets
        FILE clappTargets.cmake
        NAMESPACE clapp::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/clapp")

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/clapp-config-version.cmake"
        COMPATIBILITY SameMajorVersion)

configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clapp-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/clapp-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/clapp")

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/clapp-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/clapp-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/clapp")

message(STATUS "")
message(STATUS "  clapp ${PROJECT_VERSION} — Command Line Argument Parser for Plus-plus")
message(STATUS "  ─────────────────────────────────────────────────────────────")
message(STATUS "  Compiler              : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type            : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Reflection (P2996)    : ${CLAPP_HAS_REFLECTION} (required)")
message(STATUS "  Annotations (P3394)   : ${CLAPP_HAS_ANNOTATIONS}")
message(STATUS "  Expansion stmts (P1306): ${CLAPP_HAS_EXPANSION_STATEMENTS}")
message(STATUS "  Reflection flags      : ${CLAPP_REFLECTION_FLAGS}")
message(STATUS "  Tests / examples      : ${CLAPP_BUILD_TESTS} / ${CLAPP_BUILD_EXAMPLES}")
message(STATUS "  Benchmarks            : ${CLAPP_BUILD_BENCHES}")
message(STATUS "  Sanitizer             : ${CLAPP_SANITIZER}")
message(STATUS "")
