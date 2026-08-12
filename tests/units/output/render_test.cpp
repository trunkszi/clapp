#include <clapp/builder/styling.hpp>
#include <clapp/output/render.hpp>
#include <clapp/output/styled_str.hpp>

#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::ansi_color;
    using clapp::color;
    using clapp::color_choice;
    using clapp::color_env;
    using clapp::style;
    using clapp::style_class;
    using clapp::styled_str;
    using clapp::styles;
    using clapp::text_effects;
    using clapp::tri;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Shared fixtures
    // ---------------------------------------------------------------------------

    // A message that uses a styled class, an unstyled class, and body text between them,
    // with the fragment boundaries in places where an off-by-one in the renderer shows up
    // as a misplaced reset rather than as missing output.
    constexpr styled_str help_fragment() {
        styled_str message;
        message.push(style_class::header, "Options:")
                .push_plain("\n  ")
                .push(style_class::literal, "--color")
                .push_plain(" ")
                .push(style_class::placeholder, "<WHEN>")
                .push_plain("  when to colour\n");
        return message;
    }

    constexpr styled_str error_fragment() {
        styled_str message;
        message.push(style_class::error, "error:")
                .push_plain(" invalid value '")
                .push(style_class::invalid, "gren")
                .push_plain("' for '")
                .push(style_class::literal, "--color")
                .push_plain("'\n  tip: a similar value exists: ")
                .push(style_class::valid, "green")
                .push_plain("\n");
        return message;
    }

    // Escape-stripping is deliberately naive: clapp only ever emits CSI ... 'm', so a
    // scanner that eats from ESC to the first byte in [@-~] is exact for this input and
    // would visibly break if the renderer ever emitted something else.
    constexpr std::string strip_escapes(std::string_view text) {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\x1B') {
                out.push_back(text[i]);
                continue;
            }
            ++i;                                         // the ESC
            if (i < text.size() && text[i] == '[') ++i;  // the '['
            while (i < text.size() && !(text[i] >= '@' && text[i] <= '~')) ++i;
        }
        return out;
    }

    constexpr bool has_escape(std::string_view text) {
        for (const char byte : text) {
            if (byte == '\x1B') return true;
        }
        return false;
    }

    static_assert(strip_escapes("\x1B[1m--help\x1B[0m x") == "--help x"sv);
    static_assert(strip_escapes("\x1B[4:3mwavy\x1B[0m") == "wavy"sv);
    static_assert(strip_escapes("plain") == "plain"sv);
    static_assert(has_escape("a\x1B[0m"));
    static_assert(!has_escape("a[0m"));

    // ---------------------------------------------------------------------------
    // render_plain — the claim the whole seam rests on
    // ---------------------------------------------------------------------------

    consteval bool plain_is_the_text_and_nothing_else() {
        const styled_str message   = error_fragment();
        const std::string rendered = clapp::render_plain(message);
        return !has_escape(rendered) && rendered == message.to_string() &&
               rendered == "error: invalid value 'gren' for '--color'\n"
                           "  tip: a similar value exists: green\n"sv;
    }

    static_assert(plain_is_the_text_and_nothing_else());

    // The documented divergence from clap, pinned so that changing it is deliberate.
    // clap keeps its ANSI inside the StyledStr and writes through anstream's StripStream
    // when colour is off, which also strips escapes the *user* embedded in after_help.
    // clapp's spans hold no escapes, so render_plain() is the identity on content — and
    // therefore passes a user-supplied escape straight through. Every other "no escape"
    // assertion in this file uses content that has none, which is what makes them claims
    // about the renderer rather than about the input.
    consteval bool content_escapes_are_passed_through_not_stripped() {
        styled_str message;
        message.push_plain("see \x1B[1mthe manual\x1B[0m");
        return clapp::render_plain(message) == "see \x1B[1mthe manual\x1B[0m"sv &&
               clapp::render_plain(message) == message.to_string() &&
               clapp::render_ansi(message, styles::plain()) == clapp::render_plain(message);
    }

    static_assert(content_escapes_are_passed_through_not_stripped());

    // ---------------------------------------------------------------------------
    // "Colour off" has three spellings and must be one behaviour
    // ---------------------------------------------------------------------------
    //
    // This is the single most valuable assertion in the file. A renderer that emitted an
    // escape unconditionally — or that emitted a bare reset for an unstyled run — passes
    // every content test in the suite and fails only here.

    consteval bool colour_off_never_emits_an_escape() {
        for (const styled_str& message : {help_fragment(), error_fragment()}) {
            const std::string via_plain   = clapp::render_plain(message);
            const std::string via_render  = clapp::render(message, styles::styled(), false);
            const std::string via_palette = clapp::render_ansi(message, styles::plain());

            if (has_escape(via_plain) || has_escape(via_render) || has_escape(via_palette)) {
                return false;
            }
            if (via_render != via_plain || via_palette != via_plain) return false;
        }
        return true;
    }

    static_assert(colour_off_never_emits_an_escape());

    // The same claim once more, phrased as the property a snapshot test depends on: the
    // coloured rendering carries exactly the uncoloured one inside it. Run over three
    // palettes, including one that exercises every colour kind and every effect, so that
    // a renderer which dropped or duplicated text while getting the escapes right cannot
    // pass.
    constexpr styles exotic_palette() {
        const style everything =
                style{}.with_fg(color::rgb(1, 22, 255))
                        .with_bg(color::indexed(240))
                        .with_underline_color(color::of(ansi_color::bright_magenta))
                        .with_effects(text_effects{.bits = 0x0FFF});
        return styles::plain()
                .with(style_class::header, everything)
                .with(style_class::literal, style{}.with_fg(color::of(ansi_color::bright_cyan)))
                .with(style_class::placeholder, style{}.italic())
                .with(style_class::plain, style{}.dimmed())
                .with(style_class::invalid, style{}.with_bg(color::of(ansi_color::red)))
                .with(style_class::valid, style{}.with_fg(color::indexed(2)));
    }

    consteval bool stripping_the_colour_gives_the_plain_text() {
        for (const styled_str& message : {help_fragment(), error_fragment()}) {
            const std::string plain = clapp::render_plain(message);
            for (const styles& palette : {styles::plain(), styles::styled(), exotic_palette()}) {
                if (strip_escapes(clapp::render_ansi(message, palette)) != plain) return false;
            }
        }
        return true;
    }

    static_assert(stripping_the_colour_gives_the_plain_text());

    // An empty message renders to nothing under every palette. A renderer that emitted a
    // leading reset "to be safe" would fail here and nowhere else.
    static_assert(clapp::render_plain(styled_str{}).empty());
    static_assert(clapp::render_ansi(styled_str{}, styles::styled()).empty());
    static_assert(clapp::render_ansi(styled_str{}, exotic_palette()).empty());

    // ---------------------------------------------------------------------------
    // render_ansi — the exact bytes
    // ---------------------------------------------------------------------------

    consteval bool default_palette_brackets_only_styled_runs() {
        const styled_str message = help_fragment();
        // header is bold+underline; literal is bold; placeholder and plain are unstyled in
        // clap's default palette, so they must arrive with no escapes around them at all.
        return clapp::render_ansi(message, styles::styled()) == "\x1B[1m\x1B[4mOptions:\x1B[0m"
                                                                "\n  "
                                                                "\x1B[1m--color\x1B[0m"
                                                                " <WHEN>  when to colour\n"sv;
    }

    static_assert(default_palette_brackets_only_styled_runs());

    consteval bool error_message_marks_the_offending_value() {
        return clapp::render_ansi(error_fragment(), styles::styled()) ==
               "\x1B[1m\x1B[31merror:\x1B[0m"
               " invalid value '"
               "\x1B[33mgren\x1B[0m"
               "' for '"
               "\x1B[1m--color\x1B[0m"
               "'\n  tip: a similar value exists: "
               "\x1B[32mgreen\x1B[0m"
               "\n"sv;
    }

    static_assert(error_message_marks_the_offending_value());

    // The documented non-coalescing rule, pinned so that "optimising" it later is a
    // deliberate act with a failing test attached. header and usage resolve to the same
    // clapp::style in the default palette, and still get their own bracket pair.
    consteval bool adjacent_equal_styles_are_not_coalesced() {
        styled_str message;
        message.push(style_class::header, "A").push(style_class::usage, "B");
        return clapp::render_ansi(message, styles::styled()) ==
               "\x1B[1m\x1B[4mA\x1B[0m\x1B[1m\x1B[4mB\x1B[0m"sv;
    }

    static_assert(adjacent_equal_styles_are_not_coalesced());

    // context_value with no explicit entry follows context — the styles::get() fallback
    // has to survive the trip through the renderer, or `[default: false]` loses its
    // value's colour the moment someone styles `context` alone.
    consteval bool context_value_follows_context_through_the_renderer() {
        const styles palette = styles::plain().with(style_class::context,
                                                    style{}.with_fg(color::of(ansi_color::cyan)));
        styled_str message;
        message.push(style_class::context, "[default: ")
                .push(style_class::context_value, "false")
                .push(style_class::context, "]");
        return clapp::render_ansi(message, palette) ==
               "\x1B[36m[default: \x1B[0m\x1B[36mfalse\x1B[0m\x1B[36m]\x1B[0m"sv;
    }

    static_assert(context_value_follows_context_through_the_renderer());

    // Exhaustive: under clap's default palette, exactly the six classes the table in
    // styling.hpp names produce escapes, and the other four do not. A class added to the
    // palette without a decision about it shows up here.
    consteval bool exactly_the_documented_classes_are_styled() {
        constexpr std::array<style_class, 10> every{style_class::plain,
                                                    style_class::header,
                                                    style_class::usage,
                                                    style_class::literal,
                                                    style_class::placeholder,
                                                    style_class::valid,
                                                    style_class::invalid,
                                                    style_class::error,
                                                    style_class::context,
                                                    style_class::context_value};

        for (const style_class which : every) {
            const styled_str message{which, "x"};
            const std::string rendered = clapp::render_ansi(message, styles::styled());
            const bool styled          = has_escape(rendered);
            const bool expected = which == style_class::header || which == style_class::usage ||
                                  which == style_class::literal || which == style_class::valid ||
                                  which == style_class::invalid || which == style_class::error;
            if (styled != expected) return false;
            if (strip_escapes(rendered) != "x"sv) return false;
        }
        return true;
    }

    static_assert(exactly_the_documented_classes_are_styled());

    // ---------------------------------------------------------------------------
    // render_style — anstyle's spelling, colour kind by colour kind
    // ---------------------------------------------------------------------------

    // Effects come first, in bit order, each as its own SGR. anstyle's effect::METADATA.
    static_assert(clapp::render_style(style{}.bold()) == "\x1B[1m"sv);
    static_assert(clapp::render_style(style{}.dimmed()) == "\x1B[2m"sv);
    static_assert(clapp::render_style(style{}.italic()) == "\x1B[3m"sv);
    static_assert(clapp::render_style(style{}.underline()) == "\x1B[4m"sv);
    static_assert(clapp::render_style(style{}.strikethrough()) == "\x1B[9m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::double_underline())) ==
                  "\x1B[21m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::curly_underline())) ==
                  "\x1B[4:3m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::dotted_underline())) ==
                  "\x1B[4:4m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::dashed_underline())) ==
                  "\x1B[4:5m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::blink())) == "\x1B[5m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::invert())) == "\x1B[7m"sv);
    static_assert(clapp::render_style(style{}.with_effects(text_effects::hidden())) == "\x1B[8m"sv);

    // All twelve at once, which pins the *order* rather than the individual codes.
    static_assert(clapp::render_style(style{}.with_effects(text_effects{.bits = 0x0FFF})) ==
                  "\x1B[1m\x1B[2m\x1B[3m\x1B[4m\x1B[21m\x1B[4:3m"
                  "\x1B[4:4m\x1B[4:5m\x1B[5m\x1B[7m\x1B[8m\x1B[9m"sv);

    // The 16 ANSI colours: 30–37 and 90–97 for foreground, 40–47 and 100–107 for
    // background. The bright half is where an off-by-eight hides.
    static_assert(clapp::render_style(style{}.with_fg(color::of(ansi_color::black))) ==
                  "\x1B[30m"sv);
    static_assert(clapp::render_style(style{}.with_fg(color::of(ansi_color::white))) ==
                  "\x1B[37m"sv);
    static_assert(clapp::render_style(style{}.with_fg(color::of(ansi_color::bright_black))) ==
                  "\x1B[90m"sv);
    static_assert(clapp::render_style(style{}.with_fg(color::of(ansi_color::bright_white))) ==
                  "\x1B[97m"sv);
    static_assert(clapp::render_style(style{}.with_bg(color::of(ansi_color::black))) ==
                  "\x1B[40m"sv);
    static_assert(clapp::render_style(style{}.with_bg(color::of(ansi_color::white))) ==
                  "\x1B[47m"sv);
    static_assert(clapp::render_style(style{}.with_bg(color::of(ansi_color::bright_black))) ==
                  "\x1B[100m"sv);
    static_assert(clapp::render_style(style{}.with_bg(color::of(ansi_color::bright_white))) ==
                  "\x1B[107m"sv);

    // An underlined ANSI colour has no code of its own; anstyle promotes it to the
    // 256-colour palette, whose first sixteen indices are the ANSI ones.
    static_assert(clapp::render_style(style{}.with_underline_color(color::of(ansi_color::red))) ==
                  "\x1B[58;5;1m"sv);
    static_assert(clapp::render_style(style{}.with_underline_color(
                          color::of(ansi_color::bright_white))) == "\x1B[58;5;15m"sv);

    // 256-colour and truecolour, on all three layers.
    static_assert(clapp::render_style(style{}.with_fg(color::indexed(0))) == "\x1B[38;5;0m"sv);
    static_assert(clapp::render_style(style{}.with_fg(color::indexed(255))) == "\x1B[38;5;255m"sv);
    static_assert(clapp::render_style(style{}.with_bg(color::indexed(240))) == "\x1B[48;5;240m"sv);
    static_assert(clapp::render_style(style{}.with_underline_color(color::indexed(9))) ==
                  "\x1B[58;5;9m"sv);
    static_assert(clapp::render_style(style{}.with_fg(color::rgb(0, 128, 255))) ==
                  "\x1B[38;2;0;128;255m"sv);
    static_assert(clapp::render_style(style{}.with_bg(color::rgb(255, 0, 7))) ==
                  "\x1B[48;2;255;0;7m"sv);
    static_assert(clapp::render_style(style{}.with_underline_color(color::rgb(1, 2, 3))) ==
                  "\x1B[58;2;1;2;3m"sv);

    // index 1 as an ANSI colour and index 1 as a palette index are the same pixel and
    // different bytes — the distinction color::as_ansi() exists to protect.
    static_assert(clapp::render_style(style{}.with_fg(color::of(ansi_color::red))) !=
                  clapp::render_style(style{}.with_fg(color::indexed(1))));

    // Ordering within one style: effects, then fg, then bg, then underline colour.
    static_assert(clapp::render_style(style{}.with_underline_color(color::of(ansi_color::blue))
                                              .with_bg(color::indexed(7))
                                              .with_fg(color::rgb(9, 9, 9))
                                              .bold()) ==
                  "\x1B[1m\x1B[38;2;9;9;9m\x1B[48;5;7m\x1B[58;5;4m"sv);

    // Reset elision: a run that emitted nothing must not emit a reset.
    static_assert(clapp::render_style(style{}).empty());
    static_assert(clapp::render_style_reset(style{}).empty());
    static_assert(clapp::render_style_reset(style{}.bold()) == clapp::ansi_reset);
    static_assert(clapp::ansi_reset == "\x1B[0m"sv);

    // ---------------------------------------------------------------------------
    // The injected environment
    // ---------------------------------------------------------------------------
    //
    // Five optionals rather than a map: the shape makes "unset" and "set to empty"
    // impossible to conflate at the call site, which is exactly the distinction the three
    // emptiness rules turn on.

    struct fake_env {
        std::optional<std::string_view> no_color{};
        std::optional<std::string_view> clicolor_force{};
        std::optional<std::string_view> clicolor{};
        std::optional<std::string_view> term{};
        std::optional<std::string_view> ci{};

        constexpr std::optional<std::string_view> operator()(std::string_view name) const {
            if (name == "NO_COLOR"sv) return no_color;
            if (name == "CLICOLOR_FORCE"sv) return clicolor_force;
            if (name == "CLICOLOR"sv) return clicolor;
            if (name == "TERM"sv) return term;
            if (name == "CI"sv) return ci;
            return std::nullopt;
        }
    };

    static_assert(clapp::env_lookup<fake_env>);

    // A lambda is a lookup too — the concept must not accidentally require a class type,
    // or the injection point is unusable from a caller that has one variable to fake.
    static_assert(clapp::env_lookup<decltype([](std::string_view) {
        return std::optional<std::string_view>{};
    })>);

    constexpr color_env probe(bool is_terminal, const fake_env& env) {
        return clapp::probe_color_env(is_terminal, env);
    }

    // ---------------------------------------------------------------------------
    // probe_color_env — one field at a time
    // ---------------------------------------------------------------------------

    // The terminal answer is passed through, never re-derived.
    static_assert(probe(true, fake_env{}).stream_is_terminal);
    static_assert(!probe(false, fake_env{}).stream_is_terminal);

    // NO_COLOR: presence is not enough, the value must be non-empty (no-color.org).
    static_assert(!probe(true, fake_env{}).no_color);
    static_assert(!probe(true, fake_env{.no_color = ""sv}).no_color);
    static_assert(probe(true, fake_env{.no_color = "1"sv}).no_color);
    static_assert(probe(true, fake_env{.no_color = "0"sv}).no_color);      // any non-empty value
    static_assert(probe(true, fake_env{.no_color = "false"sv}).no_color);  // including this one

    // CLICOLOR_FORCE: the same emptiness rule, and — surprisingly — no value check at all.
    static_assert(!probe(true, fake_env{}).clicolor_force);
    static_assert(!probe(true, fake_env{.clicolor_force = ""sv}).clicolor_force);
    static_assert(probe(true, fake_env{.clicolor_force = "1"sv}).clicolor_force);
    static_assert(probe(true, fake_env{.clicolor_force = "0"sv}).clicolor_force);

    // CLICOLOR is the one that reads its value, and it is tri-state: unset is not "no".
    static_assert(probe(true, fake_env{}).clicolor == tri::infer);
    static_assert(probe(true, fake_env{.clicolor = "0"sv}).clicolor == tri::no);
    static_assert(probe(true, fake_env{.clicolor = "1"sv}).clicolor == tri::yes);
    static_assert(probe(true, fake_env{.clicolor = ""sv}).clicolor == tri::yes);
    static_assert(probe(true, fake_env{.clicolor = "00"sv}).clicolor == tri::yes);
    static_assert(probe(true, fake_env{.clicolor = "no"sv}).clicolor == tri::yes);

