#include <clapp/parser/value_source.hpp>

#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

    using clapp::all_value_sources;
    using clapp::describe;
    using clapp::is_explicit;
    using clapp::name_of;
    using clapp::strongest;
    using clapp::value_source;
    using clapp::value_source_count;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // The enumeration itself
    // ---------------------------------------------------------------------------

    static_assert(value_source_count == 3);
    static_assert(all_value_sources.size() == value_source_count);
    static_assert(sizeof(value_source) == 1);

    // clap's three variants, all present.
    static_assert(all_value_sources[0] == value_source::default_value);
    static_assert(all_value_sources[1] == value_source::env_variable);
    static_assert(all_value_sources[2] == value_source::command_line);

    // The order IS the precedence. Asserted on the enumerators, not merely on strongest(),
    // because a plausible edit reorders the enumerators and leaves strongest() alone.
    static_assert(value_source::default_value < value_source::env_variable);
    static_assert(value_source::env_variable < value_source::command_line);
    static_assert(value_source::default_value < value_source::command_line);

    // ---------------------------------------------------------------------------
    // strongest() — clap's `existing.max(source)`
    // ---------------------------------------------------------------------------

    static_assert(strongest(value_source::default_value, value_source::command_line) ==
                  value_source::command_line);
    static_assert(strongest(value_source::command_line, value_source::default_value) ==
                  value_source::command_line);
    static_assert(strongest(value_source::default_value, value_source::env_variable) ==
                  value_source::env_variable);
    static_assert(strongest(value_source::env_variable, value_source::command_line) ==
                  value_source::command_line);

    /**
     * Commutative and idempotent, over every pair. This is what makes the order the
     * parser merges in irrelevant — `add_env()` before `add_defaults()` or the other way
     * round must reach the same answer.
     */
    [[nodiscard]] consteval bool strongest_is_a_join() {
        for (const value_source a : all_value_sources) {
            if (strongest(a, a) != a) return false;
            for (const value_source b : all_value_sources) {
                if (strongest(a, b) != strongest(b, a)) return false;
                if (strongest(a, b) != (a < b ? b : a)) return false;
                for (const value_source c : all_value_sources)
                    if (strongest(strongest(a, b), c) != strongest(a, strongest(b, c)))
                        return false;
            }
        }
        return true;
    }

    static_assert(strongest_is_a_join());

    // ---------------------------------------------------------------------------
    // is_explicit() — the one that is easy to read backwards
    // ---------------------------------------------------------------------------

    static_assert(!is_explicit(value_source::default_value));
    static_assert(is_explicit(value_source::command_line));

    // The assertion that exists purely to pin the reading down: a value that came out of
    // the environment counts as user-supplied. Deleting this line and defining
    // is_explicit as `source == command_line` would leave every other assertion in this
    // file passing.
    static_assert(is_explicit(value_source::env_variable));

    // is_explicit() partitions exactly one source off from the rest.
    [[nodiscard]] consteval std::size_t explicit_count() {
        std::size_t seen = 0;
        for (const value_source source : all_value_sources)
            if (is_explicit(source)) ++seen;
        return seen;
    }

    static_assert(explicit_count() == value_source_count - 1);

    // ---------------------------------------------------------------------------
    // Names and descriptions
    // ---------------------------------------------------------------------------

    static_assert(name_of(value_source::default_value) == "default-value"sv);
    static_assert(name_of(value_source::env_variable) == "env-variable"sv);
    static_assert(name_of(value_source::command_line) == "command-line"sv);

    /**
     * Kebab-case throughout, matching clapp::name_of(clapp::error_kind): lower-case ASCII
     * and hyphens only, never an underscore and never the Rust `PascalCase`.
     */
    [[nodiscard]] consteval bool names_are_kebab_case() {
        for (const value_source source : all_value_sources) {
            const std::string_view name = name_of(source);
            if (name.empty()) return false;
            if (name.front() == '-' || name.back() == '-') return false;
            for (const char byte : name) {
                const bool lower  = byte >= 'a' && byte <= 'z';
                const bool hyphen = byte == '-';
                if (!lower && !hyphen) return false;
            }
        }
        return true;
    }

    static_assert(names_are_kebab_case());

    /**
     * Every source is named, described, and distinct from the others. A `switch` that
     * forgets a case returns the empty view rather than failing to compile, so this is
     * what makes the omission loud.
     */
    [[nodiscard]] consteval bool every_source_is_documented() {
        for (std::size_t i = 0; i < all_value_sources.size(); ++i) {
            if (name_of(all_value_sources[i]).empty()) return false;
            if (describe(all_value_sources[i]).empty()) return false;
            for (std::size_t j = i + 1; j < all_value_sources.size(); ++j) {
                if (name_of(all_value_sources[i]) == name_of(all_value_sources[j])) return false;
                if (describe(all_value_sources[i]) == describe(all_value_sources[j])) return false;
            }
        }
        return true;
    }

    static_assert(every_source_is_documented());

    // A value outside the enumeration is not undefined behaviour here: both tables fall
    // through to an empty view rather than off the end of a function.
    static_assert(name_of(static_cast<value_source>(200)).empty());
    static_assert(describe(static_cast<value_source>(200)).empty());

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases — report the compile-time conclusions, and check the one thing a
// static_assert cannot: that the enumeration survives a round trip through storage.
// ---------------------------------------------------------------------------

CLAPP_TEST("value_source: the enumerator order is the precedence order") {
    CLAPP_CHECK(value_source::default_value < value_source::env_variable);
    CLAPP_CHECK(value_source::env_variable < value_source::command_line);
    CLAPP_CHECK(strongest(value_source::default_value, value_source::command_line) ==
                value_source::command_line);
    CLAPP_CHECK(strongest_is_a_join());
}

CLAPP_TEST("value_source: is_explicit means 'not a default', not 'from argv'") {
    CLAPP_CHECK(!is_explicit(value_source::default_value));
    CLAPP_CHECK(is_explicit(value_source::env_variable));
    CLAPP_CHECK(is_explicit(value_source::command_line));
    CLAPP_CHECK(explicit_count() == value_source_count - 1);
}

CLAPP_TEST("value_source: every source is named and described") {
    CLAPP_CHECK(every_source_is_documented());
    CLAPP_CHECK(names_are_kebab_case());
    CLAPP_CHECK(name_of(value_source::command_line) == "command-line"sv);
    CLAPP_CHECK(describe(value_source::env_variable).find("env") != std::string_view::npos);
}

CLAPP_TEST("value_source: survives a round trip through its underlying type") {
    for (const value_source source : all_value_sources) {
        const auto stored  = static_cast<std::uint8_t>(source);
        const auto restore = static_cast<value_source>(stored);
        CLAPP_CHECK(restore == source);
        CLAPP_CHECK(!name_of(restore).empty());
    }
}
