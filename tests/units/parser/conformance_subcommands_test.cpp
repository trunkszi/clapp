#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    bool says(const outcome& got, std::string_view fragment) {
        return message_of(got).find(fragment) != std::string::npos;
    }

    /**
     * \brief Whole-block comparison against clap's expected string, printing both on a
     *        mismatch. Used wherever clap itself compares the whole block: a `says()` check
     *        would also pass on a message that mentions the right word in the wrong place,
     *        or that drops the `tip:` line entirely.
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

    consteval command_spec make_one_sub() {
        command_builder app("test");
        std::move(app)
                .subcommand(command_builder("some").arg(arg_builder("test")
                                                                .short_('t')
                                                                .long_("test")
                                                                .action(arg_action::set)
                                                                .help("testing testing")))
                .arg(arg_builder("other").long_("other"));
        return app.freeze();
    }
    constexpr command_spec one_sub = make_one_sub();

    consteval command_spec make_two_subs() {
        command_builder app("test");
        std::move(app)
                .subcommand(command_builder("some").arg(arg_builder("test")
                                                                .short_('t')
                                                                .long_("test")
                                                                .action(arg_action::set)
                                                                .help("testing testing")))
                .subcommand(command_builder("add").arg(arg_builder("roster").short_('r')))
                .arg(arg_builder("other").long_("other"));
        return app.freeze();
    }
    constexpr command_spec two_subs = make_two_subs();

    consteval command_spec make_single_alias() {
        command_builder app("myprog");
        std::move(app).subcommand(command_builder("test").alias("do-stuff"));
        return app.freeze();
    }
    constexpr command_spec single_alias = make_single_alias();

    consteval command_spec make_multiple_aliases() {
        command_builder app("myprog");
        std::move(app).subcommand(command_builder("test").aliases({"do-stuff", "test-stuff"}));
        return app.freeze();
    }
    constexpr command_spec multiple_aliases = make_multiple_aliases();

    consteval command_spec make_same_name_option() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("ui-path").long_("ui-path").value_name("PATH").required())
                .subcommand(command_builder("signer"));
        return app.freeze();
    }
    constexpr command_spec same_name_option = make_same_name_option();

    consteval command_spec make_double_dash_slop() {
        command_builder app("myprog");
        std::move(app)
                .no_binary_name()
                .arg(arg_builder("eff").short_('f').num_args(value_range::optional()))
                .arg(arg_builder("pea").short_('p').action(arg_action::set))
                .arg(arg_builder("slop")
                             .index(1)
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .last());
        return app.freeze();
    }
    constexpr command_spec double_dash_slop = make_double_dash_slop();

    consteval command_spec make_sub_and_positional() {
        command_builder app("myprog");
        std::move(app)
                .subcommand(command_builder("subcommand"))
                .arg(arg_builder("argument").index(1));
        return app.freeze();
    }
    constexpr command_spec sub_and_positional = make_sub_and_positional();

    consteval command_spec make_text_then_sub() {
        command_builder app("myprog");
        std::move(app).arg(arg_builder("some_text").index(1)).subcommand(command_builder("test"));
        return app.freeze();
    }
    constexpr command_spec text_then_sub = make_text_then_sub();

    consteval command_spec make_shared_name() {
        command_builder app("opt");
        std::move(app)
                .arg(arg_builder("global").long_("global").action(arg_action::set_true))
                .subcommand(command_builder("global"));
        return app.freeze();
    }
    constexpr command_spec shared_name = make_shared_name();

    consteval command_spec make_no_help_sub() {
        command_builder app("fake");
        std::move(app)
                .subcommand(command_builder("sub"))
                .disable_help_subcommand()
                .infer_subcommands();
        return app.freeze();
    }
    constexpr command_spec no_help_sub = make_no_help_sub();

    consteval command_spec make_busybox() {
        command_builder app("busybox");
        std::move(app)
                .multicall()
                .subcommand(command_builder("busybox")
                                    .subcommand(command_builder("true"))
                                    .subcommand(command_builder("false")))
                .subcommand(command_builder("true"))
                .subcommand(command_builder("false"));
        return app.freeze();
    }
    constexpr command_spec busybox = make_busybox();

    consteval command_spec make_hostname() {
        command_builder app("hostname");
        std::move(app)
                .multicall()
                .subcommand(command_builder("hostname"))
                .subcommand(command_builder("dnsdomainname"));
        return app.freeze();
    }
    constexpr command_spec hostname = make_hostname();

    consteval command_spec make_repl() {
        command_builder app("repl");
        std::move(app)
                .version("1.0.0")
                .propagate_version()
                .multicall()
                .subcommand(command_builder("foo"))
                .subcommand(command_builder("bar"));
        return app.freeze();
    }
    constexpr command_spec repl = make_repl();

    consteval command_spec make_dym() {
        command_builder app("dym");
        std::move(app).subcommand(command_builder("subcmd"));
        return app.freeze();
    }
    constexpr command_spec dym = make_dym();

    consteval command_spec make_dym_ambiguous() {
        command_builder app("dym");
        std::move(app).subcommand(command_builder("test")).subcommand(command_builder("temp"));
        return app.freeze();
    }
    constexpr command_spec dym_ambiguous = make_dym_ambiguous();

    // `--subcmarg` is not an argument of `dym` at all — it belongs to `subcmd`. The tip has
    // to search one level down.
    consteval command_spec make_dym_sub_arg() {
        command_builder app("dym");
        std::move(app).subcommand(command_builder("subcmd").arg(arg_builder("subcmdarg")
                                                                        .short_('s')
                                                                        .long_("subcmdarg")
                                                                        .value_name("subcmdarg")
                                                                        .help("tests")
                                                                        .action(arg_action::set)));
        return app.freeze();
    }
    constexpr command_spec dym_sub_arg = make_dym_sub_arg();

    consteval command_spec make_sub_after_escape() {
        command_builder app("cmd");
        std::move(app).subcommand(command_builder("subcmd"));
        return app.freeze();
    }
    constexpr command_spec sub_after_escape = make_sub_after_escape();

    static_assert(single_alias.find_subcommand("do-stuff")->get_name() == "test");
    static_assert(multiple_aliases.find_subcommand("test-stuff")->get_name() == "test");
    static_assert(busybox.is_multicall_set());
    static_assert(no_help_sub.is_disable_help_subcommand_set());
    static_assert(double_dash_slop.find_arg("slop")->is_last_set());

}  // namespace

// ---------------------------------------------------------------------------
// Entering a subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::subcommand") {
    const outcome got = clapp::parse(one_sub, raw_args{"myprog", "some", "--test", "testing"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"some"});
    const arg_matches* child = got->subcommand_matches("some");
    CLAPP_CHECK(child != nullptr);
    if (child != nullptr) {
        CLAPP_CHECK(child->contains_id("test"));
        CLAPP_CHECK(one_string(*child, "test") == std::optional<std::string>{"testing"});
    }
}

CLAPP_TEST("subcommands.rs::subcommand_none_given") {
    const outcome got = clapp::parse(one_sub, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->subcommand_name().has_value());
}

CLAPP_TEST("subcommands.rs::subcommand_multiple") {
    const outcome got = clapp::parse(two_subs, raw_args{"myprog", "some", "--test", "testing"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_matches("some") != nullptr);
    // Only the one that ran; a sibling must not acquire empty matches.
    CLAPP_CHECK(got->subcommand_matches("add") == nullptr);
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"some"});
}

// ---------------------------------------------------------------------------
// Aliases
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::single_alias") {
    const outcome got = clapp::parse(single_alias, raw_args{"myprog", "do-stuff"});
    CLAPP_CHECK(got.has_value());
    // The CANONICAL name, not the alias the user typed.
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("subcommands.rs::multiple_aliases") {
    const outcome got = clapp::parse(multiple_aliases, raw_args{"myprog", "test-stuff"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("subcommands.rs::alias_help") {
    // `help <alias>` resolves the alias and prints the aliased command's help.
    const outcome got = clapp::parse(single_alias, raw_args{"myprog", "help", "do-stuff"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
}

// ---------------------------------------------------------------------------
// An option in flight wins
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::issue_1031_args_with_same_name") {
    const outcome got = clapp::parse(same_name_option, raw_args{"prog", "--ui-path", "signer"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "ui-path") == std::optional<std::string>{"signer"});
    CLAPP_CHECK(!got->subcommand_name().has_value());
}

CLAPP_TEST("subcommands.rs::issue_1031_args_with_same_name_no_more_vals") {
    // Once `--ui-path` is satisfied the same word IS the subcommand.
    const outcome got =
            clapp::parse(same_name_option, raw_args{"prog", "--ui-path", "value", "signer"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "ui-path") == std::optional<std::string>{"value"});
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"signer"});
}

// ---------------------------------------------------------------------------
// `--` and `last(true)`
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::issue_1161_multiple_hyphen_hyphen") {
    // Everything after the FIRST `--` is data, including a second `--`.
    const outcome got = clapp::parse(double_dash_slop,
                                     raw_args{"-f",
                                              "-p=bob",
                                              "--",
                                              "sloppy",
                                              "slop",
                                              "-a",
                                              "--",
                                              "subprogram",
                                              "position",
                                              "args"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "slop") ==
                std::vector<std::string>{
                        "sloppy", "slop", "-a", "--", "subprogram", "position", "args"});
}

CLAPP_TEST("subcommands.rs::issue_1722_not_emit_error_when_arg_follows_similar_to_a_subcommand") {
    const outcome got = clapp::parse(sub_and_positional, raw_args{"myprog", "--", "subcommand"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "argument") == std::optional<std::string>{"subcommand"});
    CLAPP_CHECK(!got->subcommand_name().has_value());
}

CLAPP_TEST("subcommands.rs::subcommand_used_after_double_dash") {
    // The tip has to say what to DO — "remove the '--'" — because the error alone reads
    // as "no such thing" when the thing plainly exists. Whole block: an earlier version
    // of this case asserted only `says(got, "subcmd")`, which the message satisfies
    // three times over and would still satisfy with the tip line deleted.
    const outcome got = clapp::parse(sub_after_escape, raw_args{"cmd", "--", "subcmd"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected argument 'subcmd' found\n"
                           "\n"
                           "  tip: subcommand 'subcmd' exists; to use it, remove the '--' "
                           "before it\n"
                           "\n"
                           "Usage: cmd [COMMAND]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// A filled positional does not block a subcommand
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::subcommand_after_argument") {
    const outcome got = clapp::parse(text_then_sub, raw_args{"myprog", "teat", "test"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "some_text") == std::optional<std::string>{"teat"});
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("subcommands.rs::subcommand_after_argument_looks_like_help") {
    // `helt` must NOT be matched to the built-in `help` subcommand.
    const outcome got = clapp::parse(text_then_sub, raw_args{"myprog", "helt", "test"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "some_text") == std::optional<std::string>{"helt"});
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("subcommands.rs::issue_2494_subcommand_is_present") {
    // One name, two meanings, told apart by the leading dashes.
    const outcome both = clapp::parse(shared_name, raw_args{"opt", "--global", "global"});
    CLAPP_CHECK(both.has_value());
    CLAPP_CHECK(both->subcommand_name() == std::optional<std::string_view>{"global"});
    CLAPP_CHECK(both->get_flag("global"));

    const outcome flag_only = clapp::parse(shared_name, raw_args{"opt", "--global"});
    CLAPP_CHECK(flag_only.has_value());
    CLAPP_CHECK(!flag_only->subcommand_name().has_value());
    CLAPP_CHECK(flag_only->get_flag("global"));

    const outcome sub_only = clapp::parse(shared_name, raw_args{"opt", "global"});
    CLAPP_CHECK(sub_only.has_value());
    CLAPP_CHECK(sub_only->subcommand_name() == std::optional<std::string_view>{"global"});
    CLAPP_CHECK(!sub_only->get_flag("global"));
}

CLAPP_TEST("subcommands.rs::subcommand_not_recognized") {
    // `disable_help_subcommand` really removes it, even under `infer_subcommands`.
    const outcome got = clapp::parse(no_help_sub, raw_args{"fake", "help"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "help"));
}

// ---------------------------------------------------------------------------
// "did you mean"
//
// Four cases, and the fourth is the one that gives the other three their meaning: the
// same misspelled flag with a word that is not a subcommand must produce NO tip. An
// implementation that runs the cross-subcommand search unconditionally passes
// `subcmd_did_you_mean_output_arg` and fails only here.
//
// Whole blocks, because the interesting content is entirely in the tip line — its
// presence, its wording (singular vs plural), and the order of the names in it.
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::subcmd_did_you_mean_output") {
    const outcome got = clapp::parse(dym, raw_args{"dym", "subcm"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(same_block(got,
                           "error: unrecognized subcommand 'subcm'\n"
                           "\n"
                           "  tip: a similar subcommand exists: 'subcmd'\n"
                           "\n"
                           "Usage: dym [COMMAND]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("subcommands.rs::subcmd_did_you_mean_output_ambiguous") {
    // Two candidates ⇒ plural verb, plural noun, and both names listed in declaration
    // order. The singular/plural switch is a one-character difference that no
    // substring check would notice.
    const outcome got = clapp::parse(dym_ambiguous, raw_args{"dym", "te"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(same_block(got,
                           "error: unrecognized subcommand 'te'\n"
                           "\n"
                           "  tip: some similar subcommands exist: 'test', 'temp'\n"
                           "\n"
                           "Usage: dym [COMMAND]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("subcommands.rs::subcmd_did_you_mean_output_arg") {
    // The misspelled token is a FLAG, and the suggestion crosses into a subcommand:
    // the tip names both halves, `'subcmd --subcmdarg'`.
    const outcome got = clapp::parse(dym_sub_arg, raw_args{"dym", "--subcmarg", "subcmd"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected argument '--subcmarg' found\n"
                           "\n"
                           "  tip: 'subcmd --subcmdarg' exists\n"
                           "\n"
                           "Usage: dym [COMMAND]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("subcommands.rs::subcmd_did_you_mean_output_arg_false_positives") {
    // Identical command, identical misspelling, and `foo` in place of `subcmd`. No tip:
    // the search is gated on the LATER token naming a real subcommand.
    const outcome got = clapp::parse(dym_sub_arg, raw_args{"dym", "--subcmarg", "foo"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(same_block(got,
                           "error: unexpected argument '--subcmarg' found\n"
                           "\n"
                           "Usage: dym [COMMAND]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// multicall
// ---------------------------------------------------------------------------

CLAPP_TEST("subcommands.rs::busybox_like_multicall") {
    // argv[0] IS the multiplexer, so `true` is a second level.
    const outcome nested = clapp::parse(busybox, raw_args{"busybox", "true"});
    CLAPP_CHECK(nested.has_value());
    CLAPP_CHECK(nested->subcommand_name() == std::optional<std::string_view>{"busybox"});
    const arg_matches* inner = nested->subcommand_matches("busybox");
    CLAPP_CHECK(inner != nullptr);
    if (inner != nullptr)
        CLAPP_CHECK(inner->subcommand_name() == std::optional<std::string_view>{"true"});

    // argv[0] IS the applet.
    const outcome applet = clapp::parse(busybox, raw_args{"true"});
    CLAPP_CHECK(applet.has_value());
    CLAPP_CHECK(applet->subcommand_name() == std::optional<std::string_view>{"true"});

    const outcome unknown = clapp::parse(busybox, raw_args{"a.out"});
    CLAPP_CHECK(!unknown.has_value());
    CLAPP_CHECK(kind_of(unknown) == error_kind::invalid_subcommand);
}

CLAPP_TEST("subcommands.rs::hostname_like_multicall") {
    const outcome self_named = clapp::parse(hostname, raw_args{"hostname"});
    CLAPP_CHECK(self_named.has_value());
    CLAPP_CHECK(self_named->subcommand_name() == std::optional<std::string_view>{"hostname"});

    const outcome other = clapp::parse(hostname, raw_args{"dnsdomainname"});
    CLAPP_CHECK(other.has_value());
    CLAPP_CHECK(other->subcommand_name() == std::optional<std::string_view>{"dnsdomainname"});

    const outcome unknown = clapp::parse(hostname, raw_args{"a.out"});
    CLAPP_CHECK(!unknown.has_value());
    CLAPP_CHECK(kind_of(unknown) == error_kind::invalid_subcommand);

    // The applet has no subcommands of its own, so a SECOND name is a stray argument
    // rather than a nested command — the half that separates hostname from busybox.
    const outcome doubled = clapp::parse(hostname, raw_args{"hostname", "hostname"});
    CLAPP_CHECK(!doubled.has_value());
    CLAPP_CHECK(kind_of(doubled) == error_kind::unknown_argument);

    const outcome mixed = clapp::parse(hostname, raw_args{"hostname", "dnsdomainname"});
    CLAPP_CHECK(!mixed.has_value());
    CLAPP_CHECK(kind_of(mixed) == error_kind::unknown_argument);
}

CLAPP_TEST("subcommands.rs::bad_multicall_command_error") {
    const outcome unknown = clapp::parse(repl, raw_args{"world"});
    CLAPP_CHECK(!unknown.has_value());
    CLAPP_CHECK(kind_of(unknown) == error_kind::invalid_subcommand);
    CLAPP_CHECK(says(unknown, "unrecognized subcommand 'world'"));

    // Whatever multicall does to argv[0] must not disable `--help` or `--version`.
    const outcome help = clapp::parse(repl, raw_args{"foo", "--help"});
    CLAPP_CHECK(kind_of(help) == error_kind::display_help);

    const outcome version = clapp::parse(repl, raw_args{"foo", "--version"});
    CLAPP_CHECK(kind_of(version) == error_kind::display_version);
}

// ---------------------------------------------------------------------------
// The `--help` rendering half, unblocked by M5
//
// These six pin behavior beyond what the parse-side cases above already cover:
//
//   * A VISIBLE ALIAS IS ADVERTISED IN THE `Commands:` TABLE AND AN INVISIBLE ONE IS NOT,
//     even though BOTH resolve at parse time — which the `single_alias` case above
//     already proved. The two are therefore only distinguishable here, and asserting
//     them as a pair is the point: `test` carries one invisible alias and two visible
//     ones, and exactly the two visible ones appear.
//   * `subcommand_value_name` AND `subcommand_help_heading` ARE INDEPENDENT. One renames
//     the placeholder in the usage line, the other the heading over the table; clap
//     tests both on one command, so a renderer that wires them to the same string passes
//     neither half alone.
//   * A MULTICALL APPLET'S OWN HELP NAMES THE PATH IT WAS REACHED BY. `foo bar --help`
//     says `Usage: foo bar [value]`, not `Usage: repl foo bar` and not `Usage: bar`.
//     This is what clapp::help_style::usage_name exists for: ADR-0005 freezes the tree,
//     so the child spec cannot know its own path and the caller supplies it.
// ---------------------------------------------------------------------------

namespace {

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        See the fuller note in conformance_hidden_args_test.cpp.
     */
    std::string
    help_page(const command_spec& cmd, bool long_form, std::string_view usage_name = {}) {
        return clapp::render_help(
                       cmd,
                       clapp::help_style{.use_long   = long_form && clapp::long_help_exists(cmd),
                                         .usage_name = usage_name})
                .to_string();
    }

    bool same_page(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    consteval command_spec make_visible_alias_cmd() {
        command_builder app("clap-test");
        std::move(app).version("2.6").subcommand(command_builder("test")
                                                         .about("Some help")
                                                         .alias("invisible")
                                                         .visible_alias("dongle")
                                                         .visible_alias("done"));
        return app.freeze();
    }
    constexpr command_spec visible_alias_cmd = make_visible_alias_cmd();

    consteval command_spec make_invisible_alias_cmd() {
        command_builder app("clap-test");
        std::move(app).version("2.6").subcommand(
                command_builder("test").about("Some help").alias("invisible"));
        return app.freeze();
    }
    constexpr command_spec invisible_alias_cmd = make_invisible_alias_cmd();

    consteval command_spec make_placeholder_cmd() {
        command_builder app("myprog");
        std::move(app)
                .subcommand(command_builder("subcommand"))
                .subcommand_value_name("TEST_PLACEHOLDER")
                .subcommand_help_heading("TEST_HEADER");
        return app.freeze();
    }
    constexpr command_spec placeholder_cmd = make_placeholder_cmd();

    /**
     * clap's `multicall_render_help` fixture: `repl` above has no nested applet, and the
     * path `foo bar` is the whole point of these three cases.
     */
    consteval command_spec make_nested_multicall() {
        command_builder app("repl");
        std::move(app).version("1.0.0").propagate_version().multicall().subcommand(
                command_builder("foo").subcommand(
                        command_builder("bar").arg(arg_builder("value").index(1))));
        return app.freeze();
    }
    constexpr command_spec nested_multicall = make_nested_multicall();

    // Both alias kinds resolve at parse time; only visibility differs. Asserted here so the
    // screens below are read as a visibility test rather than a resolution test.
    static_assert(visible_alias_cmd.find_subcommand("dongle")->get_name() == "test");
    static_assert(visible_alias_cmd.find_subcommand("invisible")->get_name() == "test");
    static_assert(invisible_alias_cmd.find_subcommand("invisible")->get_name() == "test");

    static_assert(
            nested_multicall.find_subcommand("foo")->find_subcommand("bar")->has_arg("value"));

}  // namespace

