/**
 * \file
 * \brief Output edge: render_plain / render_ansi, and colour policy
 *        (probe_color_env / resolve_color). Sole emitter of `\033[` in the tree.
 *
 * Policy (env/TTY) and bytes (palette) stay separate so help content is comparable
 * without colour. SGR order matches anstyle 1.0.14: effects, fg, bg, underline colour —
 * each as its own sequence (colon sub-params cannot join a `;` list).
 */

#pragma once

#include <clapp/builder/styling.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/util/str.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <cstdio>   // IWYU pragma: keep
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#    include <io.h>
#else
#    include <unistd.h>
#endif

namespace clapp {

    // =======================================================================
    // Escape sequences
    // =======================================================================

    /**
     * \brief SGR reset to default appearance. After styled runs only; never after plain.
     */
    inline constexpr std::string_view ansi_reset = "\x1B[0m";

    namespace detail {

        /**
         * \brief SGR for each clapp::text_effects bit (0 = bold … 11 = strikethrough).
         * \note Twelve separate sequences: colon sub-params (`4:3` etc.) cannot join a
         *       `;` list without changing terminal behaviour.
         */
        inline constexpr std::array<std::string_view, 12> effect_escapes{
                "\x1B[1m",    // bold
                "\x1B[2m",    // dimmed
                "\x1B[3m",    // italic
                "\x1B[4m",    // underline
                "\x1B[21m",   // double_underline
                "\x1B[4:3m",  // curly_underline
                "\x1B[4:4m",  // dotted_underline
                "\x1B[4:5m",  // dashed_underline
                "\x1B[5m",    // blink
                "\x1B[7m",    // invert
                "\x1B[8m",    // hidden
                "\x1B[9m",    // strikethrough
        };

        /** \brief Which of a style's three colour slots is being rendered. */
        enum class color_layer : std::uint8_t {
            foreground, /**< SGR 30–37 / 90–97, or 38. */
            background, /**< SGR 40–47 / 100–107, or 48. */
            underline,  /**< SGR 58. An extension; widely ignored, never wrong. */
        };

        /**
         * \brief Append SGR for \p which as \p layer.
         * \param out Destination.
         * \param which Colour; color_kind::unset appends nothing.
         * \param layer Foreground, background, or underline.
         * \note ANSI underline has no per-colour code; promote to 256-palette `58;5;<n>`
         *       (anstyle) so clapp::color::of() underline colours are not lost.
         */
        constexpr void append_color(std::string& out, const color& which, color_layer layer) {
            switch (which.kind) {
            case color_kind::unset:
                return;

            case color_kind::ansi: {
                const bool bright       = which.r >= 8;
                const std::uint8_t base = bright ? static_cast<std::uint8_t>(which.r - 8) : which.r;
                switch (layer) {
                case color_layer::foreground:
                    append_bytes(out, bright ? "\x1B[9" : "\x1B[3");
                    append_decimal(out, base);
                    out.push_back('m');
                    return;
                case color_layer::background:
                    append_bytes(out, bright ? "\x1B[10" : "\x1B[4");
                    append_decimal(out, base);
                    out.push_back('m');
                    return;
                case color_layer::underline:
                    // No per-colour code exists; anstyle delegates to Ansi256.
                    append_bytes(out, "\x1B[58;5;");
                    append_decimal(out, which.r);
                    out.push_back('m');
                    return;
                }
                return;
            }

            case color_kind::ansi256: {
                switch (layer) {
                case color_layer::foreground:
                    append_bytes(out, "\x1B[38;5;");
                    break;
                case color_layer::background:
                    append_bytes(out, "\x1B[48;5;");
                    break;
                case color_layer::underline:
                    append_bytes(out, "\x1B[58;5;");
                    break;
                }
                append_decimal(out, which.r);
                out.push_back('m');
                return;
            }

            case color_kind::rgb: {
                switch (layer) {
                case color_layer::foreground:
                    append_bytes(out, "\x1B[38;2;");
                    break;
                case color_layer::background:
                    append_bytes(out, "\x1B[48;2;");
                    break;
                case color_layer::underline:
                    append_bytes(out, "\x1B[58;2;");
                    break;
                }
                append_decimal(out, which.r);
                out.push_back(';');
                append_decimal(out, which.g);
                out.push_back(';');
                append_decimal(out, which.b);
                out.push_back('m');
                return;
            }
            }
        }

