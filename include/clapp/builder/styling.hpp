/**
 * \file
 * \brief Help/error style vocabulary: style_class, style, styles palette, color_choice.
 */

#pragma once

#include <clapp/detail/std_meta.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/util/str.hpp>

#include <array>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <optional>
#include <string_view>
#include <utility>

namespace clapp {
    /**
     * \brief Sixteen ANSI colours as palette indices 0–15 (renderer maps to SGR codes).
     */
    enum class ansi_color : std::uint8_t {
        black = 0, /**< ANSI black. */
        red = 1, /**< ANSI red. */
        green = 2, /**< ANSI green. */
        yellow = 3, /**< ANSI yellow. */
        blue = 4, /**< ANSI blue. */
        magenta = 5, /**< ANSI magenta. */
        cyan = 6, /**< ANSI cyan. */
        white = 7, /**< ANSI white. */
        bright_black = 8, /**< Bright ANSI black, commonly rendered as gray. */
        bright_red = 9, /**< Bright ANSI red. */
        bright_green = 10, /**< Bright ANSI green. */
        bright_yellow = 11, /**< Bright ANSI yellow. */
        bright_blue = 12, /**< Bright ANSI blue. */
        bright_magenta = 13, /**< Bright ANSI magenta. */
        bright_cyan = 14, /**< Bright ANSI cyan. */
        bright_white = 15, /**< Bright ANSI white. */
    };

    /** \brief Which colour space a clapp::color is expressed in. */
    enum class color_kind : std::uint8_t {
        unset, /**< No colour; the terminal's default is left alone. */
        ansi, /**< One of the sixteen clapp::ansi_color values. */
        ansi256, /**< An index into the 256-colour palette. */
        rgb, /**< A 24-bit true colour. */
    };

    /**
     * \brief Foreground, background or underline colour (structural; no optional/variant).
     *
     * color_kind::unset means "leave alone". Channels #r/#g/#b are reused: ansi and
     * ansi256 store the index in #r; rgb uses all three. Prefer as_ansi() / as_index().
     */
    struct color {
        /**
         * \name Structural storage
         * \{
         */
        color_kind kind = color_kind::unset; /**< Colour-space discriminator. */
        std::uint8_t r = 0; /**< Red channel, ANSI colour, or palette index. */
        std::uint8_t g = 0; /**< Green channel for an RGB colour. */
        std::uint8_t b = 0; /**< Blue channel for an RGB colour. */
        /** \} */

        /** \brief One of the sixteen ANSI colours. */
        [[nodiscard]] static constexpr color of(ansi_color which) noexcept {
            return {.kind = color_kind::ansi, .r = std::to_underlying(which)};
        }

        /** \brief An index into the 256-colour palette. */
        [[nodiscard]] static constexpr color indexed(std::uint8_t index) noexcept {
            return {.kind = color_kind::ansi256, .r = index};
        }

        /** \brief A 24-bit true colour. */
        [[nodiscard]] static constexpr color
        rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept {
            return {.kind = color_kind::rgb, .r = red, .g = green, .b = blue};
        }

        /** \brief Whether a colour was chosen at all. */
        [[nodiscard]] constexpr bool is_set() const noexcept { return kind != color_kind::unset; }

        /**
         * \brief The ANSI colour, when #kind is color_kind::ansi.
         * \return The colour, or `std::nullopt` for every other kind — including
         *         color_kind::ansi256, whose first sixteen indices *display* the same
         *         but are a different escape sequence.
         */
        [[nodiscard]] constexpr std::optional<ansi_color> as_ansi() const noexcept {
            if (kind != color_kind::ansi) return std::nullopt;
            return static_cast<ansi_color>(r);
        }

        /** \brief The palette index, when #kind is color_kind::ansi256. */
        [[nodiscard]] constexpr std::optional<std::uint8_t> as_index() const noexcept {
            if (kind != color_kind::ansi256) return std::nullopt;
            return r;
        }

        /** \brief Compare the colour kind and channels. */
        [[nodiscard]] constexpr bool operator==(const color &) const noexcept = default;
    };