CLAPP_TEST("subcommands.rs::visible_aliases_help_output") {
    CLAPP_CHECK(same_page(help_page(visible_alias_cmd, true),
                          "Usage: clap-test [COMMAND]\n"
                          "\n"
                          "Commands:\n"
                          "  test  Some help [aliases: dongle, done]\n"
                          "  help  Print this message or the help of the given subcommand(s)\n"
                          "\n"
                          "Options:\n"
                          "  -h, --help     Print help\n"
                          "  -V, --version  Print version\n"));
}

CLAPP_TEST("subcommands.rs::invisible_aliases_help_output") {
    // Same command minus the two visible aliases: the `[aliases: …]` tail disappears
    // entirely rather than rendering empty brackets.
    CLAPP_CHECK(same_page(help_page(invisible_alias_cmd, true),
                          "Usage: clap-test [COMMAND]\n"
                          "\n"
                          "Commands:\n"
                          "  test  Some help\n"
                          "  help  Print this message or the help of the given subcommand(s)\n"
                          "\n"
                          "Options:\n"
                          "  -h, --help     Print help\n"
                          "  -V, --version  Print version\n"));
}

CLAPP_TEST("subcommands.rs::subcommand_placeholder_test") {
    // clap asserts the usage line exactly and only `contains("TEST_HEADER:")` for the
    // heading. Both are asserted exactly here — the whole page is available, so there is
    // no reason to keep clap's weaker form, and a `contains` check would also pass if the
    // heading appeared in the wrong section.
    CLAPP_CHECK(same_page(help_page(placeholder_cmd, true),
                          "Usage: myprog [TEST_PLACEHOLDER]\n"
                          "\n"
                          "TEST_HEADER:\n"
                          "  subcommand  \n"
                          "  help        Print this message or the help of the given "
                          "subcommand(s)\n"
                          "\n"
                          "Options:\n"
                          "  -h, --help  Print help\n"));
}