        /**
         * \brief Append every SGR \p how asks for, in anstyle order.
         * \param out Destination.
         * \param how Style; plain appends nothing.
         */
        constexpr void append_style(std::string& out, const style& how) {
            for (std::size_t bit = 0; bit < effect_escapes.size(); ++bit) {
                const text_effects one{.bits = static_cast<std::uint16_t>(1u << bit)};
                if (how.effects.contains(one)) append_bytes(out, effect_escapes[bit]);
            }
            append_color(out, how.fg, color_layer::foreground);
            append_color(out, how.bg, color_layer::background);
            append_color(out, how.underline_color, color_layer::underline);
        }

    }  // namespace detail

    /**
     * \brief SGR prefix that enters \p how.
     * \param how Style to enter.
     * \return Escapes in anstyle order, or empty when `how.is_plain()`.
     * \note Public for incremental writers; pair with render_style_reset().
     */
    [[nodiscard]] constexpr std::string render_style(const style& how) {
        std::string out;
        detail::append_style(out, how);
        return out;
    }

    /**
     * \brief Reset that closes a run drawn in \p how.
     * \param how Style that was entered.
     * \return clapp::ansi_reset, or empty when `how.is_plain()`.
     * \note Plain runs emit neither enter nor reset, so plain palette matches colour-off.
     */
    [[nodiscard]] constexpr std::string_view render_style_reset(const style& how) noexcept {
        return how.is_plain() ? std::string_view{} : ansi_reset;
    }

    // =======================================================================
    // Rendering a message
    // =======================================================================

    /**
     * \brief \p message with styling discarded (no escapes added).
     * \param message Message to render.
     * \return Concatenation of every fragment's text (same as styled_str::to_string()).
     *
     * \warning **Does not strip either — stripping is upstream.** Producers
     *          (render_help / render_usage / render_version / error::render) end with
     *          strip_escapes(), so no clapp-produced styled_str carries an escape.
     *          This function must stay the identity on content: otherwise to_string(),
     *          render_plain(), and `render_ansi(m, styles::plain())` diverge. A
     *          stream-side strip would also let the wrapper see sequences (mid-sequence
     *          breaks). Caller-built styled_str may still hold escapes. 差异清单 #29.
     */
    [[nodiscard]] constexpr std::string render_plain(const styled_str& message) {
        return message.to_string();
    }

    /**
     * \brief \p message with \p palette applied as ANSI escapes.
     * \param message Message to render.
     * \param palette Style per style_class; styles::styled() is default, plain disables.
     * \return Rendered bytes.
     * \note At most one enter/reset per fragment; plain styles emit neither, so
     *       `render_ansi(m, styles::plain()) == render_plain(m)`.
     *
     * \warning Runs are not coalesced across classes. Adjacent fragments that resolve to
     *          the same style still reset and re-enter (`header` and `usage` are both
     *          bold+underline). Matches clap's per-piece `{style}{text}{reset}`; pinned.
     */
    [[nodiscard]] constexpr std::string render_ansi(const styled_str& message,
                                                    const styles& palette) {
        std::string out;
        for (const styled_span& fragment : message.spans()) {
            const style how = palette.get(fragment.class_);
            detail::append_style(out, how);
            detail::append_bytes(out, fragment.text);
            detail::append_bytes(out, render_style_reset(how));
        }
        return out;
    }

    /**
     * \brief render_ansi() or render_plain(), chosen by \p use_color.
     * \param message Message to render.
     * \param palette Used only when \p use_color is true.
     * \param use_color Already-resolved decision (see resolve_color()).
     * \return Rendered bytes.
     * \note Takes a `bool`, not color_choice, so the rendering half stays pure / consteval.
     */
    [[nodiscard]] constexpr std::string
    render(const styled_str& message, const styles& palette, bool use_color) {
        return use_color ? render_ansi(message, palette) : render_plain(message);
    }

    // =======================================================================
    // Colour policy: probing the environment
    // =======================================================================

