#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// clap's NonEmptyStringValueParser, as a clapp value_parser specialization
// ---------------------------------------------------------------------------
//
// clapp's `value_parser<std::string>` accepts "" on purpose (`--name ""` is a legitimate
// empty name), so the two clap cases that need rejection bring their own type. This is
// the documented extension point, and using it here also proves it works from outside
// the library.

namespace {
    struct non_empty_string {
        std::string text;
    };
}  // namespace

namespace clapp {
    template<>
    struct value_parser<non_empty_string> {
        [[nodiscard]] static constexpr std::expected<non_empty_string, parse_error>
        parse(os_str value) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value())
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                                   .input     = value,
                                                   .type_name = "non_empty_string",
                                                   .reason    = "invalid UTF-8"});
            if (text->empty())
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                                   .input     = value,
                                                   .type_name = "non_empty_string",
                                                   .reason    = "value must not be empty"});
            return non_empty_string{std::string(*text)};
        }

        [[nodiscard]] static constexpr std::span<const possible_value> possible_values() noexcept {
            return {};
        }
    };
}  // namespace clapp

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    /**
     * \brief Whole-block comparison against clap's expected string, printing both on a
     *        mismatch. Used wherever clap itself compares the whole block: a substring check
     *        would also pass on a message whose `Usage:` line or `tip:` line has gone
     *        missing, which is exactly the regression these cases exist to catch.
     */
    bool same_block(const outcome& got, std::string_view want) {
        const std::string text = message_of(got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    std::vector<std::string> raw_of(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    // clap's `arg!(x: -x [name])` is an option whose value is optional: num_args(0..=1).
    // `arg!(x: -x <name>)` is one required value. Both spellings appear below and the
    // difference is load-bearing, so each fixture says which it is.

    consteval command_spec make_require_equals_nonempty() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("cfg")
                                   .long_("config")
                                   .require_equals()
                                   .action(arg_action::set)
                                   .value_parser<non_empty_string>());
        return app.freeze();
    }
    constexpr command_spec require_equals_nonempty = make_require_equals_nonempty();

    consteval command_spec make_require_equals_plain() {
        command_builder app("prog");
        std::move(app).arg(
                arg_builder("cfg").long_("config").require_equals().action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec require_equals_plain = make_require_equals_plain();

    consteval command_spec make_require_equals_nonempty_with_positional() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("cfg")
                             .long_("config")
                             .require_equals()
                             .action(arg_action::set)
                             .value_parser<non_empty_string>())
                .arg(arg_builder("some").index(1));
        return app.freeze();
    }
    constexpr command_spec require_equals_nonempty_pos =
            make_require_equals_nonempty_with_positional();

    consteval command_spec make_require_equals_min_zero() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("cfg")
                             .long_("config")
                             .action(arg_action::set)
                             .require_equals()
                             .num_args(value_range::at_least(0)))
                .arg(arg_builder("cmd").index(1));
        return app.freeze();
    }
    constexpr command_spec require_equals_min_zero = make_require_equals_min_zero();

    consteval command_spec make_hyphen_config() {
        command_builder app("prog");
        std::move(app).arg(
                arg_builder("cfg").long_("config").action(arg_action::set).allow_hyphen_values());
        return app.freeze();
    }
    constexpr command_spec hyphen_config = make_hyphen_config();

    consteval command_spec make_two_opts_short() {
        command_builder app("opts");
        std::move(app)
                .arg(arg_builder("f")
                             .short_('f')
                             .num_args(value_range::optional())
                             .value_name("flag"))
                .arg(arg_builder("c")
                             .short_('c')
                             .num_args(value_range::optional())
                             .value_name("color"));
        return app.freeze();
    }
    constexpr command_spec two_opts_short = make_two_opts_short();

    consteval command_spec make_two_opts_long() {
        command_builder app("opts");
        std::move(app)
                .arg(arg_builder("flag")
                             .long_("flag")
                             .num_args(value_range::optional())
                             .value_name("flag"))
                .arg(arg_builder("color")
                             .long_("color")
                             .num_args(value_range::optional())
                             .value_name("color"));
        return app.freeze();
    }
    constexpr command_spec two_opts_long = make_two_opts_long();

    consteval command_spec make_two_opts_mixed() {
        command_builder app("opts");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .num_args(value_range::optional())
                             .value_name("flag"))
                .arg(arg_builder("color")
                             .short_('c')
                             .long_("color")
                             .num_args(value_range::optional())
                             .value_name("color"));
        return app.freeze();
    }
    constexpr command_spec two_opts_mixed = make_two_opts_mixed();

    consteval command_spec make_lots_o_vals() {
        command_builder app("opts");
        std::move(app).arg(
                arg_builder("o").short_('o').num_args(value_range::at_least(1)).required());
        return app.freeze();
    }
    constexpr command_spec lots_o_vals = make_lots_o_vals();

    consteval command_spec make_defaulted_opt() {
        command_builder app("df");
        std::move(app).arg(arg_builder("o")
                                   .short_('o')
                                   .num_args(value_range::optional())
                                   .default_value("default"));
        return app.freeze();
    }
    constexpr command_spec defaulted_opt = make_defaulted_opt();

    consteval command_spec make_multi_with_positional() {
        command_builder app("mvae");
        std::move(app)
                .arg(arg_builder("o")
                             .short_('o')
                             .action(arg_action::append)
                             .num_args(value_range::optional()))
                .arg(arg_builder("file").index(1));
        return app.freeze();
    }
    constexpr command_spec multi_with_positional = make_multi_with_positional();

    consteval command_spec make_delimited_optional() {
        command_builder app("mvae");
        std::move(app)
                .arg(arg_builder("o")
                             .short_('o')
                             .action(arg_action::append)
                             .num_args(value_range::optional())
                             .value_delimiter(','))
                .arg(arg_builder("file").index(1));
        return app.freeze();
    }
    constexpr command_spec delimited_optional = make_delimited_optional();

    consteval command_spec make_delimited_required() {
        command_builder app("mvae");
        std::move(app)
                .arg(arg_builder("o").short_('o').value_delimiter(',').required())
                .arg(arg_builder("file").index(1));
        return app.freeze();
    }
    constexpr command_spec delimited_required = make_delimited_required();

    consteval command_spec make_hyphen_multi() {
        command_builder app("mvae");
        std::move(app).arg(arg_builder("o")
                                   .short_('o')
                                   .required()
                                   .num_args(value_range::at_least(1))
                                   .allow_hyphen_values());
        return app.freeze();
    }
    constexpr command_spec hyphen_multi = make_hyphen_multi();

    consteval command_spec make_plain_required_opt() {
        command_builder app("mvae");
        std::move(app).arg(arg_builder("o").short_('o').required());
        return app.freeze();
    }
    constexpr command_spec plain_required_opt = make_plain_required_opt();

    consteval command_spec make_hyphen_multi_with_flag() {
        command_builder app("mvae");
        std::move(app)
                .arg(arg_builder("o")
                             .short_('o')
                             .required()
                             .num_args(value_range::at_least(1))
                             .allow_hyphen_values())
                .arg(arg_builder("f").short_('f').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec hyphen_multi_with_flag = make_hyphen_multi_with_flag();

    consteval command_spec make_hyphen_optional_with_flag() {
        command_builder app("mvae");
        std::move(app)
                .arg(arg_builder("o")
                             .short_('o')
                             .action(arg_action::append)
                             .num_args(value_range::optional())
                             .allow_hyphen_values())
                .arg(arg_builder("f").short_('f').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec hyphen_optional_with_flag = make_hyphen_optional_with_flag();

    consteval command_spec make_hyphen_then_positional() {
        command_builder app("mvae");
        std::move(app)
                .arg(arg_builder("o").short_('o').action(arg_action::set).allow_hyphen_values())
                .arg(arg_builder("arg").index(1));
        return app.freeze();
    }
    constexpr command_spec hyphen_then_positional = make_hyphen_then_positional();

    consteval command_spec make_min_zero_default_missing() {
        command_builder app("foo");
        std::move(app).arg(arg_builder("del")
                                   .short_('d')
                                   .long_("del")
                                   .action(arg_action::set)
                                   .require_equals()
                                   .num_args(value_range::at_least(0))
                                   .default_missing_value("default"));
        return app.freeze();
    }
    constexpr command_spec min_zero_default_missing = make_min_zero_default_missing();

    // clap's `issue_1105_setup`.
    consteval command_spec make_issue_1105() {
        command_builder app("opts");
        std::move(app)
                .arg(arg_builder("option").short_('o').long_("option").required())
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec issue_1105 = make_issue_1105();

    consteval command_spec make_short_eq() {
        command_builder app("cmd");
        std::move(app).arg(arg_builder("opt").short_('f').required());
        return app.freeze();
    }
    constexpr command_spec short_eq = make_short_eq();

    consteval command_spec make_long_eq() {
        command_builder app("cmd");
        std::move(app).arg(arg_builder("opt").long_("foo").required());
        return app.freeze();
    }
    constexpr command_spec long_eq = make_long_eq();

    consteval command_spec make_default_only() {
        command_builder app("test");
        std::move(app).next_help_heading("test").arg(
                arg_builder("a").long_("a").default_value("32"));
        return app.freeze();
    }
    constexpr command_spec default_only = make_default_only();

    // clap's issue_2279: a `next_help_heading` before and after the argument must not eat
    // the default. Two fixtures, exactly as clap has two commands.
    consteval command_spec make_heading_after() {
        command_builder app("cmd");
        std::move(app)
                .arg(arg_builder("foo").short_('f').default_value("bar"))
                .next_help_heading("This causes default_value to be ignored");
        return app.freeze();
    }
    constexpr command_spec heading_after = make_heading_after();

    consteval command_spec make_heading_before() {
        command_builder app("cmd");
        std::move(app)
                .next_help_heading("This causes default_value to be ignored")
                .arg(arg_builder("foo").short_('f').default_value("bar"));
        return app.freeze();
    }
    constexpr command_spec heading_before = make_heading_before();

    consteval command_spec make_infer_race() {
        command_builder app("test");
        std::move(app)
                .infer_long_args()
                .arg(arg_builder("racetrack")
                             .long_("racetrack")
                             .alias("autobahn")
                             .action(arg_action::set_true))
                .arg(arg_builder("racecar").long_("racecar").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec infer_race = make_infer_race();

    consteval command_spec make_infer_one() {
        command_builder app("test");
        std::move(app).infer_long_args().arg(
                arg_builder("arg").long_("arg").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec infer_one = make_infer_one();

    consteval command_spec make_infer_exact() {
        command_builder app("test");
        std::move(app)
                .infer_long_args()
                .arg(arg_builder("arg").long_("arg").action(arg_action::set_true))
                .arg(arg_builder("arg2").long_("arg2").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec infer_exact = make_infer_exact();

    consteval command_spec make_infer_aliases() {
        command_builder app("test");
        std::move(app).infer_long_args().arg(arg_builder("abc-123")
                                                     .long_("abc-123")
                                                     .aliases({"a", "abc-xyz"})
                                                     .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec infer_aliases = make_infer_aliases();

    consteval command_spec make_infer_ambiguous() {
        command_builder app("test");
        std::move(app)
                .infer_long_args()
                .arg(arg_builder("abc-123").long_("abc-123").action(arg_action::set_true))
                .arg(arg_builder("abc-xyz").long_("abc-xyz").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec infer_ambiguous = make_infer_ambiguous();

    // --- clap's shared `utils::complex_app()` -----------------------------------------
    //
    // clap keeps one 15-argument command and reuses it across its whole suite; the two
    // suggestion cases below are the ones in THIS file that need it, and they need it
    // precisely because the tip has to be chosen against a crowded namespace — `--optio` is
    // one edit from `--option` and two from `--option3`, `--optvaleq` and `--optvalnoeq`.
    // A two-argument fixture would let a much worse ranking function pass.
    //
    // One deliberate simplification: clap hangs `["fast", "slow"]` / `["vi", "emacs"]` on the
    // ARGUMENT via `.value_parser([..])`, while in clapp a value domain belongs to the TYPE.
    // Reproducing it here would mean two one-off types whose possible values no case in this
    // file reads. `conformance_possible_values_test.cpp` is where that mechanism is pinned.
    consteval command_spec make_complex_app() {
        command_builder app("clap-test");
        std::move(app)
                .version("v1.4.8")
                .about("tests clap library")
                .author("Kevin K. <kbknapp@gmail.com>")
                .arg(arg_builder("option")
                             .short_('o')
                             .long_("option")
                             .value_name("opt")
                             .help("tests options")
                             .required(false)
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("positional").index(1).help("tests positionals"))
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("tests flags")
                             .action(arg_action::count)
                             .global())
                .arg(arg_builder("flag2")
                             .short_('F')
                             .help("tests flags with exclusions")
                             .conflicts_with("flag")
                             .requires_("long-option-2")
                             .action(arg_action::set_true))
                .arg(arg_builder("long-option-2")
                             .long_("long-option-2")
                             .value_name("option2")
                             .help("tests long options with exclusions")
                             .conflicts_with("option")
                             .requires_("positional2")
                             .action(arg_action::set))
                .arg(arg_builder("positional2").index(2).help("tests positionals with exclusions"))
                .arg(arg_builder("option3")
                             .short_('O')
                             .long_("option3")
                             .value_name("option3")
                             .help("specific vals")
                             .action(arg_action::set))
                .arg(arg_builder("positional3")
                             .index(3)
                             .help("tests specific values")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("multvals")
                             .long_("multvals")
                             .help("Tests multiple values, not mult occs")
                             .value_names({"one", "two"}))
                .arg(arg_builder("multvalsmo")
                             .long_("multvalsmo")
                             .help("Tests multiple values, and mult occs")
                             .value_names({"one", "two"})
                             .action(arg_action::append))
                .arg(arg_builder("minvals2")
                             .long_("minvals2")
                             .value_name("minvals")
                             .help("Tests 2 min vals")
                             .num_args(value_range::at_least(2)))
                .arg(arg_builder("maxvals3")
                             .long_("maxvals3")
                             .value_name("maxvals")
                             .help("Tests 3 max vals")
                             .num_args(value_range{1, 3}))
                .arg(arg_builder("optvaleq")
                             .long_("optvaleq")
                             .value_name("optval")
                             .help("Tests optional value, require = sign")
                             .num_args(value_range{0, 1})
                             .require_equals())
                .arg(arg_builder("optvalnoeq")
                             .long_("optvalnoeq")
                             .value_name("optval")
                             .help("Tests optional value")
                             .num_args(value_range{0, 1}))
                .subcommand(command_builder("subcmd")
                                    .about("tests subcommands")
                                    .version("0.1")
                                    .arg(arg_builder("option")
                                                 .short_('o')
                                                 .long_("option")
                                                 .value_name("scoption")
                                                 .help("tests options")
                                                 .num_args(value_range::at_least(1)))
                                    .arg(arg_builder("subcmdarg")
                                                 .short_('s')
                                                 .long_("subcmdarg")
                                                 .value_name("subcmdarg")
                                                 .help("tests other args")
                                                 .action(arg_action::set))
                                    .arg(arg_builder("scpositional")
                                                 .index(1)
                                                 .help("tests positionals")));
        return app.freeze();
    }
    constexpr command_spec complex_app = make_complex_app();

    // clap's issue #616: `--files-without-matches` differs from `--files-without-match` by
    // one character AND from `--files-with-matches` by one character. The naive answer is the
    // first candidate in declaration order, which is the wrong one.
    consteval command_spec make_ripgrep_616() {
        command_builder app("ripgrep-616");
        std::move(app)
                .arg(arg_builder("files-with-matches")
                             .long_("files-with-matches")
                             .action(arg_action::set_true))
                .arg(arg_builder("files-without-match")
                             .long_("files-without-match")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec ripgrep_616 = make_ripgrep_616();

    static_assert(require_equals_plain.find_arg("cfg")->is_require_equals_set());
    static_assert(hyphen_config.find_arg("cfg")->is_allow_hyphen_values_set());
    static_assert(infer_race.is_infer_long_args_set());
    static_assert(delimited_required.find_arg("o")->get_value_delimiter() ==
                  std::optional<char>{','});

    std::vector<std::string>
    repeated(std::string_view head, std::string_view word, std::size_t count) {
        std::vector<std::string> out;
        out.emplace_back("");
        out.emplace_back(head);
        for (std::size_t i = 0; i < count; ++i) out.emplace_back(word);
        return out;
    }

}  // namespace

// ---------------------------------------------------------------------------
// require_equals
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::require_equals_fail") {
    const outcome got =
            clapp::parse(require_equals_nonempty, raw_args{"prog", "--config", "file.conf"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::no_equals);
}

CLAPP_TEST("opts.rs::require_equals_fail_message") {
    // Whole block. Note the argument is spelled `--config=<cfg>` WITH the equals sign —
    // the message shows the form the user should have typed, not the form they did.
    const outcome got =
            clapp::parse(require_equals_plain, raw_args{"prog", "--config", "file.conf"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::no_equals);
    CLAPP_CHECK(same_block(got,
                           "error: equal sign is needed when assigning values to "
                           "'--config=<cfg>'\n"
                           "\n"
                           "Usage: prog [OPTIONS]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("opts.rs::require_equals_min_values_zero") {
    // `--config` with no `=` supplies no values at all, so `cmd` gets the next token.
    const outcome got = clapp::parse(require_equals_min_zero, raw_args{"prog", "--config", "cmd"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("cfg"));
    CLAPP_CHECK(one_string(*got, "cmd") == std::optional<std::string>{"cmd"});
}

CLAPP_TEST("opts.rs::require_equals_no_empty_values_fail") {
    const outcome got =
            clapp::parse(require_equals_nonempty_pos, raw_args{"prog", "--config=", "file.conf"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
}

CLAPP_TEST("opts.rs::require_equals_empty_vals_pass") {
    // The mirror of the case above: with no non-empty requirement, `--config=` is a
    // perfectly good empty value.
    const outcome got = clapp::parse(require_equals_plain, raw_args{"prog", "--config="});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "cfg") == std::optional<std::string>{""});
}

CLAPP_TEST("opts.rs::require_equals_pass") {
    const outcome got = clapp::parse(require_equals_plain, raw_args{"prog", "--config=file.conf"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "cfg") == std::optional<std::string>{"file.conf"});
}

// ---------------------------------------------------------------------------
// Hyphens that are values
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::double_hyphen_as_value") {
    const outcome got = clapp::parse(hyphen_config, raw_args{"prog", "--config", "--"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "cfg") == std::optional<std::string>{"--"});
}

CLAPP_TEST("opts.rs::stdin_char") {
    const outcome got = clapp::parse(two_opts_short, raw_args{"", "-f", "-"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("f"));
    CLAPP_CHECK(one_string(*got, "f") == std::optional<std::string>{"-"});
}

// ---------------------------------------------------------------------------
// The four spellings, again, with values
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::opts_using_short") {
    const outcome got = clapp::parse(two_opts_short, raw_args{"", "-f", "some", "-c", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("f"));
    CLAPP_CHECK(one_string(*got, "f") == std::optional<std::string>{"some"});
    CLAPP_CHECK(got->contains_id("c"));
    CLAPP_CHECK(one_string(*got, "c") == std::optional<std::string>{"other"});
}

CLAPP_TEST("opts.rs::opts_using_long_space") {
    const outcome got =
            clapp::parse(two_opts_long, raw_args{"", "--flag", "some", "--color", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"some"});
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"other"});
}

CLAPP_TEST("opts.rs::opts_using_long_equals") {
    const outcome got = clapp::parse(two_opts_long, raw_args{"", "--flag=some", "--color=other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"some"});
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"other"});
}

CLAPP_TEST("opts.rs::opts_using_mixed") {
    const outcome got =
            clapp::parse(two_opts_mixed, raw_args{"", "-f", "some", "--color", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"some"});
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"other"});
}

CLAPP_TEST("opts.rs::opts_using_mixed2") {
    const outcome got = clapp::parse(two_opts_mixed, raw_args{"", "--flag=some", "-c", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"some"});
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"other"});
}

CLAPP_TEST("opts.rs::lots_o_vals") {
    // 297 values on one option: clap's comment says "i.e. more than u8", and it is there
    // because a count that lives in a byte silently wraps at 256.
    const raw_args line{std::from_range, repeated("-o", "some", 297)};
    const outcome got = clapp::parse(lots_o_vals, line);
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("o"));
    CLAPP_CHECK(raw_of(*got, "o").size() == 297);
}

// ---------------------------------------------------------------------------
// Defaults and delimiters
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::default_values_user_value") {
    const outcome got = clapp::parse(defaulted_opt, raw_args{"", "-o", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("o"));
    CLAPP_CHECK(one_string(*got, "o") == std::optional<std::string>{"value"});
}

CLAPP_TEST("opts.rs::multiple_vals_pos_arg_equals") {
    const outcome got = clapp::parse(multi_with_positional, raw_args{"", "-o=1", "some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "o") == std::optional<std::string>{"1"});
    CLAPP_CHECK(one_string(*got, "file") == std::optional<std::string>{"some"});
}

CLAPP_TEST("opts.rs::require_delims_no_delim") {
    // `-o 1 2 some`: the option takes ONE value, so `2` starts the positionals, `some`
    // has nowhere to go, and clap reports it as an unknown argument.
    const outcome got = clapp::parse(delimited_optional, raw_args{"mvae", "-o", "1", "2", "some"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("opts.rs::require_delims") {
    const outcome got = clapp::parse(delimited_required, raw_args{"", "-o", "1,2", "some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "o") == std::vector<std::string>{"1", "2"});
    CLAPP_CHECK(one_string(*got, "file") == std::optional<std::string>{"some"});
}

// ---------------------------------------------------------------------------
// allow_hyphen_values, in both orders
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::leading_hyphen_pass") {
    const outcome got = clapp::parse(hyphen_multi, raw_args{"", "-o", "-2", "3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "o") == std::vector<std::string>{"-2", "3"});
}

CLAPP_TEST("opts.rs::leading_hyphen_fail") {
    const outcome got = clapp::parse(plain_required_opt, raw_args{"", "-o", "-2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("opts.rs::leading_hyphen_with_flag_after") {
    // `-f` is swallowed as data because `-o` is still collecting.
    const outcome got = clapp::parse(hyphen_multi_with_flag, raw_args{"", "-o", "-2", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "o") == std::vector<std::string>{"-2", "-f"});
    CLAPP_CHECK(!got->get_flag("f"));
}

CLAPP_TEST("opts.rs::leading_hyphen_with_flag_before") {
    // The same two arguments in the other order: `-f` fires, then `-o` takes `-2`.
    const outcome got = clapp::parse(hyphen_optional_with_flag, raw_args{"", "-f", "-o", "-2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "o") == std::vector<std::string>{"-2"});
    CLAPP_CHECK(got->get_flag("f"));
}

CLAPP_TEST("opts.rs::leading_hyphen_with_only_pos_follows") {
    const outcome got = clapp::parse(hyphen_then_positional, raw_args{"", "-o", "-2", "--", "val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "o") == std::vector<std::string>{"-2"});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"val"});
}

// ---------------------------------------------------------------------------
// Empty values — clap issue #1105, all six spellings
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::issue_1105_empty_value_long_fail") {
    const outcome got = clapp::parse(issue_1105, raw_args{"cmd", "--option", "--flag"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

CLAPP_TEST("opts.rs::issue_1105_empty_value_long_explicit") {
    const outcome got = clapp::parse(issue_1105, raw_args{"cmd", "--option", ""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{""});
}

CLAPP_TEST("opts.rs::issue_1105_empty_value_long_equals") {
    const outcome got = clapp::parse(issue_1105, raw_args{"cmd", "--option="});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{""});
}

CLAPP_TEST("opts.rs::issue_1105_empty_value_short_fail") {
    const outcome got = clapp::parse(issue_1105, raw_args{"cmd", "-o", "--flag"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

CLAPP_TEST("opts.rs::issue_1105_empty_value_short_explicit") {
    const outcome got = clapp::parse(issue_1105, raw_args{"cmd", "-o", ""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{""});
}

CLAPP_TEST("opts.rs::issue_1105_empty_value_short_equals") {
    const outcome got = clapp::parse(issue_1105, raw_args{"cmd", "-o="});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{""});
}

// ---------------------------------------------------------------------------
// `=` inside a value
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::short_eq_val_starts_with_eq") {
    const outcome got = clapp::parse(short_eq, raw_args{"test", "-f==value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"=value"});
}

CLAPP_TEST("opts.rs::long_eq_val_starts_with_eq") {
    const outcome got = clapp::parse(long_eq, raw_args{"test", "--foo==value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"=value"});
}

// ---------------------------------------------------------------------------
// default_missing_value with min-zero
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::issue_1047_min_zero_vals_default_val") {
    const outcome got = clapp::parse(min_zero_default_missing, raw_args{"foo", "-d"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "del") == std::optional<std::string>{"default"});
}

// ---------------------------------------------------------------------------
// Defaults survive help-heading bookkeeping
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::issue_2022_get_flags_misuse") {
    const outcome got = clapp::parse(default_only, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "a") == std::optional<std::string>{"32"});
}

CLAPP_TEST("opts.rs::issue_2279") {
    const outcome after = clapp::parse(heading_after, raw_args{""});
    CLAPP_CHECK(after.has_value());
    CLAPP_CHECK(one_string(*after, "foo") == std::optional<std::string>{"bar"});

    const outcome before = clapp::parse(heading_before, raw_args{""});
    CLAPP_CHECK(before.has_value());
    CLAPP_CHECK(one_string(*before, "foo") == std::optional<std::string>{"bar"});
}

// ---------------------------------------------------------------------------
// infer_long_args
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::infer_long_arg_pass") {
    const outcome racecar = clapp::parse(infer_race, raw_args{"test", "--racec=hello"});
    CLAPP_CHECK(racecar.has_value());
    CLAPP_CHECK(!racecar->get_flag("racetrack"));
    CLAPP_CHECK(one_string(*racecar, "racecar") == std::optional<std::string>{"hello"});

    const outcome racetrack = clapp::parse(infer_race, raw_args{"test", "--racet"});
    CLAPP_CHECK(racetrack.has_value());
    CLAPP_CHECK(racetrack->get_flag("racetrack"));
    CLAPP_CHECK(!one_string(*racetrack, "racecar").has_value());

    // Inference reaches an ALIAS too, not only the primary long spelling.
    const outcome autobahn = clapp::parse(infer_race, raw_args{"test", "--auto"});
    CLAPP_CHECK(autobahn.has_value());
    CLAPP_CHECK(autobahn->get_flag("racetrack"));
    CLAPP_CHECK(!one_string(*autobahn, "racecar").has_value());

    // A bare `--` is the escape, never a zero-length prefix of `--arg`.
    const outcome escape = clapp::parse(infer_one, raw_args{"test", "--"});
    CLAPP_CHECK(escape.has_value());
    CLAPP_CHECK(!escape->get_flag("arg"));

    const outcome one_letter = clapp::parse(infer_one, raw_args{"test", "--a"});
    CLAPP_CHECK(one_letter.has_value());
    CLAPP_CHECK(one_letter->get_flag("arg"));
}

CLAPP_TEST("opts.rs::infer_long_arg_pass_conflicts_exact_match") {
    // `--arg` is a prefix of `--arg2` and must still be the exact match.
    const outcome exact = clapp::parse(infer_exact, raw_args{"test", "--arg"});
    CLAPP_CHECK(exact.has_value());
    CLAPP_CHECK(exact->get_flag("arg"));

    const outcome longer = clapp::parse(infer_exact, raw_args{"test", "--arg2"});
    CLAPP_CHECK(longer.has_value());
    CLAPP_CHECK(longer->get_flag("arg2"));
}

CLAPP_TEST("opts.rs::infer_long_arg_pass_conflicting_aliases") {
    // `--ab` is a prefix of both `abc-123` and its own alias `abc-xyz` — the SAME
    // argument, so this is not an ambiguity.
    const outcome got = clapp::parse(infer_aliases, raw_args{"test", "--ab"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("abc-123"));
}

CLAPP_TEST("opts.rs::infer_long_arg_fail_conflicts") {
    // `--abc` is a prefix of two DIFFERENT arguments, so it matches neither.
    const outcome got = clapp::parse(infer_ambiguous, raw_args{"test", "--abc"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

// ---------------------------------------------------------------------------
// "did you mean"
//
// Both cases compare the whole block, because the tip line is the entire content and it
// is one line among five. They also test opposite failure modes of the same function:
// `did_you_mean` needs the RIGHT candidate out of a crowd, and `issue_1073` needs it not
// to take the FIRST plausible one — `--files-without-matches` is one edit from
// `--files-without-match` and one edit from `--files-with-matches`, and clap's issue
// title is literally "suboptimal flag suggestion".
// ---------------------------------------------------------------------------

CLAPP_TEST("opts.rs::did_you_mean") {
    const outcome got = clapp::parse(complex_app, raw_args{"clap-test", "--optio=foo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected argument '--optio' found\n"
                           "\n"
                           "  tip: a similar argument exists: '--option'\n"
                           "\n"
                           "Usage: clap-test --option <opt>... [positional] [positional2] "
                           "[positional3]...\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("opts.rs::issue_1073_suboptimal_flag_suggestion") {
    const outcome got =
            clapp::parse(ripgrep_616, raw_args{"ripgrep-616", "--files-without-matches"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected argument '--files-without-matches' found\n"
                           "\n"
                           "  tip: a similar argument exists: '--files-without-match'\n"
                           "\n"
                           "Usage: ripgrep-616 --files-without-match\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}