// TERM: on POSIX an unset TERM means no colour support; only "dumb" is rejected by
// value. (The Windows branch inverts the unset case; this suite runs on POSIX.)
#if !defined(_WIN32)
    static_assert(!probe(true, fake_env{}).term_supports_color);
    static_assert(!probe(true, fake_env{.term = "dumb"sv}).term_supports_color);
    static_assert(probe(true, fake_env{.term = "xterm-256color"sv}).term_supports_color);
    static_assert(probe(true, fake_env{.term = ""sv}).term_supports_color);
    static_assert(probe(true, fake_env{.term = "dumb-but-not-really"sv}).term_supports_color);
#endif

    // CI: presence alone, empty included — the one variable where emptiness counts.
    static_assert(!probe(true, fake_env{}).is_ci);
    static_assert(probe(true, fake_env{.ci = ""sv}).is_ci);
    static_assert(probe(true, fake_env{.ci = "woodpecker"sv}).is_ci);
    static_assert(probe(true, fake_env{.ci = "false"sv}).is_ci);

    // The three emptiness rules, side by side. This is the assertion that fails if
    // someone "simplifies" all three probes into one helper.
    consteval bool the_three_emptiness_rules_differ() {
        const color_env all_empty = probe(
                true,
                fake_env{.no_color = ""sv, .clicolor_force = ""sv, .clicolor = ""sv, .ci = ""sv});
        return !all_empty.no_color                // empty NO_COLOR means nothing
               && !all_empty.clicolor_force       // empty CLICOLOR_FORCE means nothing
               && all_empty.clicolor == tri::yes  // empty CLICOLOR means "not 0", so yes
               && all_empty.is_ci;                // empty CI still means CI
    }

    static_assert(the_three_emptiness_rules_differ());

    // The probe reads exactly five variables. A sixth — COLORTERM, TERM_PROGRAM, FORCE_COLOR
    // — is a policy change, and this is where it has to be argued for.
    consteval bool probe_reads_exactly_five_variables() {
        std::array<std::string_view, 12> seen{};
        std::size_t count = 0;

        const auto lookup = [&](std::string_view name) -> std::optional<std::string_view> {
            if (count < seen.size()) seen[count] = name;
            ++count;
            return std::nullopt;
        };
        static_cast<void>(clapp::probe_color_env(false, lookup));

        if (count != 5) return false;
        constexpr std::array<std::string_view, 5> expected{
                "NO_COLOR"sv, "CLICOLOR_FORCE"sv, "CLICOLOR"sv, "TERM"sv, "CI"sv};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (seen[i] != expected[i]) return false;
        }
        return true;
    }

    static_assert(probe_reads_exactly_five_variables());

    // ---------------------------------------------------------------------------
    // resolve_color — probe and precedence, end to end
    // ---------------------------------------------------------------------------

    constexpr bool decide(color_choice choice, bool is_terminal, const fake_env& env) {
        return clapp::resolve_color(choice, probe(is_terminal, env));
    }

    // A terminal that says it supports colour.
    constexpr fake_env xterm{.term = "xterm-256color"sv};

    // 1. An explicit choice beats every variable, in both directions.
    static_assert(decide(color_choice::always, false, fake_env{.no_color = "1"sv}));
    static_assert(decide(color_choice::always, false, fake_env{.term = "dumb"sv}));
    static_assert(!decide(color_choice::never, true, xterm));
    static_assert(!decide(color_choice::never, true, fake_env{.clicolor_force = "1"sv}));

    // 2. NO_COLOR beats CLICOLOR_FORCE.
    static_assert(!decide(color_choice::auto_,
                          true,
                          fake_env{.no_color = "1"sv, .clicolor_force = "1"sv, .term = "xterm"sv}));

    // ...and an *empty* NO_COLOR does not, which is the whole point of the emptiness rule.
    static_assert(decide(color_choice::auto_,
                         true,
                         fake_env{.no_color = ""sv, .clicolor_force = "1"sv}));

    // 3. CLICOLOR_FORCE beats a pipe, TERM=dumb and CLICOLOR=0 — including when it is
    //    itself spelled "0", which reads wrong and is what anstyle-query does.
    static_assert(decide(color_choice::auto_, false, fake_env{.clicolor_force = "1"sv}));
    static_assert(decide(color_choice::auto_, false, fake_env{.clicolor_force = "0"sv}));
    static_assert(decide(color_choice::auto_,
                         true,
                         fake_env{.clicolor_force = "1"sv, .clicolor = "0"sv, .term = "dumb"sv}));

    // 4. CLICOLOR=0 disables once nothing above it applies — even on a good terminal.
    static_assert(!decide(color_choice::auto_,
                          true,
                          fake_env{.clicolor = "0"sv, .term = "xterm-256color"sv}));

    // 5. Otherwise: a terminal plus one source of support.
    static_assert(decide(color_choice::auto_, true, xterm));
    static_assert(!decide(color_choice::auto_, false, xterm));  // piped
    static_assert(!decide(color_choice::auto_, true, fake_env{.term = "dumb"sv}));
    static_assert(!decide(color_choice::auto_, true, fake_env{}));  // TERM unset (POSIX)
    static_assert(decide(color_choice::auto_, true, fake_env{.clicolor = "1"sv}));
    static_assert(decide(color_choice::auto_, true, fake_env{.ci = ""sv}));
    static_assert(!decide(color_choice::auto_, false, fake_env{.ci = "true"sv}));

    // CI rescues a dumb terminal, because the "support" clause is a disjunction.
    static_assert(decide(color_choice::auto_, true, fake_env{.term = "dumb"sv, .ci = "true"sv}));

    // Exhaustive over the probe's whole input space: 2 terminal answers * 3 NO_COLOR
    // spellings * 3 CLICOLOR_FORCE * 4 CLICOLOR * 4 TERM * 3 CI = 864 environments, times
    // three choices. Everything asserted individually above is a named row of this table;
    // the table itself catches the rows nobody thought to name.
    consteval bool the_whole_truth_table_agrees_with_the_ported_rule() {
        constexpr std::array<std::optional<std::string_view>, 3> presence{
                std::nullopt, std::optional{""sv}, std::optional{"1"sv}};
        constexpr std::array<std::optional<std::string_view>, 4> clicolors{
                std::nullopt, std::optional{""sv}, std::optional{"0"sv}, std::optional{"1"sv}};
        constexpr std::array<std::optional<std::string_view>, 4> terms{
                std::nullopt,
                std::optional{""sv},
                std::optional{"dumb"sv},
                std::optional{"xterm-256color"sv}};

        for (const bool is_terminal : {false, true}) {
            for (const std::optional<std::string_view>& no_color : presence) {
                for (const std::optional<std::string_view>& force : presence) {
                    for (const std::optional<std::string_view>& clicolor : clicolors) {
                        for (const std::optional<std::string_view>& term : terms) {
                            for (const std::optional<std::string_view>& ci : presence) {
                                const fake_env env{.no_color       = no_color,
                                                   .clicolor_force = force,
                                                   .clicolor       = clicolor,
                                                   .term           = term,
                                                   .ci             = ci};
                                const color_env probed = probe(is_terminal, env);

                                // The reference: anstream 1.0.0 `fn choice`, re-derived here
                                // from the raw variables rather than from `probed`, so that a
                                // bug in probe_color_env() cannot cancel out against itself.
                                const bool no_color_set =
                                        no_color.has_value() && !(*no_color).empty();
                                const bool force_set = force.has_value() && !(*force).empty();
                                const bool clicolor_yes =
                                        clicolor.has_value() && *clicolor != "0"sv;
                                const bool clicolor_no = clicolor.has_value() && *clicolor == "0"sv;
#if defined(_WIN32)
                                const bool term_ok = !term.has_value() || *term != "dumb"sv;
#else
                                const bool term_ok = term.has_value() && *term != "dumb"sv;
#endif
                                const bool ci_set = ci.has_value();

                                bool expected_auto = false;
                                if (no_color_set) {
                                    expected_auto = false;
                                } else if (force_set) {
                                    expected_auto = true;
                                } else if (clicolor_no) {
                                    expected_auto = false;
                                } else {
                                    expected_auto =
                                            is_terminal && (term_ok || clicolor_yes || ci_set);
                                }

                                if (clapp::resolve_color(color_choice::auto_, probed) !=
                                    expected_auto) {
                                    return false;
                                }
                                if (!clapp::resolve_color(color_choice::always, probed))
                                    return false;
                                if (clapp::resolve_color(color_choice::never, probed)) return false;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }

    static_assert(the_whole_truth_table_agrees_with_the_ported_rule());

    // The two halves meet: with the decision made, `render` picks the right renderer, and
    // the "never" side is escape-free in every environment the table above can produce.
    consteval bool a_never_decision_reaches_the_renderer() {
        const styled_str message = error_fragment();
        for (const bool is_terminal : {false, true}) {
            const color_env probed = probe(is_terminal, xterm);
            const bool on          = clapp::resolve_color(color_choice::never, probed);
            if (on) return false;
            if (has_escape(clapp::render(message, styles::styled(), on))) return false;
        }
        return true;
    }

    static_assert(a_never_decision_reaches_the_renderer());

    // ---------------------------------------------------------------------------
    // Runtime mirror
    // ---------------------------------------------------------------------------
    //
    // The cases below report the compile-time conclusions and cover the two things that
    // cannot cross the consteval boundary: the real getenv/isatty edge, and a rendered
    // std::string that outlives the expression that built it.

    CLAPP_TEST("render: colour off emits no escape, three ways") {
        CLAPP_CHECK(colour_off_never_emits_an_escape());
        CLAPP_CHECK(plain_is_the_text_and_nothing_else());
        CLAPP_CHECK(content_escapes_are_passed_through_not_stripped());

        styled_str message;
        message.push(style_class::error, "error:").push_plain(" boom");

        const std::string plain   = clapp::render_plain(message);
        const std::string off     = clapp::render(message, styles::styled(), false);
        const std::string plain_p = clapp::render_ansi(message, styles::plain());

        CLAPP_CHECK(!has_escape(plain));
        CLAPP_CHECK(!has_escape(off));
        CLAPP_CHECK(!has_escape(plain_p));
        CLAPP_CHECK(plain == off);
        CLAPP_CHECK(plain == plain_p);
        CLAPP_CHECK(plain == "error: boom");
    }

    CLAPP_TEST("render: the coloured rendering strips back to the plain one") {
        CLAPP_CHECK(stripping_the_colour_gives_the_plain_text());

        const styled_str message = error_fragment();
        const std::string on     = clapp::render_ansi(message, styles::styled());
        CLAPP_CHECK(has_escape(on));
        CLAPP_CHECK(strip_escapes(on) == clapp::render_plain(message));
        CLAPP_CHECK(on.size() > clapp::render_plain(message).size());
    }

    CLAPP_TEST("render: exact bytes survive being returned from a function") {
        const std::string rendered = [] {
            styled_str message;
            message.push(style_class::literal, "--help").push_plain(" show this");
            return clapp::render_ansi(message, styles::styled());
        }();
        CLAPP_CHECK(rendered == "\x1B[1m--help\x1B[0m show this");
    }

    CLAPP_TEST("render_style: anstyle's spelling and ordering hold at run time too") {
        CLAPP_CHECK(clapp::render_style(styles::styled().get(style_class::error)) ==
                    "\x1B[1m\x1B[31m");
        CLAPP_CHECK(clapp::render_style(styles::styled().get(style_class::header)) ==
                    "\x1B[1m\x1B[4m");
        CLAPP_CHECK(clapp::render_style(styles::plain().get(style_class::plain)).empty());
        CLAPP_CHECK(clapp::render_style_reset(styles::plain().get(style_class::plain)).empty());
        CLAPP_CHECK(clapp::render_style_reset(styles::styled().get(style_class::error)) ==
                    clapp::ansi_reset);
    }

    CLAPP_TEST("probe_color_env: the injected truth table holds at run time") {
        CLAPP_CHECK(the_three_emptiness_rules_differ());
        CLAPP_CHECK(probe_reads_exactly_five_variables());
        CLAPP_CHECK(the_whole_truth_table_agrees_with_the_ported_rule());
    }

    CLAPP_TEST("stream_is_terminal: a null stream is not a terminal") {
        // The one branch that is decidable without knowing how ctest wired up the process.
        CLAPP_CHECK(!clapp::stream_is_terminal(nullptr));
    }

    CLAPP_TEST("resolve_color: the real edge agrees with the probe it performs") {
        // Nothing here asserts whether ctest gave us a terminal — that is the harness's
        // business and asserting it would make the test fail interactively or in CI, one or
        // the other. What must hold is that the two spellings of the same question agree.
        for (std::FILE* const stream : {stdout, stderr}) {
            const color_env probed = clapp::probe_color_env(stream);
            CLAPP_CHECK(probed.stream_is_terminal == clapp::stream_is_terminal(stream));
            CLAPP_CHECK(clapp::resolve_color(color_choice::auto_, stream) ==
                        clapp::resolve_color(color_choice::auto_, probed));

            // An explicit choice does not consult the environment at all, so these two are
            // assertable no matter where the output went.
            CLAPP_CHECK(clapp::resolve_color(color_choice::always, stream));
            CLAPP_CHECK(!clapp::resolve_color(color_choice::never, stream));
        }
    }

    CLAPP_TEST("render_for_stream: an explicit choice decides both directions") {
        styled_str message;
        message.push(style_class::error, "error:").push_plain(" boom");

        const std::string never =
                clapp::render_for_stream(message, styles::styled(), color_choice::never, stdout);
        const std::string always =
                clapp::render_for_stream(message, styles::styled(), color_choice::always, stdout);

        CLAPP_CHECK(!has_escape(never));
        CLAPP_CHECK(never == "error: boom");
        CLAPP_CHECK(has_escape(always));
        CLAPP_CHECK(always == "\x1B[1m\x1B[31merror:\x1B[0m boom");
        CLAPP_CHECK(strip_escapes(always) == never);
    }

    CLAPP_TEST("render_for_stream: a null stream is not a terminal, and not a crash") {
        // resolve_color() reaches isatty() through stream_is_terminal(), which must not hand
        // a null FILE* to fileno().
        CLAPP_CHECK(!clapp::probe_color_env(nullptr).stream_is_terminal);

        // Deliberately *not* asserted: that `auto_` comes out off here. A developer with
        // CLICOLOR_FORCE exported gets colour on a null stream, correctly — that rule beats
        // the pipe check, and hard-coding "off" would make this test pass on CI and fail on
        // their machine. What is assertable is that the one-call form agrees with the two
        // calls it is defined as.
        const styled_str message{style_class::header, "Options:"};
        const bool on = clapp::resolve_color(color_choice::auto_, nullptr);
        const std::string rendered =
                clapp::render_for_stream(message, styles::styled(), color_choice::auto_, nullptr);
        CLAPP_CHECK(rendered == clapp::render(message, styles::styled(), on));
        CLAPP_CHECK(has_escape(rendered) == on);
        CLAPP_CHECK(strip_escapes(rendered) == "Options:");
    }

}  // namespace