    /**
     * \brief Callable: environment variable name → optional value.
     * \tparam F Accepts `string_view`; returns something convertible to
     *         `optional<string_view>` — nullopt = unset, view (possibly empty) = set.
     * \note nullopt vs empty is load-bearing: empty means nothing for NO_COLOR /
     *       CLICOLOR_FORCE, "yes" for CLICOLOR, and present for CI. Do not collapse them.
     */
    template<class F>
    concept env_lookup = requires(const F& probe, std::string_view name) {
        { probe(name) } -> std::convertible_to<std::optional<std::string_view>>;
    };

    namespace detail {

        /**
         * \brief Whether \p value is set and non-empty.
         * \note Uses `operator*` not `->`: libstdc++ `_GLIBCXX_ASSERTIONS` makes `->`
         *       non-constexpr.
         */
        [[nodiscard]] constexpr bool env_non_empty(const std::optional<std::string_view>& value) {
            if (!value.has_value()) return false;
            const std::string_view text = *value;
            return !text.empty();
        }

        /**
         * \brief env_lookup via `std::getenv` — sole colour-path reader of the process env.
         * \note Not constexpr (impure edge). getenv is not thread-safe against setenv.
         */
        struct getenv_lookup {
            /** \brief Read environment variable \p name, if it is set. */
            [[nodiscard]] std::optional<std::string_view> operator()(std::string_view name) const {
                // getenv() needs a NUL terminator that a string_view cannot promise.
                std::string key;
                append_bytes(key, name);
                const char* const found = std::getenv(key.c_str());
                if (found == nullptr) return std::nullopt;
                return std::string_view{found};
            }
        };

    }  // namespace detail

    /**
     * \brief Fill color_env from \p lookup and a known terminal answer (anstyle-query).
     *
     * | Field | Rule |
     * |-------|------|
     * | stream_is_terminal | \p is_terminal |
     * | no_color / clicolor_force | set **and non-empty** |
     * | clicolor | unset → infer; `"0"` → no; else yes |
     * | term_supports_color | TERM set and not `"dumb"` (Windows: unset ok) |
     * | is_ci | CI set, any value including empty |
     *
     * \tparam Lookup clapp::env_lookup.
     * \param is_terminal Whether the destination is a TTY (passed in to stay constexpr).
     * \param lookup Environment reader.
     * \return Probed facts for resolve_color().
     *
     * \warning **Empty vs set differs by variable.** `NO_COLOR=` / `CLICOLOR_FORCE=` mean
     *          nothing (must be non-empty). `CI=` means yes (presence alone). Do not test
     *          all three with `getenv(...) != nullptr`.
     *
     * \warning **`CLICOLOR_FORCE=0` forces colour ON.** Emptiness is tested, not zero;
     *          only `CLICOLOR` reads its value. Ported deliberately; pinned by a test.
     */
    template<env_lookup Lookup>
    [[nodiscard]] constexpr color_env probe_color_env(bool is_terminal, const Lookup& lookup) {
        color_env env{};
        env.stream_is_terminal = is_terminal;
        env.no_color           = detail::env_non_empty(lookup("NO_COLOR"));
        env.clicolor_force     = detail::env_non_empty(lookup("CLICOLOR_FORCE"));

        const std::optional<std::string_view> clicolor = lookup("CLICOLOR");
        env.clicolor = !clicolor.has_value() ? tri::infer : (*clicolor == "0" ? tri::no : tri::yes);

        const std::optional<std::string_view> term = lookup("TERM");
#if defined(_WIN32)
        env.term_supports_color = !term.has_value() || *term != "dumb";
#else
        env.term_supports_color = term.has_value() && *term != "dumb";
#endif

        env.is_ci = lookup("CI").has_value();
        return env;
    }

    /**
     * \brief Whether \p stream is a terminal.
     * \param stream Destination (`stdout`/`stderr`); null → false.
     * \return `isatty(fileno(stream))` (`_isatty`/`_fileno` on Windows).
     * \note One of two impure functions here (with getenv_lookup). Null checked before
     *       fileno (UB otherwise).
     */
    [[nodiscard]] inline bool stream_is_terminal(std::FILE* stream) noexcept {
        if (stream == nullptr) return false;
#if defined(_WIN32)
        const int descriptor = ::_fileno(stream);
        return descriptor >= 0 && ::_isatty(descriptor) != 0;
#else
        const int descriptor = ::fileno(stream);
        return descriptor >= 0 && ::isatty(descriptor) != 0;
#endif
    }

