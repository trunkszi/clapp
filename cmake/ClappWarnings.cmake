include_guard(GLOBAL)

add_library(clapp_warnings INTERFACE)
add_library(clapp::warnings ALIAS clapp_warnings)

if (MSVC)
    target_compile_options(clapp_warnings INTERFACE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor)
    if (CLAPP_WERROR)
        target_compile_options(clapp_warnings INTERFACE /WX)
    endif ()
else ()
    target_compile_options(clapp_warnings INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wcast-qual
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(clapp_warnings INTERFACE
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wuseless-cast)

        target_compile_options(clapp_warnings INTERFACE -Wno-shadow)
    endif ()

    if (CLAPP_WERROR)
        target_compile_options(clapp_warnings INTERFACE -Werror)
    endif ()
endif ()
