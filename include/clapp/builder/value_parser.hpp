/**
 * \file
 * \brief value_parser, parse_error, builtins, and type-erased parser_vtable.
 */

#pragma once

#include <clapp/builder/possible_value.hpp>
#include <clapp/detail/std_meta.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/util/any_value.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace clapp {

    // -----------------------------------------------------------------------
    // The error side
    // -----------------------------------------------------------------------

    /**
     * \brief Why one command-line value could not become a typed value.
     * Five kinds map to distinct renderer sentences; detail lives on parse_error.
     */
    enum class parse_error_kind : unsigned char {
        /** Bytes are not valid UTF-8 (textual/numeric target). clap invalid_utf8. */
        invalid_utf8,
        /**
         * Empty treated as "none supplied" (clap empty_value → required message).
         * \warning Policy, not observation: empty is not enough. Numerics use
         *          invalid_digit for `""` (value *was* supplied). Reserve for types
         *          with no empty member — clapp: only value_parser<path>.
         *          Measured: `--port ""` ValueValidation; `--path ""` InvalidValue.
         */
        empty_value,
        /** Not a number (junk, empty, trailing junk). clap value_validation. */
        invalid_digit,
        /**
         * Well-formed number outside the target type. clap "{value} is not in {range}".
         * \note value_reason() builds that from domain when reason is empty.
         */
        out_of_range,
        /**
         * Not in an enumerated set (or non-numeric catch-all). possible routes to
         * error_kind::invalid_value when non-empty.
         */
        invalid_value,
    };

    /**
     * \brief A human-readable name for \p kind, for error messages.
     * \param kind The failure to describe.
     * \return A view into static storage; safe to keep indefinitely.
     * \note Overloads clapp::describe(encoding_error) rather than picking a distinct
     *       name, so the renderer can spell one call for either enumeration.
     */
    [[nodiscard]] constexpr std::string_view describe(parse_error_kind kind) noexcept {
        switch (kind) {
        case parse_error_kind::invalid_utf8:
            return "value is not valid UTF-8";
        case parse_error_kind::empty_value:
            return "value is empty";
        case parse_error_kind::invalid_digit:
            return "value is not a number";
        case parse_error_kind::out_of_range:
            return "value is out of range for its type";
        case parse_error_kind::invalid_value:
            return "value is not one of the accepted values";
        }
        return "unknown parse error";
    }

    /**
     * \brief Context-free payload for one failed value conversion (feeds clapp::error).
     * \note type_name spelling is implementation-defined but unique per type.
     * \warning #input is a borrowing os_str — usually argv, but a temporary string
     *          dangles if the error is kept. Other members are static and durable.
     */
    struct parse_error {
        /** \brief What class of failure this is. */
        parse_error_kind kind = parse_error_kind::invalid_value;

        /** \brief The raw bytes that failed. Borrowed; see the class warning. */
        os_str input{};

        /**
         * \brief Spelling of the type the value was to become, from
         *        `std::meta::display_string_of`. Static storage.
         */
        std::string_view type_name{};

        /** \brief Failure sentence, or empty for describe(#kind). Static storage. */
        std::string_view reason{};

        /**
         * \brief Accepted domain as static text (e.g. `"0..=65535"`), or empty.
         * \note Used by value_reason() only for out_of_range with empty reason.
         */
        std::string_view domain{};

        /**
         * \brief Accepted values (includes hidden); empty when unbounded.
         * \note Hidden still match; renderer filters via visible_values().
         */
        std::span<const possible_value> possible{};

        /**
         * \brief Where and how the bytes stopped being UTF-8.
         * \note Meaningful only when #kind is parse_error_kind::invalid_utf8;
         *       value-initialized otherwise.
         */
        invalid_encoding encoding{};

        /** \brief #reason, or a generic description of #kind when none was set. */
        [[nodiscard]] constexpr std::string_view message() const noexcept {
            return reason.empty() ? describe(kind) : reason;
        }

        /** \brief Whether an enumerated set of accepted values is available. */
        [[nodiscard]] constexpr bool has_possible_values() const noexcept {
            return !possible.empty();
        }

        /**
         * \brief Visible accepted names (lazy range for did_you_mean).
         * \warning Borrows #possible; must not outlive this parse_error.
         */
        [[nodiscard]] constexpr auto visible_values() const noexcept {
            return possible |
                   std::views::filter([](const possible_value& v) { return !v.is_hide_set(); }) |
                   std::views::transform([](const possible_value& v) { return v.get_name(); });
        }

        /**
         * \brief Equality by content, including the accepted-value list.
         *
         * \note Hand-written rather than defaulted for the same reason
         *       clapp::possible_value's is: a defaulted comparison would compare
         *       #possible as a pointer/length pair, so two errors naming the same
         *       values through different arrays would compare unequal.
         */
        [[nodiscard]] constexpr bool operator==(const parse_error& other) const noexcept {
            return kind == other.kind && input == other.input && type_name == other.type_name &&
                   reason == other.reason && domain == other.domain && encoding == other.encoding &&
                   std::ranges::equal(possible, other.possible);
        }
    };

    // -----------------------------------------------------------------------
    // The customization point
    // -----------------------------------------------------------------------

    /**
     * \brief Extension point: turn command-line bytes into `T`. Primary template empty.
     * \tparam T Target type.
     * \note Specialize with static parse(os_str) → expected<T, parse_error>; optional
     *       possible_values(). Optional parse(os_str, bool ignore_case) with default.
     *       Args store const parser_vtable* from parser_for(), not a parser object.
     */
    template<class T>
    struct value_parser {};

    /**
     * \brief Type with value_parser<T>::parse(os_str) → expected<T, parse_error> exactly.
     * \tparam T Candidate value type.
     */
    template<class T>
    concept parsable = requires(os_str value) {
        { value_parser<T>::parse(value) } -> std::same_as<std::expected<T, parse_error>>;
    };

    /**
     * \brief parsable type with parse(os_str, bool ignore_case).
     * \tparam T Candidate value type.
     * \note Built-in enums satisfy this; bool folds case unconditionally without the param.
     */
    template<class T>
    concept case_insensitively_parsable = parsable<T> && requires(os_str value, bool ignore_case) {
        {
            value_parser<T>::parse(value, ignore_case)
        } -> std::same_as<std::expected<T, parse_error>>;
    };

    /**
     * \brief parsable type that declares possible_values() (may still return empty).
     * \tparam T Candidate value type.
     * \note For "does it enumerate?", check possible_values_of<T>().empty().
     */
    template<class T>
    concept enumerable_parser = parsable<T> && requires {
        {
            value_parser<T>::possible_values()
        } -> std::convertible_to<std::span<const possible_value>>;
    };

    // -----------------------------------------------------------------------
    // Shared machinery for the builtin specializations
    // -----------------------------------------------------------------------

    namespace detail {

        /**
         * \brief Target type spelling in static storage (detail::type_name; unique per type).
         * \tparam T Type being parsed into.
         */
        template<class T>
        inline constexpr std::string_view parsed_type_name = type_name<T>();

        /**
         * \brief The ten integer types std::from_chars accepts (covers all cstdint aliases).
         * \tparam T Candidate type.
         * \warning Not `integral && !bool`: plain char is signed_integral and would be
         *          parsed as a number; char has its own single-byte specialization.
         */
        template<class T>
        concept standard_integer =
                std::same_as<T, signed char> || std::same_as<T, short> || std::same_as<T, int> ||
                std::same_as<T, long> || std::same_as<T, long long> ||
                std::same_as<T, unsigned char> || std::same_as<T, unsigned short> ||
                std::same_as<T, unsigned int> || std::same_as<T, unsigned long> ||
                std::same_as<T, unsigned long long>;

        /**
         * \brief The three standard floating-point types `std::from_chars` accepts.
         * \tparam T Candidate type.
         * \note Extended types (`__float128`, `std::float16_t`, …) are excluded: GCC 16
         *       has no `from_chars` for them, so a specialization would have to bring
         *       its own decimal-to-binary conversion.
         */
        template<class T>
        concept standard_float =
                std::same_as<T, float> || std::same_as<T, double> || std::same_as<T, long double>;

        /**
         * \brief `"min..=max"` for \p I, in static storage.
         *
         * \tparam I The integer type whose domain is wanted.
         * \return A view over static storage, e.g. `"0..=65535"` for `std::uint16_t`.
         *
         * \note The spelling is clap's `RangedI64ValueParser::format_bounds()`, so
         *       `"99999 is not in 0..=65535"` reads identically in both libraries.
         * \note `std::to_chars` is `constexpr` for integer types since C++23 (P2291),
         *       which is what makes this computable at all.
         */
        template<standard_integer I>
        [[nodiscard]] consteval std::string_view integer_domain_text() {
            std::array<char, 64> buffer{};
            char* cursor = buffer.data();

            // Raw loop-free but hand-sequenced: to_chars writes through a moving
            // cursor, which no ranges pipeline expresses more clearly.
            const auto write = [&](auto number) {
                const std::to_chars_result written =
                        std::to_chars(cursor, buffer.data() + buffer.size(), number);
                cursor = written.ptr;
            };
            write(std::numeric_limits<I>::min());
            *cursor++ = '.';
            *cursor++ = '.';
            *cursor++ = '=';
            write(std::numeric_limits<I>::max());

            const auto length = static_cast<std::size_t>(cursor - buffer.data());
            return std::string_view(
                    std::define_static_string(std::string_view(buffer.data(), length)));
        }

        /**
         * \brief integer_domain_text() memoized as a variable template.
         * \tparam I The integer type whose domain is wanted.
         */
        template<standard_integer I>
        inline constexpr std::string_view integer_domain = integer_domain_text<I>();

        /**
         * \brief from_chars for float types; shims long double on libc++ (no overload).
         * \tparam F float, double, or long double.
         * \param first Start of digits. \param last One past end. \param value Out.
         * \param fmt Grammar (chars_format::general).
         * \return Same as std::from_chars.
         * \note Fallback parses via double only when F has identical numeric limits;
         *       otherwise static_assert (no silent precision loss).
         */
        template<class F>
        [[nodiscard]] inline std::from_chars_result float_from_chars(
                const char* first, const char* last, F& value, std::chars_format fmt) noexcept {
            if constexpr (requires { std::from_chars(first, last, value, fmt); }) {
                return std::from_chars(first, last, value, fmt);
            } else {
                using limits        = std::numeric_limits<F>;
                using double_limits = std::numeric_limits<double>;
                static_assert(limits::radix == double_limits::radix &&
                                      limits::digits == double_limits::digits &&
                                      limits::min_exponent == double_limits::min_exponent &&
                                      limits::max_exponent == double_limits::max_exponent,
                              "this standard library has no std::from_chars for this "
                              "floating-point type, and it is wider than double, so no "
                              "lossless substitute exists");
                double narrow                       = 0;
                const std::from_chars_result result = std::from_chars(first, last, narrow, fmt);
                if (result.ec == std::errc{}) value = static_cast<F>(narrow);
                return result;
            }
        }

        /**
         * \brief Build the parse_error for bytes that were not valid UTF-8.
         *
         * \tparam T The target type, named in the diagnostic.
         * \param value    The offending bytes; borrowed into parse_error::input.
         * \param failure  What os_str::to_string_view() reported.
         * \param accepted The accepted-value list to attach, if any.
         */
        template<class T>
        [[nodiscard]] constexpr parse_error
        not_utf8(os_str value,
                 invalid_encoding failure,
                 std::span<const possible_value> accepted = {}) noexcept {
            return parse_error{.kind      = parse_error_kind::invalid_utf8,
                               .input     = value,
                               .type_name = parsed_type_name<T>,
                               .reason    = failure.message(),
                               .possible  = accepted,
                               .encoding  = failure};
        }

    }  // namespace detail

    // -----------------------------------------------------------------------
    // Integers
    // -----------------------------------------------------------------------

    /**
     * \brief Parse a standard integer, range-checked against `I` (not a wider type).
     * \tparam I One of detail::standard_integer.
     * \note Accepts leading `+` (stripped; from_chars does not). No base prefixes,
     *       no whitespace. Empty → invalid_digit, not empty_value.
     * \warning Range is against `I`: `--port 99999` for uint16_t is out_of_range,
     *          never silent wrap. This differs from clap's i64-then-range path on some
     *          edge cases.
     */
    template<detail::standard_integer I>
    struct value_parser<I> {
        /**
         * \brief Convert \p value to an `I`.
         * \param value The raw bytes from the command line.
         * \return The number, or why it could not be one.
         */
        [[nodiscard]] static constexpr std::expected<I, parse_error> parse(os_str value) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value()) return std::unexpected(detail::not_utf8<I>(value, text.error()));

            std::string_view digits = text.value();
            // parse_error_kind::invalid_digit, *not* empty_value: `--port ""` supplied a
            // value and it failed to convert, which is Rust's `IntErrorKind::Empty` and
            // clap's `ValueValidation`. See parse_error_kind::empty_value's \warning.
            if (digits.empty())
                return std::unexpected(
                        parse_error{.kind      = parse_error_kind::invalid_digit,
                                    .input     = value,
                                    .type_name = detail::parsed_type_name<I>,
                                    .reason    = "cannot parse integer from empty string",
                                    .domain    = detail::integer_domain<I>});

            const auto invalid = [&] {
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_digit,
                                                   .input     = value,
                                                   .type_name = detail::parsed_type_name<I>,
                                                   .reason    = "invalid digit found in string",
                                                   .domain    = detail::integer_domain<I>});
            };

            // `std::from_chars` deliberately rejects a leading '+' ([charconv.from.chars]:
            // "a minus sign is the only sign that may appear"), while Rust's FromStr —
            // and therefore clap — accepts it. Strip it, but only in front of a digit,
            // so that "+-1" and "+" stay errors rather than becoming -1 and an empty
            // parse.
            if (digits.front() == '+') {
                digits.remove_prefix(1);
                if (digits.empty() || digits.front() < '0' || digits.front() > '9')
                    return invalid();
            }

            I number = 0;
            const std::from_chars_result result =
                    std::from_chars(digits.data(), digits.data() + digits.size(), number, 10);

            // **No `.reason` on purpose.** `std::from_chars` would say "number too large
            // to fit in target type", which never tells the user what *would* fit;
            // clapp::detail::value_reason() turns the empty reason plus #domain into
            // clap's `99999 is not in 0..=65535`. Writing a sentence here takes
            // precedence over the domain and silently restores the old message — see
            // that function's rung 1.
            if (result.ec == std::errc::result_out_of_range)
                return std::unexpected(parse_error{.kind      = parse_error_kind::out_of_range,
                                                   .input     = value,
                                                   .type_name = detail::parsed_type_name<I>,
                                                   .domain    = detail::integer_domain<I>});

            if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
                return invalid();

            return number;
        }

        /**
         * \brief No enumerable domain: an integer type has 2^N values, not a list.
         * \return An empty span, always.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };

    // -----------------------------------------------------------------------
    // Floating point
    // -----------------------------------------------------------------------

    /**
     * \brief Parse float, double, or long double (inf/nan accepted; leading `+` stripped).
     * \tparam F One of the three standard floating-point types.
     * \note Rejects nan(payload). Overflow → out_of_range, not silent inf (unlike Rust).
     * \warning parse is not constexpr: floating from_chars is runtime-only in GCC 16.
     *          No dual-path fast path (would risk compile/runtime divergence).
     */
    template<detail::standard_float F>
    struct value_parser<F> {
        /**
         * \brief Convert \p value to an `F`.
         * \param value The raw bytes from the command line.
         * \return The number, or why it could not be one.
         * \note Not `constexpr`; see the class warning.
         */
        [[nodiscard]] static std::expected<F, parse_error> parse(os_str value) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value()) return std::unexpected(detail::not_utf8<F>(value, text.error()));

            std::string_view number_text = text.value();
            // parse_error_kind::invalid_digit, *not* empty_value — same reasoning as
            // value_parser<I>: Rust's `ParseFloatError`, clap's `ValueValidation`.
            if (number_text.empty())
                return std::unexpected(
                        parse_error{.kind      = parse_error_kind::invalid_digit,
                                    .input     = value,
                                    .type_name = detail::parsed_type_name<F>,
                                    .reason    = "cannot parse float from empty string"});

            const auto invalid = [&] {
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_digit,
                                                   .input     = value,
                                                   .type_name = detail::parsed_type_name<F>,
                                                   .reason    = "invalid float literal"});
            };

            // `nan(char-sequence)` is a C spelling that `std::from_chars` honours and
            // Rust does not. Rejecting the parenthesis up front is simpler than
            // inspecting the result, and no other accepted spelling contains one.
            if (number_text.contains('(')) return invalid();

            // Same leading-'+' correction as for integers; see value_parser<I>::parse.
            if (number_text.front() == '+') {
                number_text.remove_prefix(1);
                if (number_text.empty()) return invalid();
            }

            F number = 0;
            const std::from_chars_result result =
                    detail::float_from_chars(number_text.data(),
                                             number_text.data() + number_text.size(),
                                             number,
                                             std::chars_format::general);

            if (result.ec == std::errc::result_out_of_range)
                return std::unexpected(
                        parse_error{.kind      = parse_error_kind::out_of_range,
                                    .input     = value,
                                    .type_name = detail::parsed_type_name<F>,
                                    .reason    = "number is outside the representable range"});

            if (result.ec != std::errc{} || result.ptr != number_text.data() + number_text.size())
                return invalid();

            return number;
        }

        /**
         * \brief No enumerable domain.
         * \return An empty span, always.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };

    // -----------------------------------------------------------------------
    // bool
    // -----------------------------------------------------------------------

    namespace detail {

        /**
         * \brief clap TRUE_LITERALS then FALSE_LITERALS; only true/false are visible.
         * \note Index < boolean_true_count means true.
         */
        inline constexpr std::array<possible_value, 12> boolean_literals{
                possible_value{.name = arg_id{"y"}, .hide = true},
                possible_value{.name = arg_id{"yes"}, .hide = true},
                possible_value{.name = arg_id{"t"}, .hide = true},
                possible_value{.name = arg_id{"true"}},
                possible_value{.name = arg_id{"on"}, .hide = true},
                possible_value{.name = arg_id{"1"}, .hide = true},
                possible_value{.name = arg_id{"n"}, .hide = true},
                possible_value{.name = arg_id{"no"}, .hide = true},
                possible_value{.name = arg_id{"f"}, .hide = true},
                possible_value{.name = arg_id{"false"}},
                possible_value{.name = arg_id{"off"}, .hide = true},
                possible_value{.name = arg_id{"0"}, .hide = true},
        };

        /** \brief Number of entries in boolean_literals that mean `true`. */
        inline constexpr std::size_t boolean_true_count = 6;

        static_assert(boolean_literals[boolean_true_count - 1].get_name() == "1",
                      "clapp: the first half of boolean_literals must be clap's "
                      "TRUE_LITERALS; value_parser<bool> reads the meaning from the index.");
        static_assert(boolean_literals[boolean_true_count].get_name() == "n",
                      "clapp: the second half of boolean_literals must be clap's "
                      "FALSE_LITERALS.");

    }  // namespace detail

    /**
     * \brief Parse bool from clap's twelve Boolish spellings (ASCII case-insensitive).
     * \note Default is BoolishValueParser, not clap's strict true/false-only bool parser.
     * \warning Case folding is ASCII-only, never locale-aware.
     */
    template<>
    struct value_parser<bool> {
        /**
         * \brief Convert \p value to a `bool`.
         * \param value The raw bytes from the command line.
         * \return The boolean, or why it was not one.
         */
        [[nodiscard]] static constexpr std::expected<bool, parse_error>
        parse(os_str value) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value())
                return std::unexpected(
                        detail::not_utf8<bool>(value, text.error(), possible_values()));

            // Raw loop rather than `ranges::find_if`: the *index* is the answer here —
            // it carries the true/false meaning — and an iterator would have to be
            // converted back into one anyway.
            for (std::size_t i = 0; i < detail::boolean_literals.size(); ++i) {
                if (detail::boolean_literals[i].matches(text.value(), /*ignore_case=*/true))
                    return i < detail::boolean_true_count;
            }

            // One kind for every rejection, empty included: clap's `BoolValueParser`
            // ends in a single `Error::invalid_value` call, and the accepted-value list
            // below is what routes it there in clapp too — see
            // clapp::detail::parse_engine::value_error(). An empty value still renders
            // as clap's `a value is required for '--flag <FLAG>' but none was supplied
            // [possible values: true, false]`, because that is what an
            // clapp::error_kind::invalid_value with an empty bad value renders as.
            return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                               .input     = value,
                                               .type_name = detail::parsed_type_name<bool>,
                                               .reason    = "value was not a boolean",
                                               .possible  = possible_values()});
        }

        /**
         * \brief All twelve spellings; only `true` and `false` are visible.
         * \return A span over static storage.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return std::span<const possible_value>{detail::boolean_literals};
        }
    };

    // -----------------------------------------------------------------------
    // char
    // -----------------------------------------------------------------------

    /**
     * \brief Parse exactly one byte (separator/quote fields).
     * \warning Byte-oriented, not a Unicode scalar: multi-byte input is rejected whole
     *          (Rust char accepts one code point). Use std::string for non-ASCII seps.
     * \note No UTF-8 check; high bytes allowed (matches byte-wise delimiters).
     */
    template<>
    struct value_parser<char> {
        /**
         * \brief Convert \p value to a `char`.
         * \param value The raw bytes from the command line.
         * \return The byte, or why there was not exactly one.
         */
        [[nodiscard]] static constexpr std::expected<char, parse_error>
        parse(os_str value) noexcept {
            // parse_error_kind::invalid_value with no accepted-value list routes to
            // clapp::error_kind::value_validation, which is clap's `ValueValidation` for
            // `--sep ""`. The wording is Rust's `ParseCharError`, which clap forwards
            // verbatim. Not empty_value — see that enumerator's \warning.
            if (value.empty())
                return std::unexpected(
                        parse_error{.kind      = parse_error_kind::invalid_value,
                                    .input     = value,
                                    .type_name = detail::parsed_type_name<char>,
                                    .reason    = "cannot parse char from empty string"});
            if (value.size() != 1)
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                                   .input     = value,
                                                   .type_name = detail::parsed_type_name<char>,
                                                   .reason    = "expected exactly one byte"});
            return value[0];
        }

        /**
         * \brief No enumerable domain: 256 bytes is not a help listing.
         * \return An empty span, always.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };

    // -----------------------------------------------------------------------
    // Text and paths
    // -----------------------------------------------------------------------

    /**
     * \brief Parse UTF-8 into std::string (clap StringValueParser). Empty is accepted.
     * \note Strict UTF-8 (not WTF-8). Use os_string for non-text bytes.
     */
    template<>
    struct value_parser<std::string> {
        /**
         * \brief Copy \p value out as text.
         * \param value The raw bytes from the command line.
         * \return The text, or where it stopped being UTF-8.
         */
        [[nodiscard]] static constexpr std::expected<std::string, parse_error> parse(os_str value) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value())
                return std::unexpected(detail::not_utf8<std::string>(value, text.error()));
            return std::string(text.value());
        }

        /**
         * \brief No enumerable domain.
         * \return An empty span, always.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };

    /**
     * \brief Copy raw bytes into os_string; never fails (clap OsStringValueParser).
     */
    template<>
    struct value_parser<os_string> {
        /**
         * \brief Copy \p value out.
         * \param value The raw bytes from the command line.
         * \return An owning copy of exactly those bytes. Always engaged.
         */
        [[nodiscard]] static constexpr std::expected<os_string, parse_error> parse(os_str value) {
            return os_string(value);
        }

        /**
         * \brief No enumerable domain.
         * \return An empty span, always.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };

    /**
     * \brief Parse a filesystem path; empty is empty_value (clap PathBufValueParser).
     * \note via os_str::to_native(); no UTF-8 requirement.
     * \warning parse is not constexpr (filesystem::path constructors are not).
     */
    template<>
    struct value_parser<std::filesystem::path> {
        /**
         * \brief Convert \p value to a path.
         * \param value Command-line bytes.
         * \return The path, or empty_value (only builtin that uses that kind).
         * \note Not constexpr; see class warning.
         */
        [[nodiscard]] static std::expected<std::filesystem::path, parse_error> parse(os_str value) {
            if (value.empty())
                return std::unexpected(
                        parse_error{.kind      = parse_error_kind::empty_value,
                                    .input     = value,
                                    .type_name = detail::parsed_type_name<std::filesystem::path>,
                                    .reason    = "path cannot be empty"});
            return std::filesystem::path(value.to_native());
        }

        /**
         * \brief No enumerable domain.
         * \return An empty span, always.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };

    // -----------------------------------------------------------------------
    // Enumerations
    // -----------------------------------------------------------------------

    namespace detail {

        /**
         * \brief How many enumerators \p E declares.
         * \tparam E An enumeration type.
         */
        template<class E>
            requires std::is_enum_v<E>
        [[nodiscard]] consteval std::size_t enumerator_count() {
            return std::meta::enumerators_of(^^E).size();
        }

        /**
         * \brief Command-line spellings of every enumerator of \p E (declaration order).
         * \tparam E Enumeration. \tparam N enumerator_count<E>().
         * \return possible_values in static storage (name from value attr, else kebab rename).
         * \warning Duplicate spellings abort constant evaluation.
         */
        template<class E, std::size_t N>
            requires std::is_enum_v<E>
        [[nodiscard]] consteval std::array<possible_value, N> enum_values_of() {
            const std::vector<std::meta::info> enumerators = std::meta::enumerators_of(^^E);
            std::array<possible_value, N> values{};

            // Raw loop: the output is a fixed-extent std::array (nothing else reaches
            // `std::define_static_array` or a possible_values() span), and neither
            // `ranges::to` nor `ranges::copy` can size one from a lazily transformed
            // range without an intermediate transient allocation.
            for (std::size_t i = 0; i < N; ++i) {
                const value_attr attribute = meta::annotation_or<value_attr>(enumerators[i]);
                const std::string spelling =
                        attribute.name.empty()
                                ? rename(std::meta::identifier_of(enumerators[i]), naming::kebab)
                                : std::string(attribute.name.view());
                values[i] = make_possible_value(spelling, attribute.help.view())
                                    .with_hide(attribute.hide);
            }

            for (std::size_t i = 0; i < N; ++i) {
                for (std::size_t j = i + 1; j < N; ++j) {
                    if (values[i].get_name() == values[j].get_name()) std::abort();
                }
            }
            return values;
        }

        /**
         * \brief The enumerator constants of \p E, in declaration order.
         * \tparam E An enumeration type.
         * \tparam N enumerator_count<E>().
         * \note Index-aligned with enum_values_of(): position `i` in one is the
         *       spelling of position `i` in the other, which is the whole lookup.
         */
        template<class E, std::size_t N>
            requires std::is_enum_v<E>
        [[nodiscard]] consteval std::array<E, N> enum_constants_of() {
            const std::vector<std::meta::info> enumerators = std::meta::enumerators_of(^^E);
            std::array<E, N> constants{};
            for (std::size_t i = 0; i < N; ++i)
                constants[i] = std::meta::extract<E>(enumerators[i]);
            return constants;
        }

        /**
         * \brief enum_values_of() memoized, with static storage duration.
         * \tparam E An enumeration type.
         */
        template<class E>
            requires std::is_enum_v<E>
        inline constexpr auto enum_values = enum_values_of<E, enumerator_count<E>()>();

        /**
         * \brief enum_constants_of() memoized, with static storage duration.
         * \tparam E An enumeration type.
         */
        template<class E>
            requires std::is_enum_v<E>
        inline constexpr auto enum_constants = enum_constants_of<E, enumerator_count<E>()>();

    }  // namespace detail

    /**
     * \brief Parse any enum via reflection (no ValueEnum opt-in; value attrs only override).
     * \tparam E Any enumeration type.
     * \note Hidden values still match and appear in parse_error::possible; help filters them.
     * \warning Empty enums (e.g. std::byte) accept the specialization but reject every input.
     */
    template<class E>
        requires std::is_enum_v<E>
    struct value_parser<E> {
        /**
         * \brief Convert \p value to an `E`.
         *
         * \param value       The raw bytes from the command line.
         * \param ignore_case Fold ASCII case while matching, i.e. `Arg::ignore_case`.
         * \return The enumerator, or an error carrying every accepted spelling.
         *
         * \note The default argument is what makes this satisfy both clapp::parsable
         *       (one argument) and clapp::case_insensitively_parsable (two) with a
         *       single function.
         */
        [[nodiscard]] static constexpr std::expected<E, parse_error>
        parse(os_str value, bool ignore_case = false) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();

            const auto rejected = [&](parse_error_kind kind, std::string_view why) {
                return std::unexpected(parse_error{.kind      = kind,
                                                   .input     = value,
                                                   .type_name = detail::parsed_type_name<E>,
                                                   .reason    = why,
                                                   .possible  = possible_values(),
                                                   .encoding = text.has_value() ? invalid_encoding{}
                                                                                : text.error()});
            };

            // clap reports a non-UTF-8 enum value as an *invalid value* carrying the
            // possible list, not as an encoding error (`EnumValueParser::parse_ref`
            // calls `Error::invalid_value` on the `to_str()` failure). The list is the
            // useful half of that message, so the kind stays invalid_utf8 while the
            // list travels along with it.
            if (!text.has_value())
                return rejected(parse_error_kind::invalid_utf8, text.error().message());

            // Raw loop: the index found in the spelling table is the index used in the
            // constant table, so what is wanted is a position, not an iterator.
            for (std::size_t i = 0; i < detail::enum_values<E>.size(); ++i) {
                if (detail::enum_values<E>[i].matches(text.value(), ignore_case))
                    return detail::enum_constants<E>[i];
            }

            return rejected(parse_error_kind::invalid_value,
                            "value is not one of the accepted values");
        }

        /**
         * \brief Every spelling this enumeration answers to, in declaration order.
         * \return A span over static storage; hidden values included.
         */
        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return std::span<const possible_value>{detail::enum_values<E>};
        }
    };

    // -----------------------------------------------------------------------
    // Uniform entry points
    // -----------------------------------------------------------------------

    /**
     * \brief Parse \p value into `T`, applying ignore_case when supported.
     * \tparam T Target type. \param value Command-line bytes.
     * \param ignore_case ASCII fold; ignored if T does not support it.
     * \return The value or parse_error.
     */
    template<parsable T>
    [[nodiscard]] constexpr std::expected<T, parse_error> parse_value(os_str value,
                                                                      bool ignore_case = false) {
        if constexpr (case_insensitively_parsable<T>) {
            return value_parser<T>::parse(value, ignore_case);
        } else {
            static_cast<void>(ignore_case);
            return value_parser<T>::parse(value);
        }
    }

    /**
     * \brief The values a `T` accepts, or an empty span when it does not enumerate.
     *
     * \tparam T The target type.
     * \return A span over static storage.
     * \note Empty is the honest answer for a numeric or textual type, and callers
     *       treat it as "no `[possible values: ...]` line".
     */
    template<parsable T>
    [[nodiscard]] constexpr std::span<const possible_value> possible_values_of() noexcept {
        if constexpr (enumerable_parser<T>) {
            return value_parser<T>::possible_values();
        } else {
            return {};
        }
    }

    // -----------------------------------------------------------------------
    // Type erasure without virtual functions
    // -----------------------------------------------------------------------

    /**
     * \brief What clapp::any_value must additionally satisfy to be storable.
     *
     * \tparam T Candidate value type.
     * \note Splitting this out of parser_vtable_for's constraint gives a diagnostic
     *       that names the missing half — "not clapp::parsable" and "not
     *       clapp::any_storable" are different mistakes with different fixes.
     */
    template<class T>
    concept erasable_parsable = parsable<T> && any_storable<T>;

    /**
     * \brief Non-virtual dispatch table stored on an arg (three function pointers).
     * \note Structural for .rodata. #parse is runtime-only (any_value type erasure);
     *       the table pointers themselves are compile-time constants.
     */
    struct parser_vtable {
        /**
         * \brief Parse one value into a type-erased result.
         * \note The `ignore_case` parameter is always present here even though only
         *       enumerations use it. A second table entry for the case-folding parsers
         *       would make every call site ask which kind it holds, which is exactly
         *       the knowledge type erasure exists to remove.
         */
        std::expected<any_value, parse_error> (*parse)(os_str value, bool ignore_case);

        /** \brief The accepted values, or an empty span when the type does not enumerate. */
        std::span<const possible_value> (*possible_values)();

        /**
         * \brief The target type's spelling, for diagnostics. Equals
         *        `any_id::of<T>().name()` for the same `T`.
         */
        std::string_view (*type_name)();
    };

    /**
     * \brief The dispatch table for \p T.
     *
     * \tparam T A clapp::erasable_parsable type.
     *
     * \note One object per `T`, with static storage duration, so `&parser_vtable_for<T>`
     *       is a constant expression and is the same address in every translation unit.
     * \note Each entry is a captureless lambda put through unary `+`. The conversion to
     *       a function pointer is `constexpr`, so this initializer is constant even for
     *       a `T` whose own `parse` is not — which is how `float` and
     *       `std::filesystem::path` reach the table at all.
     */
    template<erasable_parsable T>
    inline constexpr parser_vtable parser_vtable_for{
            .parse = +[](os_str value, bool ignore_case) -> std::expected<any_value, parse_error> {
                return parse_value<T>(value, ignore_case).transform([](T&& parsed) {
                    return any_value(std::in_place_type<T>, std::move(parsed));
                });
            },
            .possible_values = +[]() { return possible_values_of<T>(); },
            .type_name       = +[]() { return detail::parsed_type_name<T>; },
    };

    /**
     * \brief A pointer to \p T 's dispatch table, for storing in an clapp::arg.
     *
     * \tparam T A clapp::erasable_parsable type.
     * \return The address of parser_vtable_for<T>; never null, always the same.
     *
     * \note `constexpr` rather than `consteval` so that a command tree assembled at
     *       runtime — the builder-only path described in ADR-0005 — can call it too.
     *       Nothing about the result differs between the two.
     */
    template<erasable_parsable T>
    [[nodiscard]] constexpr const parser_vtable* parser_for() noexcept {
        return &parser_vtable_for<T>;
    }

    namespace detail {

        /**
         * Compile-time contract: parser_vtable must stay structural, by value *and*
         * through a pointer, or an clapp::arg holding one cannot reach `.rodata`.
         * Catching it here names this file; the same mistake found by
         * `std::define_static_array` several layers up names neither the type nor the
         * offending member.
         */
        template<parser_vtable>
        struct parser_vtable_probe {};

        template<const parser_vtable*>
        struct parser_vtable_pointer_probe {};

        /** \brief Proof that clapp::parser_vtable is a structural type. */
        using parser_vtable_is_structural = parser_vtable_probe<parser_vtable{}>;
        /** \brief Proof that a parser-table pointer is a structural template argument. */
        using parser_vtable_pointer_is_structural = parser_vtable_pointer_probe<parser_for<int>()>;

        /**
         * \warning Do not static_assert on parser_for<T>() pointer equality here: under
         *          -fsanitize=undefined GCC 16 will not fold cross-object pointer
         *          compares in consteval, and this header is near-universal. Stability
         *          and distinctness are checked at runtime in value_parser_test.cpp.
         */

    }  // namespace detail

}  // namespace clapp