    /**
     * \brief Bitmask of SGR text effects (anstyle bit positions). Named factories are
     *        static functions (class is incomplete for static data members of own type).
     * \note Prefer bold/underline for portability; other effects vary by terminal.
     */
    struct text_effects {
        /** \brief Structural storage: one bit per effect. */
        std::uint16_t bits = 0;

        /**
         * \name The twelve effects
         * \{
         */
        /** \brief No text effect. */
        [[nodiscard]] static constexpr text_effects none() noexcept { return {}; }
        /** \brief Bold or increased intensity. */
        [[nodiscard]] static constexpr text_effects bold() noexcept { return from_bit(0); }
        /** \brief Dimmed or decreased intensity. */
        [[nodiscard]] static constexpr text_effects dimmed() noexcept { return from_bit(1); }
        /** \brief Italic text. */
        [[nodiscard]] static constexpr text_effects italic() noexcept { return from_bit(2); }
        /** \brief Single underline. */
        [[nodiscard]] static constexpr text_effects underline() noexcept { return from_bit(3); }
        /** \brief Double underline. */
        [[nodiscard]] static constexpr text_effects double_underline() noexcept {
            return from_bit(4);
        }

        /** \brief Curly underline. */
        [[nodiscard]] static constexpr text_effects curly_underline() noexcept {
            return from_bit(5);
        }

        /** \brief Dotted underline. */
        [[nodiscard]] static constexpr text_effects dotted_underline() noexcept {
            return from_bit(6);
        }

        /** \brief Dashed underline. */
        [[nodiscard]] static constexpr text_effects dashed_underline() noexcept {
            return from_bit(7);
        }

        /** \brief Blinking text. */
        [[nodiscard]] static constexpr text_effects blink() noexcept { return from_bit(8); }
        /** \brief Swap foreground and background colours. */
        [[nodiscard]] static constexpr text_effects invert() noexcept { return from_bit(9); }
        /** \brief Hidden text. */
        [[nodiscard]] static constexpr text_effects hidden() noexcept { return from_bit(10); }
        /** \brief Struck-through text. */
        [[nodiscard]] static constexpr text_effects strikethrough() noexcept {
            return from_bit(11);
        }

        /** \} */

        /** \brief Whether no effect is enabled. */
        [[nodiscard]] constexpr bool is_plain() const noexcept { return bits == 0; }

        /** \brief Whether every effect in \p other is also in `*this`. */
        [[nodiscard]] constexpr bool contains(text_effects other) const noexcept {
            return (bits & other.bits) == other.bits;
        }

        /** \brief The union of `*this` and \p other. */
        [[nodiscard]] constexpr text_effects insert(text_effects other) const noexcept {
            return {.bits = static_cast<std::uint16_t>(bits | other.bits)};
        }

        /** \brief `*this` without any effect in \p other. */
        [[nodiscard]] constexpr text_effects remove(text_effects other) const noexcept {
            const auto keep = static_cast<std::uint16_t>(~other.bits);
            return {.bits = static_cast<std::uint16_t>(bits & keep)};
        }

        /** \brief `*this` with \p other inserted or removed according to \p enable. */
        [[nodiscard]] constexpr text_effects set(text_effects other, bool enable) const noexcept {
            return enable ? insert(other) : remove(other);
        }

        /** \brief Union. The idiomatic spelling of insert(). */
        [[nodiscard]] constexpr text_effects operator|(text_effects other) const noexcept {
            return insert(other);
        }

        /** \brief Intersection. */
        [[nodiscard]] constexpr text_effects operator&(text_effects other) const noexcept {
            return {.bits = static_cast<std::uint16_t>(bits & other.bits)};
        }

        /** \brief Compare the enabled-effect bitsets. */
        [[nodiscard]] constexpr bool operator==(const text_effects &) const noexcept = default;

    private:
        /**
         * Not a bit-shift at the call sites: `1u << n` is `unsigned int`, and narrowing
         * it inside a braced initializer is ill-formed, so the cast has to live once
         * here rather than twelve times above.
         */
        [[nodiscard]] static constexpr text_effects from_bit(unsigned position) noexcept {
            return {.bits = static_cast<std::uint16_t>(1u << position)};
        }
    };

