#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/util/str.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::possible_value;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    // ---------------------------------------------------------------------------
    // The folding rule itself, decided entirely at compile time
    // ---------------------------------------------------------------------------

    // U+00E4 LATIN SMALL LETTER A WITH DIAERESIS = C3 A4; its uppercase U+00C4 = C3 84.
    // A Unicode-aware folder equates them. clapp's does not, and — the part that matters —
    // it does not equate them to anything *else* either.
    inline constexpr std::string_view a_umlaut_lower = "\xC3\xA4";
    inline constexpr std::string_view a_umlaut_upper = "\xC3\x84";

    static_assert(!clapp::detail::equals_ignore_ascii_case(a_umlaut_lower, a_umlaut_upper));
    static_assert(clapp::detail::equals_ignore_ascii_case(a_umlaut_lower, a_umlaut_lower));

    // The safety property. Every byte of a multi-byte UTF-8 sequence is outside the ASCII
    // letter ranges, so folding cannot alter one, and two *different* non-ASCII characters
    // can never be folded together. Written with characters a plausible bug would confuse —
    // same lead byte, one continuation byte apart — rather than with two obviously different
    // ones (the lesson of traps 11 and 15 in CLAUDE.md).
    static_assert(!clapp::detail::equals_ignore_ascii_case("\xC3\xA4", "\xC3\xA5"));  // ä vs å
    static_assert(!clapp::detail::equals_ignore_ascii_case("\xC3\x84", "\xC3\x85"));  // Ä vs Å
    static_assert(!clapp::detail::equals_ignore_ascii_case("\xD0\xB0", "\xD0\x90"));  // а vs А
    static_assert(!clapp::detail::equals_ignore_ascii_case("\xE4\xB8\xAD", "\xE4\xB8\xAE"));

    // The ASCII half still works, including next to a multi-byte character.
    static_assert(clapp::detail::equals_ignore_ascii_case("fast", "FAST"));
    static_assert(clapp::detail::equals_ignore_ascii_case("a\xC3\xA4", "A\xC3\xA4"));
    static_assert(!clapp::detail::equals_ignore_ascii_case("a\xC3\xA4", "a\xC3\x84"));

    // ---------------------------------------------------------------------------
    // Value domains
    // ---------------------------------------------------------------------------

    /** clap's `value_parser(["ä"])`. */
    enum class umlaut_only {
        a_umlaut[[= clapp::value{.name = "\xC3\xA4"}]],
    };

    /** `aä`: one ASCII letter and one that is not. The mixed case is what pins the rule. */
    enum class mixed_case {
        a_umlaut[[= clapp::value{.name = "a\xC3\xA4"}]],
    };

    /** The control: a domain with nothing but ASCII, where clapp and clap agree exactly. */
    enum class ascii_only { fast, slow };

    static_assert(clapp::value_parser<umlaut_only>::possible_values().size() == 1);
    static_assert(clapp::value_parser<umlaut_only>::possible_values()[0].get_name() ==
                  std::string_view{"\xC3\xA4"});

    // possible_value::matches() is the function clap's test is really about. Asserted here
    // directly as well as through a whole parse, because the parse route can only report
    // "rejected" and this route reports *why*.
    static_assert(clapp::value_parser<umlaut_only>::possible_values()[0].matches(a_umlaut_lower,
                                                                                 true));
    static_assert(!clapp::value_parser<umlaut_only>::possible_values()[0].matches(a_umlaut_upper,
                                                                                  true));
    static_assert(!clapp::value_parser<umlaut_only>::possible_values()[0].matches(a_umlaut_upper,
                                                                                  false));

    // ---------------------------------------------------------------------------
    // Fixtures — clap's `Command::new("pv")`
    // ---------------------------------------------------------------------------

    consteval command_spec make_umlaut() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<umlaut_only>()
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec umlaut = make_umlaut();

    consteval command_spec make_umlaut_exact() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<umlaut_only>());
        return app.freeze();
    }
    constexpr command_spec umlaut_exact = make_umlaut_exact();

    consteval command_spec make_mixed() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<mixed_case>()
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec mixed = make_mixed();

    consteval command_spec make_ascii() {
        command_builder app("pv");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .action(arg_action::set)
                                   .value_parser<ascii_only>()
                                   .ignore_case());
        return app.freeze();
    }
    constexpr command_spec ascii = make_ascii();

    static_assert(umlaut.find_arg("option")->is_ignore_case_set());
    static_assert(!umlaut_exact.find_arg("option")->is_ignore_case_set());
    static_assert(mixed.find_arg("option")->is_ignore_case_set());
    static_assert(ascii.find_arg("option")->is_ignore_case_set());

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    bool chose(const outcome& got, umlaut_only want) {
        if (!got.has_value()) return false;
        const std::optional<const umlaut_only*> value = got->get_one<umlaut_only>("option");
        return value.has_value() && **value == want;
    }

}  // namespace

