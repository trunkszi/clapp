#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    // ---------------------------------------------------------------------------
    // The offending bytes
    //
    // clap writes `OsString::from_vec(vec![0xe9])`. 0xE9 is the lead byte of a three-byte
    // sequence with nothing following it, so it is `encoding_error::truncated` — the same
    // classification `std::str::from_utf8` gives it.
    //
    // Named objects rather than literals at each call site, deliberately: GCC 16.1.0 can fold
    // a `constexpr` call on a string *literal* to a different answer than the run-time call
    // gives; binding first is the workaround.
    // ---------------------------------------------------------------------------

    constexpr os_str bad_value{"\xE9"};
    constexpr os_str short_equals_bad{"-a=\xE9"};
    constexpr os_str short_attached_bad{"-a\xE9"};
    constexpr os_str long_equals_bad{"--arg=\xE9"};

    // A well-formed WTF-8 unpaired surrogate (U+D800). Valid WTF-8, invalid UTF-8 — the one
    // input that separates clapp's storage rule from Rust's on POSIX.
    constexpr os_str lone_surrogate{"\xED\xA0\x80"};

    static_assert(!bad_value.is_utf8());
    static_assert(!bad_value.is_wtf8());
    static_assert(bad_value.to_string_view().error().kind == clapp::encoding_error::truncated);

    static_assert(!lone_surrogate.is_utf8());
    static_assert(lone_surrogate.is_wtf8());

    // 差异清单 entry 10, re-verified: ONE replacement character for the surrogate (clapp reads
    // WTF-8 units on every platform), THREE for a genuinely byte-wise mess. If a future
    // encoding change collapsed these two, the entry would be quietly wrong; this is the
    // assertion that would fail instead.
    static_assert(lone_surrogate.to_string_lossy().size() == 3);          // one U+FFFD
    static_assert(os_str{"\xE0\x80\x80"}.to_string_lossy().size() == 9);  // three U+FFFD

    // ---------------------------------------------------------------------------
    // Fixtures — clap's `Command::new("bad_utf8")` in its two parser flavours
    // ---------------------------------------------------------------------------

    consteval command_spec make_strict_positional() {
        command_builder app("bad_utf8");
        std::move(app).arg(arg_builder("arg"));
        return app.freeze();
    }
    constexpr command_spec strict_positional = make_strict_positional();

    consteval command_spec make_os_positional() {
        command_builder app("bad_utf8");
        std::move(app).arg(arg_builder("arg").value_parser<os_string>());
        return app.freeze();
    }
    constexpr command_spec os_positional = make_os_positional();

    consteval command_spec make_strict_option() {
        command_builder app("bad_utf8");
        std::move(app).arg(arg_builder("arg").short_('a').long_("arg").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec strict_option = make_strict_option();

    consteval command_spec make_os_option() {
        command_builder app("bad_utf8");
        std::move(app).arg(arg_builder("arg")
                                   .short_('a')
                                   .long_("arg")
                                   .action(arg_action::set)
                                   .value_parser<os_string>());
        return app.freeze();
    }
    constexpr command_spec os_option = make_os_option();

    /**
     * clap's bare `Command::new("bad_utf8")` — nothing is defined, so `-a` and `--arg` are
     * unknown *names*, which is a different question from an unreadable *value*.
     */
    consteval command_spec make_no_args() {
        command_builder app("bad_utf8");
        return app.freeze();
    }
    constexpr command_spec no_args = make_no_args();

    consteval command_spec make_external_default() {
        command_builder app("bad_utf8");
        std::move(app).allow_external_subcommands();
        return app.freeze();
    }
    constexpr command_spec external_default = make_external_default();

    consteval command_spec make_external_string() {
        command_builder app("bad_utf8");
        std::move(app).allow_external_subcommands().external_subcommand_value_parser<std::string>();
        return app.freeze();
    }
    constexpr command_spec external_string = make_external_string();

    /** clap's default, spelled out: `OsString` for whatever the external subcommand swallows. */
    consteval command_spec make_external_os() {
        command_builder app("bad_utf8");
        std::move(app).allow_external_subcommands().external_subcommand_value_parser<os_string>();
        return app.freeze();
    }
    constexpr command_spec external_os = make_external_os();

    // The three shapes exist and are the shapes the cases below assume. Predicates, never
    // `find_arg(...) != nullptr` — trap 10 in CLAUDE.md.
    static_assert(clapp::detail::has_positional_at(strict_positional, 1));
    static_assert(clapp::detail::has_positional_at(os_positional, 1));
    static_assert(clapp::detail::has_long_arg(strict_option, "arg"));
    static_assert(clapp::detail::has_short_arg(strict_option, 'a'));
    static_assert(clapp::detail::has_long_arg(os_option, "arg"));
    static_assert(!clapp::detail::has_short_arg(no_args, 'a'));
    static_assert(!clapp::detail::has_long_arg(no_args, "arg"));
    static_assert(external_default.is_allow_external_subcommands_set());
    static_assert(!external_default.has_external_subcommand_value_parser());
    static_assert(external_string.has_external_subcommand_value_parser());
    static_assert(external_os.has_external_subcommand_value_parser());

    // ---------------------------------------------------------------------------
    // Help-page fixtures — the "does `--help` survive undecodable content" half
    // ---------------------------------------------------------------------------

    consteval command_spec make_bad_default_value() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("path")
                                   .long_("path")
                                   .action(arg_action::set)
                                   .value_parser<os_string>()
                                   .default_value("\xFF\xFE"
                                                  "raw")
                                   .help("a path"));
        return app.freeze();
    }
    constexpr command_spec bad_default_value = make_bad_default_value();

    consteval command_spec make_bad_env() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("v")
                                   .long_("v")
                                   .action(arg_action::set)
                                   .value_parser<os_string>()
                                   .env("CLAPP_UTF8_TEST")
                                   .help("a value"));
        return app.freeze();
    }
    constexpr command_spec bad_env = make_bad_env();

    /**
     * A long name that is not UTF-8. **clap cannot express this** — its `Arg::long` takes a
     * `Str` — so there is no clap behaviour to conform to, only a clapp behaviour to pin.
     */
    consteval command_spec make_bad_long_name() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("weird")
                                   .long_("na\xFFme")
                                   .action(arg_action::set)
                                   .help("undecodable name"));
        return app.freeze();
    }
    constexpr command_spec bad_long_name = make_bad_long_name();

    /**
     * An environment whose one variable holds bytes no locale can spell. Injected rather
     * than `setenv`'d: `render_help` takes the lookup as a parameter precisely so this is a
     * pure function (ADR-0005).
     */
    struct hostile_env {
        [[nodiscard]] std::optional<std::string_view> operator()(std::string_view name) const {
            if (name == "CLAPP_UTF8_TEST")
                return std::string_view{"ok\xFF\xFE"
                                        "bad"};
            return std::nullopt;
        }
    };

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    /** Whether the one value stored for \p id is byte-for-byte \p want. */
    bool os_value_is(const arg_matches& matches, std::string_view id, os_str want) {
        const std::optional<const os_string*> found = matches.get_one<os_string>(id);
        return found.has_value() && (*found)->view() == want;
    }

    /** Whether `get_raw` — which never type-checks — hands back exactly \p want. */
    bool raw_value_is(const arg_matches& matches, std::string_view id, os_str want) {
        const std::optional<std::span<const os_string>> raw = matches.get_raw(id);
        return raw.has_value() && raw->size() == 1 && raw->front().view() == want;
    }

    std::string page(const command_spec& cmd) {
        return clapp::render_help(cmd, help_style{.use_long = clapp::long_help_exists(cmd)})
                .to_string();
    }

}  // namespace

