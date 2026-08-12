/**
 * \file
 * \brief One command-line argument, classified: clapp::parsed_arg.
 */

#pragma once

#include <clapp/lex/os_str.hpp>
#include <clapp/lex/short_flags.hpp>

#include <compare>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace clapp {

    /**
     * \brief One command-line argument, non-owning (`clap_lex::ParsedArg`).
     * \note Predicates and to_long() are byte-wise; decoding only in to_value/display.
     * \warning Like os_str: built from a temporary dangles with no diagnostic.
     */
    class parsed_arg {
    public:
        /**
         * \brief to_long() result: UTF-8 name (or raw bytes) plus optional `=` value.
         */
        using long_flag = std::pair<std::expected<std::string_view, os_str>, std::optional<os_str>>;

        /** \brief Empty argument `""`. */
        constexpr parsed_arg() noexcept = default;

        /**
         * \brief View \p raw as one whole argument (explicit).
         * \param raw Argument as arrived, including `-` / `--`.
         */
        constexpr explicit parsed_arg(os_str raw) noexcept : raw_(raw) {}

        /**
         * \name Shape predicates
         * Byte-wise; mutually exclusive except where noted.
         * \{
         */

        /** \brief Whether the argument is `""`. */
        [[nodiscard]] constexpr bool is_empty() const noexcept { return raw_.empty(); }

        /**
         * \brief Whether the argument is exactly `-` (stdio stand-in, not a short).
         */
        [[nodiscard]] constexpr bool is_stdio() const noexcept { return raw_ == os_str{"-"}; }

        /**
         * \brief Whether the argument is exactly `--` (end-of-options; not a long).
         * \note `---` is a long option — see to_long().
         */
        [[nodiscard]] constexpr bool is_escape() const noexcept { return raw_ == os_str{"--"}; }

        /**
         * \brief Whether the argument has negative-number shape (not a parse).
         * \return `false` if not UTF-8 or does not start with `-`.
         * \warning `parsed_arg{"-"}` is **true** (empty after `-` is a number in clap).
         *          Test is_stdio() first if that matters.
         */
        [[nodiscard]] constexpr bool is_negative_number() const {
            const std::expected<std::string_view, invalid_encoding> text = raw_.to_string_view();
            if (!text.has_value()) return false;
            const std::string_view value = *text;
            if (!value.starts_with('-')) return false;
            return detail::is_number(value.substr(1));
        }

        /**
         * \brief Whether the argument can be read as a long option.
         * \return `true` for `--…` except bare `--`.
         */
        [[nodiscard]] constexpr bool is_long() const noexcept {
            return raw_.starts_with(os_str{"--"}) && !is_escape();
        }

        /**
         * \brief Whether the argument can be read as a short-option cluster.
         * \return `true` for single `-` prefix other than bare `-`.
         * \note `-1` is both short and negative-number; the command breaks the tie.
         */
        [[nodiscard]] constexpr bool is_short() const noexcept {
            return raw_.starts_with('-') && !is_stdio() && !raw_.starts_with(os_str{"--"});
        }

        /** \} */

        /**
         * \name Conversions
         * \{
         */

        /**
         * \brief Read as a long option; splits at the first `=`.
         * \return `nullopt` unless is_long(); else `{name, value}` (`nullopt` value if
         *         no `=`; empty os_str if ends with `=`). Keep `--k` ≠ `--k=`.
         * \note `---` / `--=v` stay long so diagnostics say "unexpected argument".
         */
        [[nodiscard]] constexpr std::optional<long_flag> to_long() const noexcept {
            const std::optional<os_str> remainder = raw_.strip_prefix(os_str{"--"});
            if (!remainder.has_value()) return std::nullopt;

            const os_str body = *remainder;
            if (body.empty()) return std::nullopt;  // the `--` escape

            const std::optional<std::pair<os_str, os_str>> split = body.split_once('=');
            const os_str name = split.has_value() ? split->first : body;
            const std::optional<os_str> value =
                    split.has_value() ? std::optional<os_str>{split->second} : std::nullopt;

            const std::expected<std::string_view, invalid_encoding> decoded = name.to_string_view();
            if (decoded.has_value())
                return long_flag{std::expected<std::string_view, os_str>{*decoded}, value};
            return long_flag{std::expected<std::string_view, os_str>{std::unexpect, name}, value};
        }

        /**
         * \brief Read as a short-option cluster (body after `-`).
         * \return Cursor, or `nullopt` for `-`, `--`, `--long`, non-`-`.
         * \note Copy the cursor for lookahead.
         */
        [[nodiscard]] constexpr std::optional<short_flags> to_short() const noexcept {
            const std::optional<os_str> remainder = raw_.strip_prefix(os_str{"-"});
            if (!remainder.has_value()) return std::nullopt;

            const os_str body = *remainder;
            if (body.starts_with('-')) return std::nullopt;  // `--`, `--long`, `---`
            if (body.empty()) return std::nullopt;           // `-`, the stdio placeholder
            return short_flags{body};
        }

        /**
         * \brief Whole argument bytes as a value (no shape filter).
         * \warning May return a flag or escape; caller must know it is a value.
         */
        [[nodiscard]] constexpr os_str to_value_os() const noexcept { return raw_; }

        /**
         * \brief Argument as UTF-8 text value (zero-copy).
         * \return Text, or raw bytes on unexpected when not UTF-8.
         * \warning Same caveat as to_value_os().
         */
        [[nodiscard]] constexpr std::expected<std::string_view, os_str> to_value() const noexcept {
            const std::expected<std::string_view, invalid_encoding> decoded = raw_.to_string_view();
            if (decoded.has_value()) return *decoded;
            return std::unexpected(raw_);
        }

        /** \} */

        /**
         * \brief Lossy printable form (U+FFFD for bad sequences); not for keys/parse.
         */
        [[nodiscard]] constexpr std::string display() const { return raw_.to_string_lossy(); }

        /** \brief Byte-wise equality. */
        [[nodiscard]] constexpr bool operator==(const parsed_arg&) const noexcept = default;

        /** \brief Byte-wise ordering, unsigned and locale-independent. */
        [[nodiscard]] constexpr std::strong_ordering
        operator<=>(const parsed_arg& other) const noexcept {
            return raw_ <=> other.raw_;
        }

    private:
        os_str raw_{};
    };

}  // namespace clapp

namespace std {

    /** \brief Byte-wise hash (same as hashing `to_value_os()`). */
    template<>
    struct hash<clapp::parsed_arg> {
        /** \brief Hash argument bytes. */
        [[nodiscard]] std::size_t operator()(clapp::parsed_arg value) const noexcept {
            return std::hash<clapp::os_str>{}(value.to_value_os());
        }
    };

    /**
     * \brief Format via display() so terminals never see invalid UTF-8.
     */
    template<>
    struct formatter<clapp::parsed_arg> : formatter<std::string> {
        /**
         * \brief Write lossy rendering (width/fill from `formatter<std::string>`).
         */
        template<class Context>
        auto format(clapp::parsed_arg value, Context& context) const {
            return formatter<std::string>::format(value.display(), context);
        }
    };

}  // namespace std