    /**
     * \brief How one span is drawn: colours plus effects (anstyle Style). Default is plain.
     */
    struct style {
        /**
         * \name Structural storage
         * \{
         */
        color fg{}; /**< Foreground colour. */
        color bg{}; /**< Background colour. */
        color underline_color{}; /**< Underline colour; an extension, widely ignored. */
        text_effects effects{}; /**< Bold, underline, and the rest. */
        /** \} */

        /** \brief Whether this style would emit nothing at all. */
        [[nodiscard]] constexpr bool is_plain() const noexcept {
            return !fg.is_set() && !bg.is_set() && !underline_color.is_set() && effects.is_plain();
        }

        /** \brief A copy with a different foreground colour. */
        [[nodiscard]] constexpr style with_fg(color which) const noexcept {
            style copy = *this;
            copy.fg = which;
            return copy;
        }

        /** \brief A copy with a different background colour. */
        [[nodiscard]] constexpr style with_bg(color which) const noexcept {
            style copy = *this;
            copy.bg = which;
            return copy;
        }

        /** \brief A copy with a different underline colour. */
        [[nodiscard]] constexpr style with_underline_color(color which) const noexcept {
            style copy = *this;
            copy.underline_color = which;
            return copy;
        }

        /**
         * \brief A copy whose effects are replaced wholesale by \p which.
         * \note Replaces rather than adds; use the named helpers below to add one.
         */
        [[nodiscard]] constexpr style with_effects(text_effects which) const noexcept {
            style copy = *this;
            copy.effects = which;
            return copy;
        }

        /**
         * \name Adding one effect
         * Each returns a copy with that effect inserted, leaving the others alone.
         * \{
         */
        /** \brief Return a copy with bold enabled. */
        [[nodiscard]] constexpr style bold() const noexcept {
            return with_effects(effects | text_effects::bold());
        }

        /** \brief Return a copy with dimmed intensity enabled. */
        [[nodiscard]] constexpr style dimmed() const noexcept {
            return with_effects(effects | text_effects::dimmed());
        }

        /** \brief Return a copy with italic enabled. */
        [[nodiscard]] constexpr style italic() const noexcept {
            return with_effects(effects | text_effects::italic());
        }

        /** \brief Return a copy with underline enabled. */
        [[nodiscard]] constexpr style underline() const noexcept {
            return with_effects(effects | text_effects::underline());
        }

        /** \brief Return a copy with strikethrough enabled. */
        [[nodiscard]] constexpr style strikethrough() const noexcept {
            return with_effects(effects | text_effects::strikethrough());
        }

        /** \} */

        /** \brief Compare colours and enabled effects. */
        [[nodiscard]] constexpr bool operator==(const style &) const noexcept = default;
    };

    /**
     * \brief Semantic label for a help/error span; styles maps it to a style at render time.
     */
    enum class style_class : std::uint8_t {
        /**
         * Ordinary body text. Enumerator zero, so a value-initialized style_class is
         * the neutral one; its palette entry is an unstyled clapp::style unless a
         * front-end deliberately sets a body style.
         */
        plain,
        /** A section heading, e.g. `Options:` or an `help_heading`. */
        header,
        /** The `Usage:` heading. */
        usage,
        /** Literal command-line syntax the user could type back: `--help`, `add`. */
        literal,
        /** A stand-in for something the user supplies, such as `FILE` or `VALUE`. */
        placeholder,
        /** Suggested, correct usage — the "did you mean" candidate. */
        valid,
        /** The offending input in an error message. */
        invalid,
        /** The `error:` heading itself. */
        error,
        /** A parenthetical annotation, e.g. `[default: false]`. */
        context,
        /**
         * The value inside such an annotation, e.g. the `false` in `[default: false]`.
         * Falls back to `context` unless set; see styles::get().
         */
        context_value,
    };

    /** \brief How many clapp::style_class values there are. */
    inline constexpr std::size_t style_class_count = 10;

    /**
     * \brief Palette: one style per style_class (clap Styles). Structural for command_spec.
     */
    struct styles {
        /**
         * \name Structural storage
         * \{
         */

