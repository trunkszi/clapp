/**
 * \file
 * \brief Includes the C++26 reflection header under whichever spelling this toolchain
 *        provides.
 */

#pragma once

#include <clapp/config.hpp>

#if defined(CLAPP_META_HEADER_EXPERIMENTAL) && CLAPP_META_HEADER_EXPERIMENTAL
#    include <experimental/meta>
#elif defined(CLAPP_META_HEADER_EXPERIMENTAL)
#    include <meta>
#elif __has_include(<meta>)
// Fallback for consumers who include clapp without going through its CMake,
// so CLAPP_META_HEADER_EXPERIMENTAL was never defined.
#    include <meta>
#elif __has_include(<experimental/meta>)
#    include <experimental/meta>
#else
#    error "clapp requires C++26 static reflection: neither <meta> nor <experimental/meta> is available. \
Use GCC 16+ with -freflection, or bloomberg/clang-p2996 with -freflection-latest."
#endif