CLAPP_TEST("subcommands.rs::multicall_render_help") {
    const command_spec* bar = nested_multicall.find_subcommand("foo")->find_subcommand("bar");
    CLAPP_CHECK(same_page(help_page(*bar, true, "foo bar"),
                          "Usage: foo bar [value]\n"
                          "\n"
                          "Arguments:\n"
                          "  [value]  \n"
                          "\n"
                          "Options:\n"
                          "  -h, --help     Print help\n"
                          "  -V, --version  Print version\n"));
}

CLAPP_TEST("subcommands.rs::multicall_help_flag") {
    // clap reaches the same page through `foo bar --help`. The kind and stream are the
    // contract; the page itself is the one asserted above.
    const outcome got = clapp::parse(nested_multicall, raw_args{"foo", "bar", "--help"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(!got.has_value() && got.error().exit_code() == 0);
    CLAPP_CHECK(!got.has_value() && !got.error().use_stderr());
}

CLAPP_TEST("subcommands.rs::multicall_help_subcommand") {
    // ...and through `help foo bar`, which routes through the injected help subcommand
    // instead of the flag. Same kind, same stream.
    const outcome got = clapp::parse(nested_multicall, raw_args{"help", "foo", "bar"});
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(!got.has_value() && !got.error().use_stderr());
}