        /** One entry per clapp::style_class, indexed by its underlying value. */
        std::array<style, style_class_count> by_class{};

        /**
         * Whether `context_value` was set explicitly. When false, get() answers the
         * `context` style for it — clap models this as `Option<Style>`, which is not a
         * structural type here.
         */
        bool context_value_set = false;

        /** \} */

        /** \brief No styling at all. clap's `Styles::plain()`. */
        [[nodiscard]] static constexpr styles plain() noexcept { return {}; }

        /**
         * \brief Default palette (clap Styles::styled): header/usage bold+underline,
         *        literal bold, error red+bold, valid green, invalid yellow; rest plain.
         * \note No backgrounds; portable bold/underline/base colours only.
         */
        [[nodiscard]] static constexpr styles styled() noexcept {
            styles result{};
            result.at(style_class::header) = style{}.bold().underline();
            result.at(style_class::usage) = style{}.bold().underline();
            result.at(style_class::literal) = style{}.bold();
            result.at(style_class::error) = style{}.with_fg(color::of(ansi_color::red)).bold();
            result.at(style_class::valid) = style{}.with_fg(color::of(ansi_color::green));
            result.at(style_class::invalid) = style{}.with_fg(color::of(ansi_color::yellow));
            return result;
        }

        /**
         * \brief The style for \p which.
         *
         * \param which The semantic class of the span being rendered.
         * \return Its style, with `context_value` falling back to `context` when it was
         *         never set explicitly.
         */
        [[nodiscard]] constexpr style get(style_class which) const noexcept {
            if (which == style_class::context_value && !context_value_set) {
                return by_class[std::to_underlying(style_class::context)];
            }
            return by_class[std::to_underlying(which)];
        }

        /**
         * \brief A copy with \p which restyled.
         *
         * \param which The class to restyle.
         * \param how Its new style.
         * \note Setting `context_value` also records that it was set, which is what
         *       stops it from following `context` from then on.
         */
        [[nodiscard]] constexpr styles with(style_class which, style how) const noexcept {
            styles copy = *this;
            copy.at(which) = how;
            if (which == style_class::context_value) copy.context_value_set = true;
            return copy;
        }

        /** \brief Compare every palette entry and fallback state. */
        [[nodiscard]] constexpr bool operator==(const styles &) const noexcept = default;

    private:
        /**
         * Mutable access used by the factories and by with(). Private on purpose: a
         * caller that reaches past get() would miss the `context_value` fallback.
         */
        [[nodiscard]] constexpr style &at(style_class which) noexcept {
            return by_class[std::to_underlying(which)];
        }
    };

    /**
     * \brief Whether output should be coloured (clap ColorChoice).
     * \note auto_ is enumerator zero (keyword clash); reflection spells it `auto`.
     */
    enum class color_choice : std::uint8_t {
        auto_, /**< Colour when the stream is a terminal that supports it. */
        always, /**< Colour regardless of where the output goes. */
        never, /**< Never colour. */
    };

    /** \brief Every color_choice, for value-enum expansion. */
    inline constexpr std::array<color_choice, 3> all_color_choices{
        color_choice::auto_, color_choice::always, color_choice::never
    };

    /** \brief The spelling of \p choice, with `auto_` rendered as `auto`. */
    [[nodiscard]] constexpr std::string_view name_of(color_choice choice) noexcept {
        switch (choice) {
            case color_choice::auto_:
                return "auto";
            case color_choice::always:
                return "always";
            case color_choice::never:
                return "never";
        }
        return {};
    }

    /**
     * \brief Read a color_choice back from its spelling.
     * \param text The spelling; compared ASCII-case-insensitively, as clap does.
     * \return The choice, or `std::nullopt` when \p text names none.
     */
    [[nodiscard]] constexpr std::optional<color_choice>
    parse_color_choice(std::string_view text) noexcept {
        for (const color_choice choice: all_color_choices) {
            if (detail::equals_ignore_ascii_case(text, name_of(choice))) return choice;
        }
        return std::nullopt;
    }