// ===========================================================================
// Group A · the strict parser. clap: ErrorKind::InvalidUtf8. clapp: value_validation.
// ===========================================================================

CLAPP_TEST("utf8.rs::invalid_utf8_strict_positional") {
    const outcome got = clapp::parse(strict_positional, raw_args{"", bad_value});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    // The offending bytes reach the message, lossily. They are never dropped.
    CLAPP_CHECK(message_of(got).find("[arg]") != std::string::npos);
    CLAPP_CHECK(clapp::validate_utf8(message_of(got)).has_value());
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_option_short_space") {
    const outcome got = clapp::parse(strict_option, raw_args{"", "-a", bad_value});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(message_of(got).find("--arg <arg>") != std::string::npos);
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_option_short_equals") {
    const outcome got = clapp::parse(strict_option, raw_args{"", short_equals_bad});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_option_short_no_space") {
    const outcome got = clapp::parse(strict_option, raw_args{"", short_attached_bad});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_option_long_space") {
    const outcome got = clapp::parse(strict_option, raw_args{"", "--arg", bad_value});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_option_long_equals") {
    const outcome got = clapp::parse(strict_option, raw_args{"", long_equals_bad});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_invalid_short") {
    // THE NAME/VALUE SPLIT. `-a` is not defined, so the complaint is about `-a` and the
    // undecodable byte after it is never even examined. Compare with the case above,
    // where the identical byte is a value_validation.
    const outcome got = clapp::parse(no_args, raw_args{"", "-a", bad_value});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).find("'-a'") != std::string::npos);
}