    /**
     * \brief Probe the real environment for \p stream (`getenv` + `isatty`).
     * \param stream Destination the colour decision is about.
     * \return Probed facts. Prefer the two-arg overload in tests.
     */
    [[nodiscard]] inline color_env probe_color_env(std::FILE* stream) {
        return probe_color_env(stream_is_terminal(stream), detail::getenv_lookup{});
    }

    // =======================================================================
    // Colour policy: the answer
    // =======================================================================

    /**
     * \brief Whether to emit escapes, given probed facts.
     * \param choice User/command request; always/never beat every env var.
     * \param env Probed environment.
     * \return true → render_ansi(), false → render_plain().
     * \note Precedence is resolve_color_choice() in styling.hpp.
     */
    [[nodiscard]] constexpr bool resolve_color(color_choice choice, const color_env& env) noexcept {
        return should_style(choice, env);
    }

    /**
     * \brief Whether to emit escapes when writing to \p stream.
     * \param choice User/command request.
     * \param stream Destination (`stdout`/`stderr`).
     * \return true → render_ansi(), false → render_plain().
     * \note Once per stream, not per message (stdout TTY / stderr pipe can differ).
     */
    [[nodiscard]] inline bool resolve_color(color_choice choice, std::FILE* stream) {
        return resolve_color(choice, probe_color_env(stream));
    }

    /**
     * \brief Render \p message the way \p stream should see it.
     * \param message Message to render.
     * \param palette Palette when colour is allowed.
     * \param choice User/command request.
     * \param stream Destination for the decision; **not** written to.
     * \return Bytes to write.
     * \note Shorthand for `render(..., resolve_color(choice, stream))`.
     */
    [[nodiscard]] inline std::string render_for_stream(const styled_str& message,
                                                       const styles& palette,
                                                       color_choice choice,
                                                       std::FILE* stream) {
        return render(message, palette, resolve_color(choice, stream));
    }

    namespace detail {

        /**
         * \brief With colour off, output contains no escape (self-assembled message only).
         * \note User-supplied text is asserted at the producers
         *       (`user_escapes_never_reach_the_page` in help_test.cpp). Keep both (trap 11).
         */
        consteval bool plain_rendering_emits_no_escape() {
            styled_str message;
            message.push(style_class::error, "error:")
                    .push_plain(" unexpected argument ")
                    .push(style_class::invalid, "--verbose");

            const std::string plain = render_plain(message);
            for (const char byte : plain) {
                if (byte == '\x1B') return false;
            }
            return plain == std::string_view{"error: unexpected argument --verbose"};
        }

        static_assert(plain_rendering_emits_no_escape(),
                      "clapp: render_plain() must never produce an escape sequence.");

        /** \brief Plain palette matches colour-off rendering. */
        consteval bool plain_palette_matches_plain_rendering() {
            styled_str message;
            message.push(style_class::header, "Options:")
                    .push_plain("\n  ")
                    .push(style_class::literal, "--help");
            return render_ansi(message, styles::plain()) == render_plain(message) &&
                   render(message, styles::styled(), false) == render_plain(message);
        }

        static_assert(plain_palette_matches_plain_rendering());

        /** \brief Styled runs are bracketed; plain neighbours stay bare. */
        consteval bool styled_runs_are_bracketed_and_plain_ones_are_not() {
            styled_str message;
            message.push(style_class::literal, "--help").push_plain(" show this");
            // literal is bold: \033[1m --help \033[0m, then the body with no escapes.
            return render_ansi(message, styles::styled()) ==
                   std::string_view{"\x1B[1m--help\x1B[0m show this"};
        }

        static_assert(styled_runs_are_bracketed_and_plain_ones_are_not());

        /* anstyle order: bold then red for style_class::error. */
        static_assert(render_style(styles::styled().get(style_class::error)) ==
                      std::string_view{"\x1B[1m\x1B[31m"});
        static_assert(render_style(style{}).empty());
        static_assert(render_style_reset(style{}).empty());
        static_assert(render_style_reset(styles::styled().get(style_class::header)) == ansi_reset);

        /** \brief Empty message → empty under every palette. */
        static_assert(render_ansi(styled_str{}, styles::styled()).empty());
        static_assert(render_plain(styled_str{}).empty());

    }  // namespace detail

}  // namespace clapp
