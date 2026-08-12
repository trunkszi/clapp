#include <clapp/builder/styling.hpp>
#include <clapp/meta/annotations.hpp>

#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    using clapp::ansi_color;
    using clapp::color;
    using clapp::color_choice;
    using clapp::color_env;
    using clapp::color_kind;
    using clapp::style;
    using clapp::style_class;
    using clapp::styles;
    using clapp::text_effects;
    using clapp::tri;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // color
    // ---------------------------------------------------------------------------

    static_assert(color{}.kind == color_kind::unset);
    static_assert(!color{}.is_set());
    static_assert(color{}.as_ansi() == std::nullopt);
    static_assert(color{}.as_index() == std::nullopt);

    static_assert(color::of(ansi_color::red).is_set());
    static_assert(color::of(ansi_color::red).as_ansi() == ansi_color::red);
    static_assert(color::of(ansi_color::red).as_index() == std::nullopt);
    static_assert(color::of(ansi_color::bright_white).r == 15);

    static_assert(color::indexed(200).is_set());
    static_assert(color::indexed(200).as_index() == std::uint8_t{200});
    // An ansi256 index that happens to be 0-15 displays like an ANSI colour but is a
    // different escape sequence, so as_ansi() must not claim it.
    static_assert(color::indexed(1).as_ansi() == std::nullopt);
    static_assert(color::indexed(1) != color::of(ansi_color::red));

    static_assert(color::rgb(1, 2, 3).is_set());
    static_assert(color::rgb(1, 2, 3).r == 1);
    static_assert(color::rgb(1, 2, 3).g == 2);
    static_assert(color::rgb(1, 2, 3).b == 3);
    static_assert(color::rgb(1, 2, 3).as_ansi() == std::nullopt);
    static_assert(color::rgb(1, 2, 3).as_index() == std::nullopt);

    // The enumerator values are palette indices, not SGR parameters: the renderer turns
    // `red == 1` into 31, 41 or a bright variant, and `bright_black == 8` is why there is
    // no second enumeration for the bright half.
    static_assert(std::to_underlying(ansi_color::black) == 0);
    static_assert(std::to_underlying(ansi_color::white) == 7);
    static_assert(std::to_underlying(ansi_color::bright_black) == 8);
    static_assert(std::to_underlying(ansi_color::bright_white) == 15);
    static_assert(std::meta::enumerators_of(^^clapp::ansi_color).size() == 16);

    // ---------------------------------------------------------------------------
    // text_effects
    // ---------------------------------------------------------------------------

    static_assert(text_effects{}.is_plain());
    static_assert(text_effects::none().is_plain());
    static_assert(!text_effects::bold().is_plain());

    // anstyle's bit positions, preserved so the two can be compared value for value.
    static_assert(text_effects::bold().bits == 1u << 0);
    static_assert(text_effects::dimmed().bits == 1u << 1);
    static_assert(text_effects::italic().bits == 1u << 2);
    static_assert(text_effects::underline().bits == 1u << 3);
    static_assert(text_effects::double_underline().bits == 1u << 4);
    static_assert(text_effects::curly_underline().bits == 1u << 5);
    static_assert(text_effects::dotted_underline().bits == 1u << 6);
    static_assert(text_effects::dashed_underline().bits == 1u << 7);
    static_assert(text_effects::blink().bits == 1u << 8);
    static_assert(text_effects::invert().bits == 1u << 9);
    static_assert(text_effects::hidden().bits == 1u << 10);
    static_assert(text_effects::strikethrough().bits == 1u << 11);

    // Set algebra.
    constexpr text_effects heading = text_effects::bold() | text_effects::underline();
    static_assert(heading.contains(text_effects::bold()));
    static_assert(heading.contains(text_effects::underline()));
    static_assert(heading.contains(heading));
    static_assert(!heading.contains(text_effects::italic()));
    static_assert(heading.contains(text_effects::none()));  // the empty set is in everything

    static_assert(heading.remove(text_effects::bold()) == text_effects::underline());
    static_assert(heading.remove(heading).is_plain());
    static_assert(heading.insert(text_effects::bold()) == heading);  // idempotent
    static_assert((heading & text_effects::bold()) == text_effects::bold());
    static_assert((heading & text_effects::italic()).is_plain());
    static_assert(text_effects::bold().set(text_effects::italic(), true) ==
                  (text_effects::bold() | text_effects::italic()));
    static_assert(text_effects::bold().set(text_effects::bold(), false).is_plain());

    // No two named effects share a bit.
    constexpr std::array<text_effects, 12> all_effects{text_effects::bold(),
                                                       text_effects::dimmed(),
                                                       text_effects::italic(),
                                                       text_effects::underline(),
                                                       text_effects::double_underline(),
                                                       text_effects::curly_underline(),
                                                       text_effects::dotted_underline(),
                                                       text_effects::dashed_underline(),
                                                       text_effects::blink(),
                                                       text_effects::invert(),
                                                       text_effects::hidden(),
                                                       text_effects::strikethrough()};

    consteval bool effects_occupy_distinct_bits() {
        std::uint16_t seen = 0;
        for (const text_effects effect : all_effects) {
            if ((seen & effect.bits) != 0) return false;
            seen = static_cast<std::uint16_t>(seen | effect.bits);
        }
        return true;
    }
    static_assert(effects_occupy_distinct_bits());

    // ---------------------------------------------------------------------------
    // style
    // ---------------------------------------------------------------------------

    static_assert(style{}.is_plain());
    static_assert(!style{}.bold().is_plain());
    static_assert(!style{}.with_fg(color::of(ansi_color::red)).is_plain());
    static_assert(!style{}.with_bg(color::of(ansi_color::red)).is_plain());
    static_assert(!style{}.with_underline_color(color::of(ansi_color::red)).is_plain());

    // The named helpers add one effect and leave the others alone; with_effects replaces.
    static_assert(style{}.bold().underline().effects == heading);
    static_assert(style{}.bold().underline().with_effects(text_effects::italic()).effects ==
                  text_effects::italic());
    static_assert(style{}.bold().bold().effects == text_effects::bold());
    static_assert(style{}.bold().italic().dimmed().strikethrough().effects ==
                  (text_effects::bold() | text_effects::italic() | text_effects::dimmed() |
                   text_effects::strikethrough()));

    // A wither changes one field and nothing else.
    constexpr style painted = style{}.bold().with_fg(color::of(ansi_color::green));
    static_assert(painted.fg == color::of(ansi_color::green));
    static_assert(painted.bg == color{});
    static_assert(painted.effects == text_effects::bold());

    // ---------------------------------------------------------------------------
    // style_class — the semantic vocabulary
    // ---------------------------------------------------------------------------

    static_assert(std::meta::enumerators_of(^^clapp::style_class).size() ==
                  clapp::style_class_count);
    static_assert(clapp::style_class_count == 10);

    // Enumerator zero is the neutral one, so a value-initialized style_class is body text
    // rather than an arbitrary decoration.
    static_assert(style_class{} == style_class::plain);

    // Naming every style class here makes a rename break the build.
    static_assert(std::to_underlying(style_class::header) < clapp::style_class_count);
    static_assert(std::to_underlying(style_class::literal) < clapp::style_class_count);
    static_assert(std::to_underlying(style_class::placeholder) < clapp::style_class_count);
    static_assert(std::to_underlying(style_class::valid) < clapp::style_class_count);
    static_assert(std::to_underlying(style_class::invalid) < clapp::style_class_count);
    static_assert(std::to_underlying(style_class::error) < clapp::style_class_count);
    static_assert(std::to_underlying(style_class::context_value) == clapp::style_class_count - 1);

    // ---------------------------------------------------------------------------
    // styles — the palette
    // ---------------------------------------------------------------------------

    // Structural, so it can be embedded in a command_spec that reaches static storage.
    template<styles>
    struct structural_probe {};
    using styles_are_structural = structural_probe<styles::styled()>;

    static_assert(std::is_trivially_copyable_v<styles>);

    // plain() styles nothing at all.
    consteval bool plain_palette_is_entirely_unstyled() {
        for (std::size_t i = 0; i < clapp::style_class_count; ++i) {
            if (!styles::plain().get(static_cast<style_class>(i)).is_plain()) return false;
        }
        return true;
    }
    static_assert(plain_palette_is_entirely_unstyled());

    // styled() is clap's default palette, class by class.
    static_assert(styles::styled().get(style_class::header) == style{}.bold().underline());
    static_assert(styles::styled().get(style_class::usage) == style{}.bold().underline());
    static_assert(styles::styled().get(style_class::literal) == style{}.bold());
    static_assert(styles::styled().get(style_class::error) ==
                  style{}.with_fg(color::of(ansi_color::red)).bold());
    static_assert(styles::styled().get(style_class::valid) ==
                  style{}.with_fg(color::of(ansi_color::green)));
    static_assert(styles::styled().get(style_class::invalid) ==
                  style{}.with_fg(color::of(ansi_color::yellow)));
    static_assert(styles::styled().get(style_class::placeholder).is_plain());
    static_assert(styles::styled().get(style_class::context).is_plain());
    static_assert(styles::styled().get(style_class::plain).is_plain());

    // Nothing in the default palette sets a background — the one thing guaranteed to
    // clash with a user's colour scheme.
    consteval bool default_palette_sets_no_background() {
        for (std::size_t i = 0; i < clapp::style_class_count; ++i) {
            if (styles::styled().get(static_cast<style_class>(i)).bg.is_set()) return false;
        }
        return true;
    }
    static_assert(default_palette_sets_no_background());

    // with() replaces one class and leaves the rest alone.
    constexpr styles v3 =
            styles::styled()
                    .with(style_class::header, style{}.with_fg(color::of(ansi_color::yellow)))
                    .with(style_class::literal, style{}.with_fg(color::of(ansi_color::green)));
    static_assert(v3.get(style_class::header) == style{}.with_fg(color::of(ansi_color::yellow)));
    static_assert(v3.get(style_class::literal) == style{}.with_fg(color::of(ansi_color::green)));
    static_assert(v3.get(style_class::error) == styles::styled().get(style_class::error));

    // context_value falls back to context until it is set explicitly — clap models this
    // with Option<Style>, which is not a structural type here.
    constexpr styles ctx = styles::plain().with(style_class::context, style{}.dimmed());
    static_assert(!ctx.context_value_set);
    static_assert(ctx.get(style_class::context_value) == style{}.dimmed());

    constexpr styles ctx_explicit = ctx.with(style_class::context_value, style{}.bold());
    static_assert(ctx_explicit.context_value_set);
    static_assert(ctx_explicit.get(style_class::context_value) == style{}.bold());
    static_assert(ctx_explicit.get(style_class::context) == style{}.dimmed());

    // Setting context_value to the same style as context still counts as set, so a later
    // change to context no longer drags it along.
    constexpr styles pinned = ctx.with(style_class::context_value, style{}.dimmed())
                                      .with(style_class::context, style{}.italic());
    static_assert(pinned.get(style_class::context) == style{}.italic());
    static_assert(pinned.get(style_class::context_value) == style{}.dimmed());

    // ---------------------------------------------------------------------------
    // color_choice
    // ---------------------------------------------------------------------------

    static_assert(clapp::all_color_choices.size() == 3);
    static_assert(std::meta::enumerators_of(^^clapp::color_choice).size() ==
                  clapp::all_color_choices.size());

    // auto_ is enumerator zero, matching clap's `#[default] Auto`.
    static_assert(color_choice{} == color_choice::auto_);

    // The trailing underscore is a keyword clash, not part of the name.
    static_assert(clapp::name_of(color_choice::auto_) == "auto"sv);
    static_assert(clapp::name_of(color_choice::always) == "always"sv);
    static_assert(clapp::name_of(color_choice::never) == "never"sv);

    static_assert(clapp::parse_color_choice("auto") == color_choice::auto_);
    static_assert(clapp::parse_color_choice("Always") == color_choice::always);
    static_assert(clapp::parse_color_choice("NEVER") == color_choice::never);
    static_assert(clapp::parse_color_choice("auto_") == std::nullopt);
    static_assert(clapp::parse_color_choice("") == std::nullopt);
    static_assert(clapp::parse_color_choice("sometimes") == std::nullopt);

    consteval bool color_choice_names_round_trip() {
        for (const color_choice choice : clapp::all_color_choices) {
            if (clapp::parse_color_choice(clapp::name_of(choice)) != choice) return false;
        }
        return true;
    }
    static_assert(color_choice_names_round_trip());

    // ---------------------------------------------------------------------------
    // resolve_color_choice — the precedence, one rule at a time
    //
    // This is the whole runtime behaviour of the header, and it is checkable at compile
    // time only because color_env carries probed facts rather than performing the probe.
    // ---------------------------------------------------------------------------

    // A terminal that supports colour.
    constexpr color_env terminal{.stream_is_terminal = true, .term_supports_color = true};
    // The same environment, piped.
    constexpr color_env piped{.stream_is_terminal = false, .term_supports_color = true};
    // A terminal running TERM=dumb.
    constexpr color_env dumb{.stream_is_terminal = true, .term_supports_color = false};

    // 1. An explicit choice beats every environment variable.
    static_assert(clapp::resolve_color_choice(color_choice::always, piped) == color_choice::always);
    static_assert(clapp::resolve_color_choice(color_choice::always, color_env{.no_color = true}) ==
                  color_choice::always);
    static_assert(clapp::resolve_color_choice(color_choice::never, terminal) ==
                  color_choice::never);
    static_assert(clapp::resolve_color_choice(color_choice::never,
                                              color_env{.clicolor_force = true}) ==
                  color_choice::never);

    // 2. NO_COLOR wins over CLICOLOR_FORCE.
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal  = true,
                                                        .no_color            = true,
                                                        .clicolor_force      = true,
                                                        .term_supports_color = true}) ==
                  color_choice::never);

    // 3. CLICOLOR_FORCE wins over a pipe and over TERM=dumb.
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal = false,
                                                        .clicolor_force     = true}) ==
                  color_choice::always);
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal = true,
                                                        .clicolor_force     = true,
                                                        .clicolor           = tri::no}) ==
                  color_choice::always);

    // 4. CLICOLOR=0 disables when nothing above it applies.
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal  = true,
                                                        .clicolor            = tri::no,
                                                        .term_supports_color = true}) ==
                  color_choice::never);

    // 5. Otherwise: a terminal, plus one source of support.
    static_assert(clapp::resolve_color_choice(color_choice::auto_, terminal) ==
                  color_choice::always);
    static_assert(clapp::resolve_color_choice(color_choice::auto_, piped) == color_choice::never);
    static_assert(clapp::resolve_color_choice(color_choice::auto_, dumb) == color_choice::never);

    // CLICOLOR=1 and CI are each enough on their own — but only on a terminal.
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal = true,
                                                        .clicolor           = tri::yes}) ==
                  color_choice::always);
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal = true,
                                                        .is_ci = true}) == color_choice::always);
    static_assert(clapp::resolve_color_choice(color_choice::auto_,
                                              color_env{.stream_is_terminal = false,
                                                        .is_ci = true}) == color_choice::never);

    // A default-constructed environment — nothing probed, nothing known — must not colour.
    static_assert(clapp::resolve_color_choice(color_choice::auto_, color_env{}) ==
                  color_choice::never);

    // should_style() is the same decision as a predicate.
    static_assert(clapp::should_style(color_choice::auto_, terminal));
    static_assert(!clapp::should_style(color_choice::auto_, piped));
    static_assert(clapp::should_style(color_choice::always, piped));
    static_assert(!clapp::should_style(color_choice::never, terminal));

    // Exhaustive: over every environment and every choice, the answer is never `auto_`,
    // and resolving twice changes nothing. 3 * 2^5 * 3 combinations.
    consteval bool resolution_is_total_and_idempotent() {
        for (const color_choice choice : clapp::all_color_choices) {
            for (unsigned bits = 0; bits < 32u; ++bits) {
                for (const tri clicolor : {tri::infer, tri::no, tri::yes}) {
                    const color_env env{
                            .stream_is_terminal  = (bits & 1u) != 0,
                            .no_color            = (bits & 2u) != 0,
                            .clicolor_force      = (bits & 4u) != 0,
                            .clicolor            = clicolor,
                            .term_supports_color = (bits & 8u) != 0,
                            .is_ci               = (bits & 16u) != 0,
                    };
                    const color_choice once = clapp::resolve_color_choice(choice, env);
                    if (once == color_choice::auto_) return false;
                    if (clapp::resolve_color_choice(once, env) != once) return false;
                    if (clapp::should_style(choice, env) != (once == color_choice::always)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    static_assert(resolution_is_total_and_idempotent());

    // Exhaustive: NO_COLOR disables colour under `auto_` in every environment, whatever
    // else is set. This is the property users complain about when it is wrong.
    consteval bool no_color_always_wins_under_auto() {
        for (unsigned bits = 0; bits < 32u; ++bits) {
            for (const tri clicolor : {tri::infer, tri::no, tri::yes}) {
                const color_env env{
                        .stream_is_terminal  = (bits & 1u) != 0,
                        .no_color            = true,
                        .clicolor_force      = (bits & 4u) != 0,
                        .clicolor            = clicolor,
                        .term_supports_color = (bits & 8u) != 0,
                        .is_ci               = (bits & 16u) != 0,
                };
                if (clapp::should_style(color_choice::auto_, env)) return false;
            }
        }
        return true;
    }
    static_assert(no_color_always_wins_under_auto());

}  // namespace

CLAPP_TEST("styling: no escape sequence is produced anywhere") {
    // The seam, stated as a test: a style is a value. The only bytes this header
    // knows about are the names of the classes and choices, and none of them
    // contains ESC. M5 owns the renderer; help-text snapshots stay colour-free.
    std::vector<std::string> vocabulary;
    for (const color_choice choice : clapp::all_color_choices) {
        vocabulary.emplace_back(clapp::name_of(choice));
    }
    for (const std::string& word : vocabulary) {
        CLAPP_CHECK(word.find('\033') == std::string::npos);
        CLAPP_CHECK(!word.empty());
    }
}

CLAPP_TEST("styling: the default palette matches clap's Styles::styled") {
    CLAPP_CHECK(styles::styled().get(style_class::header) == style{}.bold().underline());
    CLAPP_CHECK(styles::styled().get(style_class::literal) == style{}.bold());
    CLAPP_CHECK(styles::styled().get(style_class::valid).fg == color::of(ansi_color::green));
    CLAPP_CHECK(styles::plain().get(style_class::header).is_plain());
}

CLAPP_TEST("styling: context_value follows context until set") {
    const styles base = styles::plain().with(style_class::context, style{}.dimmed());
    CLAPP_CHECK(base.get(style_class::context_value) == style{}.dimmed());

    const styles pinned_here = base.with(style_class::context_value, style{}.bold());
    CLAPP_CHECK(pinned_here.get(style_class::context_value) == style{}.bold());
    CLAPP_CHECK(pinned_here.get(style_class::context) == style{}.dimmed());
}

CLAPP_TEST("styling: resolve_color_choice honours the documented precedence") {
    CLAPP_CHECK(clapp::should_style(color_choice::auto_, terminal));
    CLAPP_CHECK(!clapp::should_style(color_choice::auto_, piped));
    CLAPP_CHECK(!clapp::should_style(color_choice::auto_, dumb));
    CLAPP_CHECK(clapp::should_style(color_choice::always, piped));
    CLAPP_CHECK(!clapp::should_style(color_choice::never, terminal));
}

CLAPP_TEST("styling: color_choice parses from a string the user typed") {
    // std::string is a transient allocation and cannot be a constexpr variable.
    std::string typed = "Auto";
    CLAPP_CHECK(clapp::parse_color_choice(typed) == color_choice::auto_);
    typed = "nope";
    CLAPP_CHECK(clapp::parse_color_choice(typed) == std::nullopt);
}