CLAPP_TEST("utf8.rs::invalid_utf8_strict_invalid_long") {
    const outcome got = clapp::parse(no_args, raw_args{"", "--arg", bad_value});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).find("'--arg'") != std::string::npos);
}

// ===========================================================================
// Group B · value_parser<os_string> — clap's `value_parser!(OsString)`.
// Six shapes, one requirement: the bytes arrive unchanged.
// ===========================================================================

CLAPP_TEST("utf8.rs::invalid_utf8_positional") {
    const outcome got = clapp::parse(os_positional, raw_args{"", bad_value});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(os_value_is(*got, "arg", bad_value));
    CLAPP_CHECK(raw_value_is(*got, "arg", bad_value));
}

CLAPP_TEST("utf8.rs::invalid_utf8_option_short_space") {
    const outcome got = clapp::parse(os_option, raw_args{"", "-a", bad_value});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", bad_value));
}

CLAPP_TEST("utf8.rs::invalid_utf8_option_short_equals") {
    // `-a=<0xE9>`: the `=` is consumed as the separator, not stored. A lexer that kept it
    // would pass every other case in this group and fail only here.
    const outcome got = clapp::parse(os_option, raw_args{"", short_equals_bad});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", bad_value));
}

CLAPP_TEST("utf8.rs::invalid_utf8_option_short_no_space") {
    const outcome got = clapp::parse(os_option, raw_args{"", short_attached_bad});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", bad_value));
}

CLAPP_TEST("utf8.rs::invalid_utf8_option_long_space") {
    const outcome got = clapp::parse(os_option, raw_args{"", "--arg", bad_value});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", bad_value));
}

CLAPP_TEST("utf8.rs::invalid_utf8_option_long_equals") {
    const outcome got = clapp::parse(os_option, raw_args{"", long_equals_bad});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", bad_value));
}

// ===========================================================================
// Group C · external subcommands
// ===========================================================================