CLAPP_TEST("unicode.rs::possible_values_ignore_case") {
    // DIVERGENCE, stated at the point it bites. clap with `feature = "unicode"` accepts
    // `Ä` here; clapp rejects it, because `ignore_case` folds ASCII only. clap WITHOUT
    // that feature rejects it too, so this is conformance with clap's default build and a
    // divergence from its `unicode` one.
    const outcome accepted = clapp::parse(umlaut, raw_args{"pv", "--option", a_umlaut_lower});
    CLAPP_CHECK(accepted.has_value());
    CLAPP_CHECK(chose(accepted, umlaut_only::a_umlaut));

    const outcome refused = clapp::parse(umlaut, raw_args{"pv", "--option", a_umlaut_upper});
    CLAPP_CHECK(!refused.has_value());
    CLAPP_CHECK(kind_of(refused) == error_kind::invalid_value);
}

CLAPP_TEST("unicode: the refusal is a normal invalid_value and the message is well-formed") {
    // Rejecting is the decision; producing a broken diagnostic would not be. The message
    // must name the accepted value, quote what the user typed, and be valid UTF-8 — the
    // input here is multi-byte, which is exactly the shape that would expose a renderer
    // slicing on bytes.
    const outcome refused = clapp::parse(umlaut, raw_args{"pv", "--option", a_umlaut_upper});
    CLAPP_CHECK(!refused.has_value());

    const std::string rendered = message_of(refused);
    CLAPP_CHECK(clapp::validate_utf8(rendered).has_value());
    CLAPP_CHECK(rendered.find("[possible values: \xC3\xA4]") != std::string::npos);
    CLAPP_CHECK(rendered.find("--option <option>") != std::string::npos);
}

CLAPP_TEST("unicode: ignore_case changes nothing for a non-ASCII value") {
    // The control that makes the case above mean something: with `ignore_case` OFF, the
    // exact spelling still works and the uppercase one still fails. If `ignore_case` had
    // been silently ineffective for *every* input, the divergence test would pass for the
    // wrong reason.
    const outcome exact = clapp::parse(umlaut_exact, raw_args{"pv", "--option", a_umlaut_lower});
    CLAPP_CHECK(exact.has_value());

    const outcome upper = clapp::parse(umlaut_exact, raw_args{"pv", "--option", a_umlaut_upper});
    CLAPP_CHECK(!upper.has_value());
    CLAPP_CHECK(kind_of(upper) == error_kind::invalid_value);
}

CLAPP_TEST("unicode: ignore_case does fold ASCII, and only ASCII") {
    // The positive half. `--option FAST` must work, or the divergence above would be
    // indistinguishable from "ignore_case is broken".
    const outcome upper = clapp::parse(ascii, raw_args{"pv", "--option", "FAST"});
    CLAPP_CHECK(upper.has_value());

    const outcome mixed_spelling = clapp::parse(ascii, raw_args{"pv", "--option", "SlOw"});
    CLAPP_CHECK(mixed_spelling.has_value());
}

CLAPP_TEST("unicode: a mixed value folds on its ASCII half only") {
    // The precise characterization. `aä` answers to `Aä` (ASCII letter folded) and not to
    // `aÄ` (non-ASCII letter left alone). One value, two inputs, opposite verdicts —
    // which no coarser test can distinguish.
    const outcome ascii_folded = clapp::parse(mixed, raw_args{"pv", "--option", "A\xC3\xA4"});
    CLAPP_CHECK(ascii_folded.has_value());

    const outcome non_ascii_folded = clapp::parse(mixed, raw_args{"pv", "--option", "a\xC3\x84"});
    CLAPP_CHECK(!non_ascii_folded.has_value());
    CLAPP_CHECK(kind_of(non_ascii_folded) == error_kind::invalid_value);
}

CLAPP_TEST("unicode: a non-ASCII possible value survives the help page intact") {
    // The value list is rendered, not just matched. `ä` must appear as its two stored
    // bytes — a renderer that measured width in bytes and then cut would split it, and a
    // renderer that ASCII-folded for display would show something else entirely.
    const std::string rendered =
            clapp::render_help(umlaut, help_style{.use_long = clapp::long_help_exists(umlaut)})
                    .to_string();
    CLAPP_CHECK(clapp::validate_utf8(rendered).has_value());
    CLAPP_CHECK(rendered.find("[possible values: \xC3\xA4]") != std::string::npos);
}

CLAPP_TEST("unicode: a non-ASCII value is one column wide, not two") {
    // Why the page above lines up: `display_width` decodes UTF-8, so `ä` counts as one
    // column even though it occupies two bytes. This is the measurement half of the
    // measure/cut pair CLAUDE.md's trap 15 is about; the cut half is exercised by wrap().
    CLAPP_CHECK(clapp::display_width(a_umlaut_lower) == 1);
    CLAPP_CHECK(clapp::display_width("a\xC3\xA4") == 2);
    CLAPP_CHECK(clapp::wrap("a\xC3\xA4 b\xC3\xA4", 3) == std::string{"a\xC3\xA4\nb\xC3\xA4"});
}
