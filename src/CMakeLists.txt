add_library(clapp INTERFACE)
add_library(clapp::clapp ALIAS clapp)

target_include_directories(clapp INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_compile_features(clapp INTERFACE cxx_std_26)

if (MSVC)
    target_compile_options(clapp INTERFACE /EHs-c-)
    target_compile_definitions(clapp INTERFACE _HAS_EXCEPTIONS=0)
else ()
    target_compile_options(clapp INTERFACE -fno-exceptions)
endif ()

target_link_libraries(clapp INTERFACE clapp::reflection_flags)