CLAPP_TEST("utf8.rs::refuse_invalid_utf8_subcommand_with_allow_external_subcommands") {
    const outcome got = clapp::parse(external_string, raw_args{"", bad_value, "normal"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    // 差异清单 entry 15's exception: THIS value_validation carries a usage line, because
    // it stands in for clap's `Error::invalid_utf8`, which does. clapp's other
    // value_validations do not — see conformance_empty_values_test.cpp.
    CLAPP_CHECK(message_of(got).find("Usage: bad_utf8") != std::string::npos);
    CLAPP_CHECK(message_of(got).find("<subcommand>") != std::string::npos);
}

CLAPP_TEST(
        "utf8.rs::refuse_invalid_utf8_subcommand_when_args_are_allowed_with_allow_external_subcommands") {
    // Same refusal with no explicit parser: the *name* is rejected before any parser is
    // consulted, so the two fixtures must agree here and disagree below.
    const outcome got = clapp::parse(external_default, raw_args{"", bad_value, "normal"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(message_of(got).find("Usage: bad_utf8") != std::string::npos);
}

CLAPP_TEST("utf8.rs::refuse_invalid_utf8_subcommand_args_with_allow_external_subcommands") {
    const outcome got = clapp::parse(
            external_string, raw_args{"", "subcommand", "normal", bad_value, "--another_normal"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(message_of(got).find("<subcommand argument>") != std::string::npos);
    // The argument-level refusal is NOT the name-level one, and does not carry a usage.
    CLAPP_CHECK(message_of(got).find("Usage:") == std::string::npos);
}

CLAPP_TEST("utf8.rs::allow_invalid_utf8_subcommand_args_with_allow_external_subcommands") {
    // DIVERGENCE, and the reason it is written as two halves rather than one.
    //
    // clap's default external parser is `OsString`, so clap's version of this test just
    // passes. clapp's default is `std::string`, which validates — so the default fixture
    // REJECTS, and clap's behaviour has to be asked for by name. Both halves are asserted
    // so that a future change of the default shows up as one failure and one pass, not as
    // silence.
    const outcome by_default = clapp::parse(
            external_default, raw_args{"", "subcommand", "normal", bad_value, "--another_normal"});
    CLAPP_CHECK(!by_default.has_value());
    CLAPP_CHECK(kind_of(by_default) == error_kind::value_validation);

    const outcome asked_for = clapp::parse(
            external_os, raw_args{"", "subcommand", "normal", bad_value, "--another_normal"});
    CLAPP_CHECK(asked_for.has_value());

    const std::optional<std::pair<std::string_view, const arg_matches&>> sub =
            asked_for->subcommand();
    CLAPP_CHECK(sub.has_value());
    CLAPP_CHECK(sub->first == "subcommand");

    const std::optional<clapp::values_ref<os_string>> values =
            sub->second.get_many<os_string>(clapp::external_id.name());
    CLAPP_CHECK(values.has_value());

    std::vector<os_string> collected;
    for (const os_string& one : *values) collected.push_back(one);
    CLAPP_CHECK(collected.size() == 3);
    CLAPP_CHECK(collected[0].view() == os_str{"normal"});
    CLAPP_CHECK(collected[1].view() == bad_value);
    CLAPP_CHECK(collected[2].view() == os_str{"--another_normal"});
}

CLAPP_TEST("utf8.rs::allow_validated_utf8_value_of") {
    const outcome got = clapp::parse(strict_option, raw_args{"test", "--arg", "me"});
    CLAPP_CHECK(got.has_value());
    const std::optional<const std::string*> value = got->get_one<std::string>("arg");
    CLAPP_CHECK(value.has_value());
    CLAPP_CHECK(**value == "me");
}

CLAPP_TEST("utf8.rs::allow_validated_utf8_external_subcommand_values_of") {
    const outcome got = clapp::parse(external_string, raw_args{"test", "cmd", "arg"});
    CLAPP_CHECK(got.has_value());
    const std::optional<std::pair<std::string_view, const arg_matches&>> sub = got->subcommand();
    CLAPP_CHECK(sub.has_value());
    const std::optional<clapp::values_ref<std::string>> values =
            sub->second.get_many<std::string>(clapp::external_id.name());
    CLAPP_CHECK(values.has_value());
    CLAPP_CHECK(std::ranges::distance(*values) == 1);
}

CLAPP_TEST("utf8.rs::panic_validated_utf8_external_subcommand_values_of_os") {
    // clap's `#[should_panic]` pair. clapp aborts on the same mistake — `get_many<T>` with
    // the wrong `T` prints both type names and calls std::abort — so the assertion is
    // written against try_get_many(), the non-fatal half of the very same check. A death
    // test would prove no more and would not run under this harness.
    const outcome got = clapp::parse(external_string, raw_args{"test", "cmd", "arg"});
    CLAPP_CHECK(got.has_value());
    const std::optional<std::pair<std::string_view, const arg_matches&>> sub = got->subcommand();
    CLAPP_CHECK(sub.has_value());
    CLAPP_CHECK(!sub->second.try_get_many<os_string>(clapp::external_id.name()).has_value());
    CLAPP_CHECK(sub->second.try_get_many<std::string>(clapp::external_id.name()).has_value());
}

CLAPP_TEST("utf8.rs::allow_invalid_utf8_external_subcommand_values_of_os") {
    const outcome got = clapp::parse(external_os, raw_args{"test", "cmd", "arg"});
    CLAPP_CHECK(got.has_value());
    const std::optional<std::pair<std::string_view, const arg_matches&>> sub = got->subcommand();
    CLAPP_CHECK(sub.has_value());
    const std::optional<clapp::values_ref<os_string>> values =
            sub->second.get_many<os_string>(clapp::external_id.name());
    CLAPP_CHECK(values.has_value());
    CLAPP_CHECK(std::ranges::distance(*values) == 1);
}

CLAPP_TEST("utf8.rs::panic_invalid_utf8_external_subcommand_values_of") {
    // The mirror of the previous should_panic: with an `os_string` parser it is
    // `std::string` that is the wrong type.
    const outcome got = clapp::parse(external_os, raw_args{"test", "cmd", "arg"});
    CLAPP_CHECK(got.has_value());
    const std::optional<std::pair<std::string_view, const arg_matches&>> sub = got->subcommand();
    CLAPP_CHECK(sub.has_value());
    CLAPP_CHECK(!sub->second.try_get_many<std::string>(clapp::external_id.name()).has_value());
    CLAPP_CHECK(sub->second.try_get_many<os_string>(clapp::external_id.name()).has_value());
}

// ===========================================================================
// Group D · clapp-only coverage. None of this exists in clap, because none of it can:
// clap's `Arg::long`, `PossibleValue` and `Str` are all UTF-8 by type.
// ===========================================================================

CLAPP_TEST("utf8: the strict parser refuses, but the bytes are still there") {
    // The claim the whole design rests on: strictness lives in the value parser, and
    // nothing below it ever loses a byte. Asserted at the seam the parser reads from.
    const raw_args raw{"", "--arg", bad_value};
    CLAPP_CHECK(raw.size() == 3);
    CLAPP_CHECK(raw.items()[2].view() == bad_value);
    CLAPP_CHECK(!raw.items()[2].is_utf8());

    const outcome got = clapp::parse(strict_option, raw);
    CLAPP_CHECK(!got.has_value());
}

CLAPP_TEST("utf8: a well-formed WTF-8 surrogate round-trips through os_string") {
    // The Windows argument that has no UTF-8 spelling at all. It must survive as one
    // value, be rejected by `std::string`, and render as a single U+FFFD.
    const outcome kept = clapp::parse(os_option, raw_args{"", "--arg", lone_surrogate});
    CLAPP_CHECK(kept.has_value());
    CLAPP_CHECK(os_value_is(*kept, "arg", lone_surrogate));
    CLAPP_CHECK((*kept->get_one<os_string>("arg"))->is_wtf8());
    CLAPP_CHECK(!(*kept->get_one<os_string>("arg"))->is_utf8());

    const outcome refused = clapp::parse(strict_option, raw_args{"", "--arg", lone_surrogate});
    CLAPP_CHECK(!refused.has_value());
    CLAPP_CHECK(kind_of(refused) == error_kind::value_validation);

    // 差异清单 entry 10, at the level a user sees: one replacement character, not three.
    const std::string rendered = message_of(refused);
    CLAPP_CHECK(clapp::validate_utf8(rendered).has_value());
    CLAPP_CHECK(rendered.find("\xEF\xBF\xBD\xEF\xBF\xBD") == std::string::npos);
}

CLAPP_TEST("utf8: a multi-byte character is not a short flag") {
    // 差异清单 entry 12. clap's `Arg::short` takes a Rust `char`, so `-é` is a legal short
    // flag there (`opts.rs::short_non_ascii_no_space`); clapp's takes one `char` byte, to
    // agree with the byte-wise cluster walk in short_flags. `-é` is therefore unknown, and
    // the diagnostic still spells it back as a character rather than as two bytes.
    const outcome got = clapp::parse(no_args, raw_args{"prog", "-\xC3\xA9"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).find("-\xC3\xA9") != std::string::npos);
    CLAPP_CHECK(clapp::validate_utf8(message_of(got)).has_value());
}

CLAPP_TEST("utf8: a multi-byte character IS a legal attached value") {
    // The other half of the same byte sequence: after a short option that takes a value,
    // `-aé` is `-a` with the value `é`, and nothing about it is exotic.
    const outcome got = clapp::parse(os_option, raw_args{"prog", "-a\xC3\xA9"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{"\xC3\xA9"}));
}

CLAPP_TEST("utf8: an undecodable byte in a short cluster is reported as one unknown flag") {
    const outcome got = clapp::parse(no_args, raw_args{"prog", "-\xFF"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(clapp::validate_utf8(message_of(got)).has_value());
}

// ---------------------------------------------------------------------------
// `--help` under undecodable content
// ---------------------------------------------------------------------------

CLAPP_TEST("utf8: help_page_survives_an_undecodable_default") {
    // WAS FAILING before this port: the raw `FF FE` went straight to the terminal and the
    // page was not valid UTF-8. clap converts with `to_string_lossy()`; clapp now does.
    const std::string rendered = page(bad_default_value);
    CLAPP_CHECK(clapp::validate_utf8(rendered).has_value());
    CLAPP_CHECK(rendered.find("--path") != std::string::npos);  // not dropped
    CLAPP_CHECK(rendered.find("a path") != std::string::npos);  // help kept
    CLAPP_CHECK(rendered.find("[default: ") != std::string::npos);
    CLAPP_CHECK(rendered.find("raw]") != std::string::npos);  // the decodable tail
}

CLAPP_TEST("utf8: help_page_survives_an_undecodable_env_value") {
    // The realistic case, and the reason the fix is not cosmetic: a default value is the
    // program's own literal, but an environment variable is the *user's* bytes.
    const std::string rendered =
            clapp::render_help(bad_env,
                               help_style{.use_long = clapp::long_help_exists(bad_env)},
                               hostile_env{})
                    .to_string();
    CLAPP_CHECK(clapp::validate_utf8(rendered).has_value());
    CLAPP_CHECK(rendered.find("[env: CLAPP_UTF8_TEST=ok") != std::string::npos);
    CLAPP_CHECK(rendered.find("bad]") != std::string::npos);
    CLAPP_CHECK(rendered.find("a value") != std::string::npos);
}

CLAPP_TEST("utf8: help_page_keeps_an_undecodable_argument_name") {
    // PINNED, NOT ENDORSED. clapp's `long_` takes a `std::string_view`, so a long name
    // that is not UTF-8 is expressible where clap's `Str` forbids it. What must hold:
    //
    //   * the argument is NOT dropped from the page — it has its row and its help;
    //   * the name is spelled with the stored bytes, so it is not silently renamed.
    //
    // The page is consequently not valid UTF-8, which is the one place clapp's output can
    // still be ill-formed. It is not fixed here because the bytes came from the *program*,
    // not from the user or the environment, and rewriting them would hide a definition
    // error behind a replacement character — the argument would then be unspellable AND
    // look fine. The parse half below is what makes that verdict concrete.
    const std::string rendered = page(bad_long_name);
    CLAPP_CHECK(rendered.find("undecodable name") != std::string::npos);
    CLAPP_CHECK(rendered.find("na\xFFme") != std::string::npos);
    CLAPP_CHECK(!clapp::validate_utf8(rendered).has_value());
}

// ---------------------------------------------------------------------------
// The utf16.rs half
//
// clap keeps a *separate* `tests/builder/utf16.rs`, `#![cfg(windows)]`, because on
// Windows its `OsString` is UTF-16 and the Unix file cannot even be compiled. clapp needs
// no such file — WTF-8 on every platform (ADR-0003) — but that is a claim, and this block
// is what makes it checkable HERE, on POSIX, without a Windows API in sight.
//
// The trick is that the Windows entry point is one conversion, `utf16_to_wtf8`, and it is
// a plain platform-independent template. So the shape utf16.rs builds —
// `bad_osstring(ascii)`: ASCII code units with a dangling low surrogate appended — can be
// built and converted right here, and the six syntactic shapes run against the result.
//
// **This matters more than a normal port, because `raw_args::from_args()`'s Windows
// branch has never been compiled** (see its own \note table). utf16.rs is the only
// specification that branch has; everything it pins other than the `CommandLineToArgvW`
// call itself is pinned below.
// ---------------------------------------------------------------------------

namespace {

    /**
     * clap's `bad_osstring`: ASCII widened to UTF-16 with an unpaired U+DC00 appended, then
     * put through the conversion `os_string::from_native()` performs on Windows.
     *
     * \note U+DC00 is a *low* surrogate with no high one before it — invalid UTF-16, which
     *       Windows nevertheless hands out, and the exact case WTF-8 exists to carry.
     */
    std::string bad_os_string(std::u16string_view ascii) {
        std::u16string units{ascii};
        units.push_back(u'\xDC00');
        return clapp::detail::utf16_to_wtf8<char16_t>(units);
    }

    /** The value every case below must end up holding: U+DC00 alone, in WTF-8. */
    const std::string lone_low_surrogate = bad_os_string(u"");

}  // namespace

CLAPP_TEST("utf16.rs::bad_osstring is invalid Unicode but well-formed WTF-8") {
    // clap's `assert!(os.to_str().is_none(), "invalid Unicode")`, plus the half clap
    // cannot state: it IS well-formed in clapp's storage, which is why nothing is lost.
    const os_str value{lone_low_surrogate};
    CLAPP_CHECK(!value.is_utf8());
    CLAPP_CHECK(value.is_wtf8());
    CLAPP_CHECK(value.size() == 3);

    // And it converts back to precisely the UTF-16 it came from — the return leg of the
    // Windows trip. A conversion that merged, dropped or substituted the surrogate would
    // still pass every byte-wise case below and fail only here.
    CLAPP_CHECK(clapp::detail::wtf8_to_utf16<char16_t>(lone_low_surrogate) ==
                std::u16string{u'\xDC00'});
}

CLAPP_TEST("utf16.rs::invalid_utf16_positional") {
    const outcome got = clapp::parse(os_positional, raw_args{"", lone_low_surrogate});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{lone_low_surrogate}));
}

CLAPP_TEST("utf16.rs::invalid_utf16_option_short_space") {
    const outcome got = clapp::parse(os_option, raw_args{"", "-a", lone_low_surrogate});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{lone_low_surrogate}));
}

CLAPP_TEST("utf16.rs::invalid_utf16_option_short_equals") {
    // clap's `bad_osstring(b"-a=")`. The `=` is an ASCII byte inside what was a UTF-16
    // string; after conversion the lexer finds it by byte, which is the whole reason the
    // conversion happens at the entry point and nowhere else.
    const outcome got = clapp::parse(os_option, raw_args{"", bad_os_string(u"-a=")});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{lone_low_surrogate}));
}

CLAPP_TEST("utf16.rs::invalid_utf16_option_short_no_space") {
    const outcome got = clapp::parse(os_option, raw_args{"", bad_os_string(u"-a")});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{lone_low_surrogate}));
}

