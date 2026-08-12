/**
 * \file
 * \brief clapp::styled_str and clapp::styled_span — semantic (style_class, text) fragments.
 */

#pragma once

#include <clapp/builder/styling.hpp>
#include <clapp/util/str.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {

    /**
     * \brief One run of text carrying a single clapp::style_class.
     *
     * Element type of clapp::styled_str. Public aggregate so tests can assert fragment
     * sequences, not only flattened text.
     */
    struct styled_span {
        /** \brief Semantic role of this run. See clapp::style_class. */
        style_class class_ = style_class::plain;

        /**
         * \brief Owned bytes; no escape sequences.
         *
         * \note Owning so a styled_str can be returned without a lifetime rule. Contrast
         *       clapp::cow_str, which borrows (error context mostly names `.rodata`).
         */
        std::string text{};

        /** \brief Same class and same bytes. */
        [[nodiscard]] constexpr bool operator==(const styled_span&) const = default;
    };

    /**
     * \brief Styled message: sequence of clapp::styled_span, no ANSI bytes.
     *
     * Return type of error/help/usage/version rendering. Colour is applied at the
     * output edge (clapp::styles / clapp::color_choice); this type never sees either.
     *
     * \code
     *     clapp::styled_str message;
     *     message.push(clapp::style_class::error, "error:")
     *            .push_plain(" unexpected argument ")
     *            .push(clapp::style_class::invalid, "--verbose");
     * \endcode
     *
     * \note Members are `constexpr`; storage is a `std::vector`, so the type can be
     *       built in a constant expression but cannot itself be a `constexpr` variable
     *       (ADR-0005 transient allocation).
     */
    class styled_str {
    public:
        /** \brief An empty message. */
        constexpr styled_str() = default;

        /**
         * \brief A message consisting of one unstyled run.
         * \param plain_text The text; an empty view yields an empty styled_str.
         */
        constexpr explicit styled_str(std::string_view plain_text) {
            push(style_class::plain, plain_text);
        }

        /**
         * \brief A message consisting of one run of \p cls.
         * \param cls  The class the text carries.
         * \param text The text; an empty view yields an empty styled_str.
         */
        constexpr styled_str(style_class cls, std::string_view text) { push(cls, text); }

        /**
         * \brief Append \p text as a run of \p cls.
         * \param cls  Class the text carries.
         * \param text Bytes to append.
         * \return `*this`, for chaining.
         * \note Empty text is a no-op; same-class appends merge so `operator==` compares
         *       content, not assembly order.
         */
        constexpr styled_str& push(style_class cls, std::string_view text) {
            if (text.empty()) return *this;
            if (!spans_.empty() && spans_.back().class_ == cls) {
                detail::append_bytes(spans_.back().text, text);
                return *this;
            }
            styled_span fragment{.class_ = cls, .text = {}};
            detail::append_bytes(fragment.text, text);
            spans_.push_back(std::move(fragment));
            return *this;
        }

        /**
         * \brief Append \p text as ordinary body text.
         * \param text The bytes to append.
         * \return `*this`, for chaining.
         */
        constexpr styled_str& push_plain(std::string_view text) {
            return push(style_class::plain, text);
        }

        /**
         * \brief Append \p value in decimal as a run of \p cls.
         * \param cls   The class the number carries.
         * \param value The number to spell.
         * \return `*this`, for chaining.
         */
        constexpr styled_str& push_decimal(style_class cls, std::ptrdiff_t value) {
            std::string digits;
            detail::append_decimal(digits, value);
            return push(cls, digits);
        }

        /**
         * \brief Append every fragment of \p other, preserving its classes.
         * \param other The message to append. Appending a message to itself is safe.
         * \return `*this`, for chaining.
         */
        constexpr styled_str& append(const styled_str& other) {
            // Copy the fragment list first: push() may reallocate spans_, and `other`
            // may be `*this`.
            const std::vector<styled_span> fragments = other.spans_;
            for (const styled_span& fragment : fragments) push(fragment.class_, fragment.text);
            return *this;
        }

        /**
         * \brief The fragments, in order.
         * \return A view of the storage; invalidated by any mutating call.
         */
        [[nodiscard]] constexpr std::span<const styled_span> spans() const noexcept {
            return spans_;
        }

        /** \brief Whether the message has no text at all. */
        [[nodiscard]] constexpr bool empty() const noexcept { return spans_.empty(); }

        /** \brief How many fragments the message has, after merging. */
        [[nodiscard]] constexpr std::size_t span_count() const noexcept { return spans_.size(); }

        /** \brief The total number of bytes of text, ignoring classes. */
        [[nodiscard]] constexpr std::size_t size() const noexcept {
            std::size_t total = 0;
            for (const styled_span& fragment : spans_) total += fragment.text.size();
            return total;
        }

        /** \brief Drop every fragment. */
        constexpr void clear() noexcept { spans_.clear(); }

        /**
         * \brief Flattened text with styling discarded.
         * \return Concatenation of every fragment (dumb terminal / pipe / snapshot).
         */
        [[nodiscard]] constexpr std::string to_string() const {
            std::string out;
            for (const styled_span& fragment : spans_) detail::append_bytes(out, fragment.text);
            return out;
        }

        /**
         * \brief Text of every fragment carrying \p cls, concatenated.
         * \param cls Class to collect.
         * \return Concatenation, or empty when no fragment carries \p cls.
         */
        [[nodiscard]] constexpr std::string text_of(style_class cls) const {
            std::string out;
            for (const styled_span& fragment : spans_) {
                if (fragment.class_ == cls) detail::append_bytes(out, fragment.text);
            }
            return out;
        }

        /**
         * \brief Whether the flattened text contains \p needle.
         *
         * \param needle The bytes to look for; matches across fragment boundaries.
         * \return `true` when present. An empty \p needle is always present.
         */
        [[nodiscard]] constexpr bool contains(std::string_view needle) const {
            const std::string flat = to_string();
            return std::string_view{flat}.find(needle) != std::string_view::npos;
        }

        /**
         * \brief Equality by fragment sequence (merged runs; empty pushes dropped).
         */
        [[nodiscard]] constexpr bool operator==(const styled_str&) const = default;

    private:
        std::vector<styled_span> spans_{};
    };

    namespace detail {

        /** \brief Verify that adjacent spans of one class are coalesced. */
        consteval bool styled_str_merges_adjacent_runs() {
            styled_str split;
            split.push(style_class::literal, "--he").push(style_class::literal, "lp");
            const styled_str whole{style_class::literal, "--help"};
            return split == whole && split.span_count() == 1;
        }

        static_assert(styled_str_merges_adjacent_runs(),
                      "clapp: styled_str must merge adjacent runs of one class, or "
                      "operator== compares assembly order instead of content.");

        /** \brief Verify that inserting empty text does not create a span. */
        consteval bool styled_str_drops_empty_pushes() {
            styled_str message;
            message.push(style_class::invalid, "").push_plain("");
            return message.empty() && message == styled_str{};
        }

        static_assert(styled_str_drops_empty_pushes());

        /** \brief Verify decimal formatting for zero, positive, and negative values. */
        consteval bool append_decimal_round_trips() {
            std::string out;
            append_decimal(out, 0);
            append_decimal(out, 42);
            append_decimal(out, -7);
            return out == std::string_view{"042-7"};
        }

        static_assert(append_decimal_round_trips());

    }  // namespace detail

}  // namespace clapp