    /**
     * \brief Probed environment facts for colour decisions (filled by output layer I/O).
     *
     * Sources (anstyle_query): stream_is_terminal ← isatty; no_color / clicolor_force ←
     * non-empty env; clicolor ← unset/infer, "0"/no, else yes; term_supports_color ←
     * TERM set and not "dumb"; is_ci ← CI present.
     *
     * \warning NO_COLOR= (set but empty) means nothing, not disable — require non-empty.
     *          Same for CLICOLOR_FORCE. CI counts by presence alone.
     */
    struct color_env {
        bool stream_is_terminal = false; /**< Whether the destination is an interactive terminal. */
        bool no_color = false; /**< Whether a non-empty `NO_COLOR` variable is present. */
        bool clicolor_force = false; /**< Whether non-empty `CLICOLOR_FORCE` requests colour. */
        tri clicolor = tri::infer; /**< Parsed state of the `CLICOLOR` variable. */
        bool term_supports_color = false; /**< Whether `TERM` names a colour-capable terminal. */
        bool is_ci = false; /**< Whether the `CI` variable is present. */
    };

    /**
     * \brief Resolve \p choice against \p env to always or never (anstream choice order).
     * \param choice User/command preference.
     * \param env Probed environment.
     * \return always or never, never auto_.
     * \note Precedence: explicit always/never; then NO_COLOR; CLICOLOR_FORCE;
     *       CLICOLOR=0; else terminal && (TERM ok | CLICOLOR yes | CI).
     */
    [[nodiscard]] constexpr color_choice resolve_color_choice(color_choice choice,
                                                              const color_env &env) noexcept {
        if (choice != color_choice::auto_) return choice;
        if (env.no_color) return color_choice::never;
        if (env.clicolor_force) return color_choice::always;
        if (env.clicolor == tri::no) return color_choice::never;
        const bool supported = env.term_supports_color || env.clicolor == tri::yes || env.is_ci;
        return (env.stream_is_terminal && supported) ? color_choice::always : color_choice::never;
    }

    /** \brief Whether to emit escape sequences. resolve_color_choice() as a predicate. */
    [[nodiscard]] constexpr bool should_style(color_choice choice, const color_env &env) noexcept {
        return resolve_color_choice(choice, env) == color_choice::always;
    }

    namespace detail {
        /**
         * Compile-time contract: the palette has to stay structural, or it cannot be
         * embedded in a `command_spec` that reaches static storage.
         */
        template<styles>
        struct styles_structural_probe {
        };

        /** \brief Instantiation proving that clapp::styles is structural. */
        using styles_are_structural = styles_structural_probe<styles::styled()>;

        static_assert(std::meta::enumerators_of(^^style_class).size() == style_class_count,
                      "clapp: style_class_count must match clapp::style_class. A class "
                      "added without widening the count indexes past the end of "
                      "styles::by_class.");

        static_assert(std::meta::enumerators_of(^^color_choice).size() == all_color_choices.size(),
                      "clapp: all_color_choices must list every clapp::color_choice.");

        static_assert(style_class{} == style_class::plain &&
                      styles::plain().get(style_class{}) == style{},
                      "clapp: the neutral style_class must be enumerator zero and must "
                      "render as no styling.");

        // The default palette must stay legible: nothing in it may set a background,
        // which is the one thing guaranteed to clash with a user's colour scheme.
        /** \brief Verify that the default palette never chooses a background colour. */
        consteval bool default_palette_sets_no_background() {
            for (const style &s: styles::styled().by_class) {
                if (s.bg.is_set()) return false;
            }
            return true;
        }

        static_assert(default_palette_sets_no_background());

        // The precedence in resolve_color_choice() is the whole content of this header's
        // runtime behaviour, so it is pinned here rather than only in the unit test.
        static_assert(resolve_color_choice(color_choice::always, color_env{.no_color = true}) ==
                      color_choice::always);
        static_assert(resolve_color_choice(color_choice::auto_,
                                           color_env{.no_color = true, .clicolor_force = true}) ==
                      color_choice::never);
        static_assert(resolve_color_choice(color_choice::auto_,
                                           color_env{
                                               .stream_is_terminal = false,
                                               .clicolor_force = true
                                           }) ==
                      color_choice::always);
    } // namespace detail
} // namespace clapp