CLAPP_TEST("utf16.rs::invalid_utf16_option_long_space") {
    const outcome got = clapp::parse(os_option, raw_args{"", "--arg", lone_low_surrogate});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{lone_low_surrogate}));
}

CLAPP_TEST("utf16.rs::invalid_utf16_option_long_equals") {
    const outcome got = clapp::parse(os_option, raw_args{"", bad_os_string(u"--arg=")});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(os_value_is(*got, "arg", os_str{lone_low_surrogate}));
}

CLAPP_TEST("utf16: the strict parser refuses a surrogate, and says where") {
    // clap's file omits its strict cases with the comment "that's a Unix-only feature".
    // In clapp it is not Unix-only, because the storage is the same on both platforms —
    // so the case exists here, and its classification is the one thing that differs from
    // the 0xE9 cases above: `encoding_error::surrogate`, not `truncated`.
    const outcome got = clapp::parse(strict_option, raw_args{"", "--arg", lone_low_surrogate});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(os_str{lone_low_surrogate}.to_string_view().error().kind ==
                clapp::encoding_error::surrogate);
    CLAPP_CHECK(clapp::validate_utf8(message_of(got)).has_value());
}

CLAPP_TEST("utf8: an undecodable argument name can never be matched") {
    // The consequence, and clap's behaviour exactly — clap's `parse_long_arg` returns
    // `NoMatchingArg` for `Err(long_arg)` without consulting the table at all. Typing the
    // *identical bytes* therefore does not match, which is why defining such a name is a
    // mistake rather than a feature.
    const outcome got = clapp::parse(bad_long_name, raw_args{"prog", "--na\xFFme", "v"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}
