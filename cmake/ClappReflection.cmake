include(CheckCXXSourceCompiles)
include_guard(GLOBAL)

set(CLAPP_REFLECTION_FLAG_CANDIDATES "-freflection-latest" "-freflection" "")

function(_clapp_try_compile_with_flags OUT_VAR OUT_FLAG SOURCE)
    foreach (flag IN LISTS CLAPP_REFLECTION_FLAG_CANDIDATES)
        string(MAKE_C_IDENTIFIER "clapp_probe_${OUT_VAR}_${flag}" cache_var)
        set(CMAKE_REQUIRED_FLAGS "${flag}")
        set(CMAKE_REQUIRED_QUIET ON)
        check_cxx_source_compiles("${SOURCE}" ${cache_var})
        if (${cache_var})
            set(${OUT_VAR} TRUE PARENT_SCOPE)
            set(${OUT_FLAG} "${flag}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
    set(${OUT_FLAG} "" PARENT_SCOPE)
endfunction()

function(_clapp_probe_meta_header OUT_HEADER OUT_EXPERIMENTAL OUT_FLAG)
    foreach (header "meta" "experimental/meta")
        _clapp_try_compile_with_flags(_ok _flag "
            #include <${header}>
            struct S { int a; int b; };
            constexpr auto n = std::meta::nonstatic_data_members_of(
                ^^S, std::meta::access_context::current()).size();
            static_assert(n == 2);
            int main() { return 0; }
        ")
        if (_ok)
            set(${OUT_HEADER} "${header}" PARENT_SCOPE)
            set(${OUT_FLAG} "${_flag}" PARENT_SCOPE)
            if (header STREQUAL "experimental/meta")
                set(${OUT_EXPERIMENTAL} TRUE PARENT_SCOPE)
            else ()
                set(${OUT_EXPERIMENTAL} FALSE PARENT_SCOPE)
            endif ()
            return()
        endif ()
    endforeach ()
    set(${OUT_HEADER} "" PARENT_SCOPE)
    set(${OUT_EXPERIMENTAL} FALSE PARENT_SCOPE)
    set(${OUT_FLAG} "" PARENT_SCOPE)
endfunction()

function(clapp_detect_reflection)
    set(CMAKE_REQUIRED_FLAGS "")

    _clapp_probe_meta_header(_meta_header _meta_experimental _refl_flag)
    if (NOT _meta_header)
        message(FATAL_ERROR
                "clapp requires C++26 static reflection (P2996). Tried <meta> and "
                "<experimental/meta> with: ${CLAPP_REFLECTION_FLAG_CANDIDATES}")
    endif ()

    set(CMAKE_REQUIRED_FLAGS "${_refl_flag}")
    set(CMAKE_REQUIRED_QUIET ON)

    check_cxx_source_compiles("
        #include <${_meta_header}>
        #include <optional>
        struct attr { char c = 0; int n = 0; };
        struct S { [[= attr{'x', 7}]] int a; };
        consteval std::optional<attr> get() {
            auto m = std::meta::nonstatic_data_members_of(
                ^^S, std::meta::access_context::current())[0];
            for (std::meta::info a : std::meta::annotations_of(m)) {
                std::meta::info k = std::meta::constant_of(a);
                if (std::meta::type_of(k) == ^^const attr)
                    return std::meta::extract<attr>(k);
            }
            return std::nullopt;
        }
        static_assert(get().has_value() && get()->n == 7);
        int main() { return 0; }
    " CLAPP_PROBE_ANNOTATIONS)

    check_cxx_source_compiles("
        #include <${_meta_header}>
        struct S { int a; int b; int c; };
        consteval int count() {
            int n = 0;
            template for (constexpr std::meta::info m :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                              ^^S, std::meta::access_context::current())))
                ++n;
            return n;
        }
        static_assert(count() == 3);
        int main() { return 0; }
    " CLAPP_PROBE_EXPANSION)

    set(CLAPP_HAS_REFLECTION TRUE PARENT_SCOPE)
    set(CLAPP_HAS_ANNOTATIONS ${CLAPP_PROBE_ANNOTATIONS} PARENT_SCOPE)
    set(CLAPP_HAS_EXPANSION_STATEMENTS ${CLAPP_PROBE_EXPANSION} PARENT_SCOPE)
    set(CLAPP_REFLECTION_FLAGS "${_refl_flag}" PARENT_SCOPE)
    set(CLAPP_META_HEADER "${_meta_header}" PARENT_SCOPE)
    set(CLAPP_META_HEADER_EXPERIMENTAL ${_meta_experimental} PARENT_SCOPE)

    add_library(clapp_reflection_flags INTERFACE)
    add_library(clapp::reflection_flags ALIAS clapp_reflection_flags)
    if (_refl_flag)
        target_compile_options(clapp_reflection_flags INTERFACE "${_refl_flag}")
    endif ()

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(clapp_reflection_flags INTERFACE -fconstexpr-steps=16000000)
    endif ()

    if (NOT CLAPP_PROBE_ANNOTATIONS)
        message(FATAL_ERROR
                "clapp: P2996 reflection detected, but the P3394 annotation value path is unavailable.\n"
                "  Annotations are the DSL carrier ([[= clapp::arg{...}]]) and cannot be missing.\n"
                "  The probe takes the full path constant_of -> ^^const A -> extract;\n"
                "  supporting only [[= ...]] syntax without extractable values still fails.")
    endif ()
    if (NOT CLAPP_PROBE_EXPANSION)
        message(FATAL_ERROR
                "clapp: P2996 reflection detected, but P1306 expansion statements (template for) are unavailable.\n"
                "  Both command_of<T>() and from_matches<T>() rely on them to walk data members.")
    endif ()
endfunction()
