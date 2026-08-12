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
    using strings = std::vector<std::string>;

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
     *        mismatch. Used where clap itself compares the whole block rather than a
     *        fragment — a `find()` check would also pass on a message that says the right
     *        thing in the wrong section.
     */
    bool same_block(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    strings raw_of(const arg_matches& matches, std::string_view id) {
        strings out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures — the arity matrix
    // ---------------------------------------------------------------------------

    consteval command_spec make_opt_exact3_append() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .help("multiple options")
                                   .num_args(value_range::exactly(3))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec opt_exact3_append = make_opt_exact3_append();

    consteval command_spec make_opt_exact3() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .help("multiple options")
                                   .num_args(value_range::exactly(3)));
        return app.freeze();
    }
    constexpr command_spec opt_exact3 = make_opt_exact3();

    consteval command_spec make_opt_min3() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .help("multiple options")
                                   .num_args(value_range::at_least(3))
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec opt_min3 = make_opt_min3();

    consteval command_spec make_opt_one_to_three() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .help("multiple options")
                                   .num_args(value_range{1, 3})
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec opt_one_to_three = make_opt_one_to_three();

    consteval command_spec make_optional_port() {
        command_builder app("test");
        std::move(app).args_override_self().arg(
                arg_builder("port").short_('p').value_name("NUM").num_args(
                        value_range::optional()));
        return app.freeze();
    }
    constexpr command_spec optional_port = make_optional_port();

    consteval command_spec make_pos_unbounded() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .help("multiple positionals")
                                   .action(arg_action::set)
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec pos_unbounded = make_pos_unbounded();

    consteval command_spec make_pos_exact3() {
        command_builder app("myprog");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .help("multiple positionals")
                                   .num_args(value_range::exactly(3)));
        return app.freeze();
    }
    constexpr command_spec pos_exact3 = make_pos_exact3();

    consteval command_spec make_pos_min3() {
        command_builder app("myprog");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .help("multiple positionals")
                                   .num_args(value_range::at_least(3)));
        return app.freeze();
    }
    constexpr command_spec pos_min3 = make_pos_min3();

    consteval command_spec make_pos_one_to_three() {
        command_builder app("myprog");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .help("multiple positionals")
                                   .num_args(value_range{1, 3}));
        return app.freeze();
    }
    constexpr command_spec pos_one_to_three = make_pos_one_to_three();

    // Delimiters that must not raise the ceiling.
    consteval command_spec make_req_delim_long() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("option")
                             .long_("option")
                             .num_args(value_range::exactly(1))
                             .value_delimiter(','))
                .arg(arg_builder("args")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .index(1));
        return app.freeze();
    }
    constexpr command_spec req_delim_long = make_req_delim_long();

    consteval command_spec make_req_delim_short() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("option")
                             .short_('o')
                             .num_args(value_range::exactly(1))
                             .value_delimiter(','))
                .arg(arg_builder("args")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .index(1));
        return app.freeze();
    }
    constexpr command_spec req_delim_short = make_req_delim_short();

    consteval command_spec make_req_delim_complex() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("option")
                             .long_("option")
                             .short_('o')
                             .num_args(value_range::exactly(1))
                             .action(arg_action::append)
                             .value_delimiter(','))
                .arg(arg_builder("args").num_args(value_range::at_least(1)).index(1));
        return app.freeze();
    }
    constexpr command_spec req_delim_complex = make_req_delim_complex();

    // A multi-valued positional that must yield to a later one.
    consteval command_spec make_low_index() {
        command_builder app("lip");
        std::move(app)
                .arg(arg_builder("files")
                             .index(1)
                             .action(arg_action::set)
                             .required()
                             .num_args(value_range::at_least(1)))
                .arg(arg_builder("target").index(2).required());
        return app.freeze();
    }
    constexpr command_spec low_index = make_low_index();

    consteval command_spec make_low_index_in_subcmd() {
        command_builder app("lip");
        std::move(app).subcommand(command_builder("test")
                                          .arg(arg_builder("files")
                                                       .index(1)
                                                       .action(arg_action::set)
                                                       .required()
                                                       .num_args(value_range::at_least(1)))
                                          .arg(arg_builder("target").index(2).required()));
        return app.freeze();
    }
    constexpr command_spec low_index_in_subcmd = make_low_index_in_subcmd();

    consteval command_spec make_low_index_with_option() {
        command_builder app("lip");
        std::move(app)
                .arg(arg_builder("files")
                             .required()
                             .index(1)
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)))
                .arg(arg_builder("target").index(2).required())
                .arg(arg_builder("opt").long_("option").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec low_index_with_option = make_low_index_with_option();

    consteval command_spec make_low_index_with_flag() {
        command_builder app("lip");
        std::move(app)
                .arg(arg_builder("files")
                             .index(1)
                             .action(arg_action::set)
                             .required()
                             .num_args(value_range::at_least(1)))
                .arg(arg_builder("target").index(2).required())
                .arg(arg_builder("flg").long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec low_index_with_flag = make_low_index_with_flag();

    consteval command_spec make_low_index_extra_flags() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("yes").long_("yes").action(arg_action::set_true))
                .arg(arg_builder("one").long_("one").action(arg_action::set))
                .arg(arg_builder("two").long_("two").action(arg_action::set))
                .arg(arg_builder("input").index(1).num_args(value_range::at_least(1)).required())
                .arg(arg_builder("output").index(2).required());
        return app.freeze();
    }
    constexpr command_spec low_index_extra_flags = make_low_index_extra_flags();

    // Value terminators.
    consteval command_spec make_terminator_option() {
        command_builder app("lip");
        std::move(app)
                .arg(arg_builder("files")
                             .short_('f')
                             .value_name("FILE")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append)
                             .value_terminator(";"))
                .arg(arg_builder("target").index(1).required());
        return app.freeze();
    }
    constexpr command_spec terminator_option = make_terminator_option();

    consteval command_spec make_terminator_beats_hyphens() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("pos")
                             .index(1)
                             .num_args(value_range::at_least(1))
                             .allow_hyphen_values()
                             .value_terminator("--"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec terminator_beats_hyphens = make_terminator_beats_hyphens();

    consteval command_spec make_hyphen_values() {
        command_builder app("do");
        std::move(app)
                .arg(arg_builder("cmds")
                             .index(1)
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .allow_hyphen_values()
                             .value_terminator(";"))
                .arg(arg_builder("location").index(2));
        return app.freeze();
    }
    constexpr command_spec hyphen_values = make_hyphen_values();

    // The `issue_1480` family: a maximum must not eat the token after it.
    consteval command_spec make_issue_1480_with_positional() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("field").long_("field").num_args(value_range{0, 1}))
                .arg(arg_builder("positional").index(1).required());
        return app.freeze();
    }
    constexpr command_spec issue_1480_with_positional = make_issue_1480_with_positional();

    consteval command_spec make_issue_1480_bare() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("field").long_("field").num_args(value_range{0, 1}));
        return app.freeze();
    }
    constexpr command_spec issue_1480_bare = make_issue_1480_bare();

    consteval command_spec make_two_positionals() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("pos1").index(1).num_args(value_range::at_least(1)))
                .arg(arg_builder("pos2").index(2).num_args(value_range::at_least(1)).last());
        return app.freeze();
    }
    constexpr command_spec two_positionals = make_two_positionals();

    // --- Fixtures for the repetition, delimiter and terminator families -------------

    consteval command_spec make_opt_long_append() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .help("multiple options")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec opt_long_append = make_opt_long_append();

    consteval command_spec make_opt_short_append() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .help("multiple options")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec opt_short_append = make_opt_short_append();

    consteval command_spec make_opt_mixed_append() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .short_('o')
                                   .help("multiple options")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec opt_mixed_append = make_opt_mixed_append();

    // `num_args(3..)` plus a required positional: the positional must survive the greedy
    // option, which is the whole point of clap's two `option_short_min_more_*` cases.
    consteval command_spec make_pos_and_opt_min3_set() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("arg").required())
                .arg(arg_builder("option")
                             .short_('o')
                             .help("multiple options")
                             .num_args(value_range::at_least(3))
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec pos_and_opt_min3_set = make_pos_and_opt_min3_set();

    consteval command_spec make_pos_and_opt_min3_default_action() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("arg").required())
                .arg(arg_builder("option")
                             .short_('o')
                             .help("multiple options")
                             .num_args(value_range::at_least(3)));
        return app.freeze();
    }
    constexpr command_spec pos_and_opt_min3_default_action = make_pos_and_opt_min3_default_action();

    // The delimiter family. clap builds a fresh command per spelling; the spelling is what
    // differs, so each shape gets its own fixture rather than one shared command.
    consteval command_spec make_delim_long_comma() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .help("multiple options")
                                   .value_delimiter(','));
        return app.freeze();
    }
    constexpr command_spec delim_long_comma = make_delim_long_comma();

    consteval command_spec make_delim_short_comma() {
        command_builder app("multiple_values");
        std::move(app).arg(
                arg_builder("option").short_('o').help("multiple options").value_delimiter(','));
        return app.freeze();
    }
    constexpr command_spec delim_short_comma = make_delim_short_comma();

    consteval command_spec make_delim_positional_comma() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option").help("multiple options").value_delimiter(','));
        return app.freeze();
    }
    constexpr command_spec delim_positional_comma = make_delim_positional_comma();

    consteval command_spec make_delim_long_semicolon() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .help("multiple options")
                                   .value_delimiter(';'));
        return app.freeze();
    }
    constexpr command_spec delim_long_semicolon = make_delim_long_semicolon();

    consteval command_spec make_delim_positional_semicolon() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option").help("multiple options").value_delimiter(';'));
        return app.freeze();
    }
    constexpr command_spec delim_positional_semicolon = make_delim_positional_semicolon();

    // The negative controls: NO delimiter set, so the commas stay inside one value.
    consteval command_spec make_no_delim_long() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .help("multiple options")
                                   .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec no_delim_long = make_no_delim_long();

    consteval command_spec make_no_delim_positional() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("option").help("multiple options").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec no_delim_positional = make_no_delim_positional();

    // `value_terminator("--")`: the terminator LOOKS like the escape, and clap's three
    // `escape_like_value_terminator*` cases are about which reading wins.
    consteval command_spec make_escape_like_terminator() {
        command_builder app("do");
        std::move(app)
                .arg(arg_builder("cmd1")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .value_terminator("--"))
                .arg(arg_builder("cmd2")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec escape_like_terminator = make_escape_like_terminator();

    consteval command_spec make_escape_like_terminator_hyphens() {
        command_builder app("do");
        std::move(app)
                .arg(arg_builder("cmd1")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .allow_hyphen_values()
                             .value_terminator("--"))
                .arg(arg_builder("cmd2")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .allow_hyphen_values()
                             .value_terminator(";"));
        return app.freeze();
    }
    constexpr command_spec escape_like_terminator_hyphens = make_escape_like_terminator_hyphens();

    consteval command_spec make_escape_like_terminator_last() {
        command_builder app("do");
        std::move(app)
                .arg(arg_builder("cmd1")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .value_terminator("--"))
                .arg(arg_builder("cmd2")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .last());
        return app.freeze();
    }
    constexpr command_spec escape_like_terminator_last = make_escape_like_terminator_last();

    // The positional twin of `terminator_option`: `num_args(0..)` so an empty run is legal,
    // plus a second positional and a flag, which are the two other ways the run can end.
    consteval command_spec make_terminator_positional() {
        command_builder app("lip");
        std::move(app)
                .arg(arg_builder("files")
                             .value_terminator(";")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(0)))
                .arg(arg_builder("other"))
                .arg(arg_builder("stop").short_('X').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec terminator_positional = make_terminator_positional();

    consteval command_spec make_positional_value_names() {
        command_builder app("test");
        std::move(app).arg(arg_builder("pos").value_names({"who", "what", "why"}));
        return app.freeze();
    }
    constexpr command_spec positional_value_names = make_positional_value_names();

    consteval command_spec make_num_args_over_value_names() {
        command_builder app("test");
        std::move(app).arg(arg_builder("pos")
                                   .long_("pos")
                                   .num_args(value_range::exactly(4))
                                   .value_names({"who", "what", "why"}));
        return app.freeze();
    }
    constexpr command_spec num_args_over_value_names = make_num_args_over_value_names();

    consteval command_spec make_pair_per_occurrence_named() {
        command_builder app("test");
        std::move(app).arg(arg_builder("pos")
                                   .long_("pos")
                                   .num_args(value_range::exactly(2))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec pair_per_occurrence_named = make_pair_per_occurrence_named();

    consteval command_spec make_pair_per_occurrence_positional() {
        command_builder app("test");
        std::move(app).arg(
                arg_builder("pos").num_args(value_range::exactly(2)).action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec pair_per_occurrence_positional = make_pair_per_occurrence_positional();

    static_assert(opt_exact3.find_arg("option")->get_num_args() == value_range::exactly(3));
    static_assert(opt_min3.find_arg("option")->get_num_args() == value_range::at_least(3));
    static_assert(opt_one_to_three.find_arg("option")->get_num_args() == (value_range{1, 3}));
    static_assert(terminator_option.find_arg("files")->get_value_terminator() ==
                  std::optional<std::string_view>{";"});

    // The delimiter is a per-argument setting, and its ABSENCE is what `no_sep*` pins. Both
    // answers are asserted here so the negative control cannot silently become positive.
    static_assert(delim_long_comma.find_arg("option")->get_value_delimiter() ==
                  std::optional<char>{','});
    static_assert(delim_long_semicolon.find_arg("option")->get_value_delimiter() ==
                  std::optional<char>{';'});
    static_assert(!no_delim_long.find_arg("option")->get_value_delimiter().has_value());
    static_assert(!no_delim_positional.find_arg("option")->get_value_delimiter().has_value());

    // `value_names()` supplies the arity when `num_args()` is silent, and yields to it when
    // it is not — the two halves of clap's `value_names_building_num_vals_for_positional` /
    // `num_args_preferred_over_value_names` pair, asserted where the number is decided.
    static_assert(positional_value_names.find_arg("pos")->get_num_args() ==
                  value_range::exactly(3));
    static_assert(num_args_over_value_names.find_arg("pos")->get_num_args() ==
                  value_range::exactly(4));

}  // namespace

// ---------------------------------------------------------------------------
// Options: exactly n
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::option_exact_exact") {
    // `append`: three values PER OCCURRENCE, six in total. Without it the second `-o`
    // is a repeat of a `set` argument and the line is a conflict.
    const outcome got =
            clapp::parse(opt_exact3_append,
                         raw_args{"", "-o", "val1", "val2", "val3", "-o", "val4", "val5", "val6"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3", "val4", "val5", "val6"});
}

CLAPP_TEST("multiple_values.rs::option_exact_exact_not_mult") {
    const outcome got = clapp::parse(opt_exact3, raw_args{"", "-o", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::option_exact_less") {
    // The complaint is about ONE occurrence with one value, not about the argument
    // having two values in total. `1 was provided`, not `2`.
    const outcome got = clapp::parse(opt_exact3_append, raw_args{"", "-o", "val1", "-o", "val2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::wrong_number_of_values);
    CLAPP_CHECK(
            says(got, "3 values required for '-o <option> <option> <option>' but 1 was provided"));
}

CLAPP_TEST("multiple_values.rs::option_exact_more") {
    const outcome got =
            clapp::parse(opt_exact3_append,
                         raw_args{"", "-o", "val1", "-o", "val2", "-o", "val3", "-o", "val4"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::wrong_number_of_values);
}

// ---------------------------------------------------------------------------
// Options: at least n
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::option_min_exact") {
    const outcome got = clapp::parse(opt_min3, raw_args{"", "-o", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::option_min_less") {
    // `too_few_values`, a DIFFERENT kind from the exactly-n case above.
    const outcome got = clapp::parse(opt_min3, raw_args{"", "-o", "val1", "val2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::too_few_values);
}

// ---------------------------------------------------------------------------
// Options: at most n
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::option_max_exact") {
    const outcome got = clapp::parse(opt_one_to_three, raw_args{"", "-o", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::option_max_less") {
    const outcome got = clapp::parse(opt_one_to_three, raw_args{"", "-o", "val1", "val2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2"});
}

CLAPP_TEST("multiple_values.rs::option_max_zero") {
    // The minimum is 1, so a bare `-o` is a missing value.
    const outcome got = clapp::parse(opt_one_to_three, raw_args{"", "-o"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
    CLAPP_CHECK(says(got, "a value is required for '-o <option>...' but none was supplied"));
}

CLAPP_TEST("multiple_values.rs::option_max_zero_eq") {
    // `-o=` supplies the EMPTY value, which satisfies the minimum of one.
    const outcome got = clapp::parse(opt_one_to_three, raw_args{"", "-o="});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{""});
}

CLAPP_TEST("multiple_values.rs::option_max_more") {
    // The ceiling stops the collection; the extra token then has nowhere to go, so the
    // error is about the TOKEN and not about the count. clap's comment says as much.
    const outcome got =
            clapp::parse(opt_one_to_three, raw_args{"", "-o", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(says(got, "unexpected argument 'val4' found"));
}

CLAPP_TEST("multiple_values.rs::optional_value") {
    const outcome attached = clapp::parse(optional_port, raw_args{"test", "-p42"});
    CLAPP_CHECK(attached.has_value());
    CLAPP_CHECK(one_string(*attached, "port") == std::optional<std::string>{"42"});

    // Present AND valueless — two facts, and a single "does it have a value" test
    // conflates them.
    const outcome bare = clapp::parse(optional_port, raw_args{"test", "-p"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(bare->contains_id("port"));
    CLAPP_CHECK(!one_string(*bare, "port").has_value());

    const outcome twice = clapp::parse(optional_port, raw_args{"test", "-p", "24", "-p", "42"});
    CLAPP_CHECK(twice.has_value());
    CLAPP_CHECK(one_string(*twice, "port") == std::optional<std::string>{"42"});
}

// ---------------------------------------------------------------------------
// Positionals: the same four shapes
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::positional") {
    const outcome got = clapp::parse(pos_unbounded, raw_args{"myprog", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::positional_exact_exact") {
    const outcome got = clapp::parse(pos_exact3, raw_args{"myprog", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::positional_exact_less") {
    const outcome got = clapp::parse(pos_exact3, raw_args{"myprog", "val1", "val2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::wrong_number_of_values);
    CLAPP_CHECK(says(got, "3 values required for '[pos] [pos] [pos]' but 2 were provided"));
}

CLAPP_TEST("multiple_values.rs::positional_exact_more") {
    const outcome got =
            clapp::parse(pos_exact3, raw_args{"myprog", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::wrong_number_of_values);
}

CLAPP_TEST("multiple_values.rs::positional_min_exact") {
    const outcome got = clapp::parse(pos_min3, raw_args{"myprog", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::positional_min_less") {
    const outcome got = clapp::parse(pos_min3, raw_args{"myprog", "val1", "val2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::too_few_values);
    CLAPP_CHECK(says(got, "3 values required by '[pos] [pos] [pos]...'; only 2 were provided"));
}

CLAPP_TEST("multiple_values.rs::positional_min_more") {
    const outcome got = clapp::parse(pos_min3, raw_args{"myprog", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3", "val4"});
}

CLAPP_TEST("multiple_values.rs::positional_max_exact") {
    const outcome got = clapp::parse(pos_one_to_three, raw_args{"myprog", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::positional_max_less") {
    const outcome got = clapp::parse(pos_one_to_three, raw_args{"myprog", "val1", "val2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2"});
}

CLAPP_TEST("multiple_values.rs::positional_max_more") {
    // A positional has nowhere else to put the extra word, so here the ceiling DOES
    // produce a count complaint — the mirror of `option_max_more` above.
    const outcome got =
            clapp::parse(pos_one_to_three, raw_args{"myprog", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::too_many_values);
    CLAPP_CHECK(says(got, "unexpected value 'val4' for '[pos]...' found; no more were expected"));
}

// ---------------------------------------------------------------------------
// A delimiter does not raise the ceiling
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::req_delimiter_long") {
    const outcome got =
            clapp::parse(req_delim_long, raw_args{"", "--option", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1"});
    CLAPP_CHECK(raw_of(*got, "args") == strings{"val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::req_delimiter_long_with_equal") {
    const outcome got = clapp::parse(req_delim_long, raw_args{"", "--option=val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1"});
    CLAPP_CHECK(raw_of(*got, "args") == strings{"val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::req_delimiter_short_with_space") {
    const outcome got = clapp::parse(req_delim_short, raw_args{"", "-o", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1"});
    CLAPP_CHECK(raw_of(*got, "args") == strings{"val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::req_delimiter_short_with_no_space") {
    const outcome got = clapp::parse(req_delim_short, raw_args{"", "-oval1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1"});
    CLAPP_CHECK(raw_of(*got, "args") == strings{"val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::req_delimiter_short_with_equal") {
    const outcome got = clapp::parse(req_delim_short, raw_args{"", "-o=val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1"});
    CLAPP_CHECK(raw_of(*got, "args") == strings{"val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::req_delimiter_complex") {
    // Every spelling of the option interleaved with the positional, in one line. This is
    // the single densest regression fixture in clap's suite: 26 tokens, and the answer
    // is wrong if ANY of the five paths above disagrees with the others.
    const outcome got = clapp::parse(req_delim_complex,
                                     raw_args{"",
                                              "val1",
                                              "-oval2",
                                              "val3",
                                              "-o",
                                              "val4",
                                              "val5",
                                              "-o=val6",
                                              "val7",
                                              "--option=val8",
                                              "val9",
                                              "--option",
                                              "val10",
                                              "val11",
                                              "-oval12,val13",
                                              "val14",
                                              "-o",
                                              "val15,val16",
                                              "val17",
                                              "-o=val18,val19",
                                              "val20",
                                              "--option=val21,val22",
                                              "val23",
                                              "--option",
                                              "val24,val25",
                                              "val26"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val2",
                                                  "val4",
                                                  "val6",
                                                  "val8",
                                                  "val10",
                                                  "val12",
                                                  "val13",
                                                  "val15",
                                                  "val16",
                                                  "val18",
                                                  "val19",
                                                  "val21",
                                                  "val22",
                                                  "val24",
                                                  "val25"});
    CLAPP_CHECK(raw_of(*got, "args") == strings{"val1",
                                                "val3",
                                                "val5",
                                                "val7",
                                                "val9",
                                                "val11",
                                                "val14",
                                                "val17",
                                                "val20",
                                                "val23",
                                                "val26"});
}

// ---------------------------------------------------------------------------
// A multi-valued positional yields to a later one
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::low_index_positional") {
    const outcome got =
            clapp::parse(low_index, raw_args{"lip", "file1", "file2", "file3", "target"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == strings{"file1", "file2", "file3"});
    CLAPP_CHECK(one_string(*got, "target") == std::optional<std::string>{"target"});
}

CLAPP_TEST("multiple_values.rs::low_index_positional_in_subcmd") {
    const outcome got = clapp::parse(low_index_in_subcmd,
                                     raw_args{"lip", "test", "file1", "file2", "file3", "target"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* child = got->subcommand_matches("test");
    CLAPP_CHECK(child != nullptr);
    if (child != nullptr) {
        CLAPP_CHECK(raw_of(*child, "files") == strings{"file1", "file2", "file3"});
        CLAPP_CHECK(one_string(*child, "target") == std::optional<std::string>{"target"});
    }
}

CLAPP_TEST("multiple_values.rs::low_index_positional_with_option") {
    const outcome got =
            clapp::parse(low_index_with_option,
                         raw_args{"lip", "file1", "file2", "file3", "target", "--option", "test"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == strings{"file1", "file2", "file3"});
    CLAPP_CHECK(one_string(*got, "target") == std::optional<std::string>{"target"});
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"test"});
}

CLAPP_TEST("multiple_values.rs::low_index_positional_with_flag") {
    const outcome got = clapp::parse(
            low_index_with_flag, raw_args{"lip", "file1", "file2", "file3", "target", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == strings{"file1", "file2", "file3"});
    CLAPP_CHECK(one_string(*got, "target") == std::optional<std::string>{"target"});
    CLAPP_CHECK(got->get_flag("flg"));
}

CLAPP_TEST("multiple_values.rs::low_index_positional_with_extra_flags") {
    // Options interleaved BEFORE the positionals, and the last word still belongs to
    // `output`: the reservation survives everything in between.
    const outcome got = clapp::parse(
            low_index_extra_flags,
            raw_args{"test", "--one", "1", "--two", "2", "3", "4", "5", "6", "7", "8"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "input") == strings{"3", "4", "5", "6", "7"});
    CLAPP_CHECK(one_string(*got, "output") == std::optional<std::string>{"8"});
    CLAPP_CHECK(one_string(*got, "one") == std::optional<std::string>{"1"});
    CLAPP_CHECK(one_string(*got, "two") == std::optional<std::string>{"2"});
    CLAPP_CHECK(!got->get_flag("yes"));
}

// ---------------------------------------------------------------------------
// Value terminators
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::multiple_value_terminator_option") {
    // `;` closes the occurrence; the next word is the positional, not another value.
    const outcome got =
            clapp::parse(terminator_option, raw_args{"lip", "-f", "val1", "val2", ";", "target"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == strings{"val1", "val2"});
    CLAPP_CHECK(one_string(*got, "target") == std::optional<std::string>{"target"});
}

CLAPP_TEST("multiple_values.rs::multiple_vals_with_hyphen") {
    const outcome got = clapp::parse(
            hyphen_values,
            raw_args{"do", "find", "-type", "f", "-name", "special", ";", "/home/clap"});
    CLAPP_CHECK(got.has_value());
    // `-type` and `-name` are data because of allow_hyphen_values; `;` ends the run and
    // the word after it reaches the SECOND positional.
    CLAPP_CHECK(raw_of(*got, "cmds") == strings{"find", "-type", "f", "-name", "special"});
    CLAPP_CHECK(one_string(*got, "location") == std::optional<std::string>{"/home/clap"});
}

CLAPP_TEST("multiple_values.rs::value_terminator_has_higher_precedence_than_allow_hyphen_values") {
    // `--` is BOTH the escape and this argument's terminator. The terminator wins, so
    // `--flag` after it is a flag rather than more data.
    const outcome got =
            clapp::parse(terminator_beats_hyphens, raw_args{"test", "-b", "--", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"-b"});
    CLAPP_CHECK(got->get_flag("flag"));
}

// ---------------------------------------------------------------------------
// A maximum must not eat the token after it
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::issue_1480_max_values_consumes_extra_arg_1") {
    // The ceiling is one, so `file` lands on the positional rather than on `--field`.
    const outcome got =
            clapp::parse(issue_1480_with_positional, raw_args{"prog", "--field", "1", "file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "field") == strings{"1"});
    CLAPP_CHECK(one_string(*got, "positional") == std::optional<std::string>{"file"});
}

CLAPP_TEST("multiple_values.rs::issue_1480_max_values_consumes_extra_arg_2") {
    // No positional at all: the second value has nowhere to go and is reported as a
    // stray token, NOT as too many values for `--field`.
    const outcome got = clapp::parse(issue_1480_bare, raw_args{"prog", "--field", "1", "2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(says(got, "unexpected argument '2' found"));
}

CLAPP_TEST("multiple_values.rs::issue_1480_max_values_consumes_extra_arg_3") {
    // Three extra tokens, and the message still names the FIRST one.
    const outcome got = clapp::parse(issue_1480_bare, raw_args{"prog", "--field", "1", "2", "3"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(says(got, "unexpected argument '2' found"));
}

CLAPP_TEST("multiple_values.rs::multiple_positional_multiple_values") {
    // Two unbounded positionals, told apart only by `--`.
    const outcome got =
            clapp::parse(two_positionals, raw_args{"myprog", "val1", "val2", "--", "val3", "val4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos1") == strings{"val1", "val2"});
    CLAPP_CHECK(raw_of(*got, "pos2") == strings{"val3", "val4"});
}

// ---------------------------------------------------------------------------
// Repetition: `append` accumulates ACROSS occurrences
//
// The family above pins what one occurrence takes. These pin that a second occurrence
// adds to the same list rather than replacing it, and that the long and short spellings
// of one argument feed one list — an implementation that keys the accumulator on the
// spelling instead of the id passes `option_long` and `option_short` and fails
// `option_mixed`.
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::option_long") {
    const outcome got =
            clapp::parse(opt_long_append,
                         raw_args{"", "--option", "val1", "--option", "val2", "--option", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::option_short") {
    const outcome got =
            clapp::parse(opt_short_append, raw_args{"", "-o", "val1", "-o", "val2", "-o", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::option_mixed") {
    // One id, two spellings, one list — and the list is in argv order, not
    // spelling-grouped order.
    const outcome got = clapp::parse(
            opt_mixed_append,
            raw_args{"", "-o", "val1", "--option", "val2", "--option", "val3", "-o", "val4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3", "val4"});
}

CLAPP_TEST("multiple_values.rs::option_exact_exact_mult") {
    // `num_args(3)` + `append`: two occurrences of three, flattened to six.
    const outcome got =
            clapp::parse(opt_exact3_append,
                         raw_args{"", "-o", "val1", "val2", "val3", "-o", "val4", "val5", "val6"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3", "val4", "val5", "val6"});
}

CLAPP_TEST("multiple_values.rs::option_short_min_more_mult_occurs") {
    // The greedy `3..` option runs to the end of argv; the required positional was
    // already filled by the token BEFORE it. An implementation that resolves positionals
    // after the option has eaten everything reports `missing_required_argument` here.
    const outcome got = clapp::parse(pos_and_opt_min3_set,
                                     raw_args{"", "pos", "-o", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3", "val4"});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"pos"});
}

CLAPP_TEST("multiple_values.rs::option_short_min_more_single_occur") {
    // Same line without the explicit `action(set)` — the arity, not the action, is what
    // makes the option multi-valued.
    const outcome got = clapp::parse(pos_and_opt_min3_default_action,
                                     raw_args{"", "pos", "-o", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3", "val4"});
    CLAPP_CHECK(one_string(*got, "arg") == std::optional<std::string>{"pos"});
}

// ---------------------------------------------------------------------------
// `value_delimiter`: one token, several values
//
// The delimiter runs AFTER the token has been assigned to an argument, so it must fire
// on every spelling that can deliver a value: `--opt=v`, `--opt v`, `-o=v`, `-o v`,
// `-ov`, and a bare positional. Six cases for one splitter, because six different pieces
// of code hand it the string.
//
// The two `no_sep*` cases are the negative control and matter more than they look: a
// delimiter that defaults to `,` (which is what clap 2 did, and what an implementation
// that "helpfully" splits will do) passes all six positive cases and fails these two.
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::sep_long_equals") {
    const outcome got = clapp::parse(delim_long_comma, raw_args{"", "--option=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::sep_long_space") {
    const outcome got = clapp::parse(delim_long_comma, raw_args{"", "--option", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::sep_short_equals") {
    const outcome got = clapp::parse(delim_short_comma, raw_args{"", "-o=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::sep_short_space") {
    const outcome got = clapp::parse(delim_short_comma, raw_args{"", "-o", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::sep_short_no_space") {
    const outcome got = clapp::parse(delim_short_comma, raw_args{"", "-oval1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::sep_positional") {
    const outcome got = clapp::parse(delim_positional_comma, raw_args{"", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::different_sep") {
    // The delimiter is whatever was configured, and a comma in the value is then data.
    const outcome got = clapp::parse(delim_long_semicolon, raw_args{"", "--option=val1;val2;val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::different_sep_positional") {
    const outcome got = clapp::parse(delim_positional_semicolon, raw_args{"", "val1;val2;val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::no_sep") {
    // No delimiter configured: the commas are part of the ONE value.
    const outcome got = clapp::parse(no_delim_long, raw_args{"", "--option=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
    CLAPP_CHECK(raw_of(*got, "option") == strings{"val1,val2,val3"});
}

CLAPP_TEST("multiple_values.rs::no_sep_positional") {
    const outcome got = clapp::parse(no_delim_positional, raw_args{"", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
}

// ---------------------------------------------------------------------------
// A terminator spelled `--`
//
// `value_terminator("--")` collides head-on with the escape token. clap resolves it by
// letting the terminator win *while the argument is collecting* and letting the escape
// reading apply otherwise, so the same two dashes mean different things at different
// points in one line. Each case walks the same five lines — nothing, `--`, `-- after`,
// `before --`, `before -- after` — because the disagreement only shows up in the cells
// where one of the two arguments is empty.
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::escape_like_value_terminator") {
    const outcome empty = clapp::parse(escape_like_terminator, raw_args{"do"});
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(!empty->contains_id("cmd1"));
    CLAPP_CHECK(!empty->contains_id("cmd2"));

    // A bare `--` starts neither argument: it terminates an occurrence that never began.
    const outcome bare = clapp::parse(escape_like_terminator, raw_args{"do", "--"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(!bare->contains_id("cmd1"));
    CLAPP_CHECK(!bare->contains_id("cmd2"));

    const outcome after = clapp::parse(escape_like_terminator, raw_args{"do", "--", "after"});
    CLAPP_CHECK(after.has_value());
    CLAPP_CHECK(!after->contains_id("cmd1"));
    CLAPP_CHECK(raw_of(*after, "cmd2") == strings{"after"});

    const outcome before = clapp::parse(escape_like_terminator, raw_args{"do", "before", "--"});
    CLAPP_CHECK(before.has_value());
    CLAPP_CHECK(raw_of(*before, "cmd1") == strings{"before"});
    CLAPP_CHECK(!before->contains_id("cmd2"));

    const outcome both =
            clapp::parse(escape_like_terminator, raw_args{"do", "before", "--", "after"});
    CLAPP_CHECK(both.has_value());
    CLAPP_CHECK(raw_of(*both, "cmd1") == strings{"before"});
    CLAPP_CHECK(raw_of(*both, "cmd2") == strings{"after"});
}

CLAPP_TEST("multiple_values.rs::escape_like_value_terminator_and_allow_hyphen_values") {
    const outcome empty = clapp::parse(escape_like_terminator_hyphens, raw_args{"do"});
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(!empty->contains_id("cmd1"));
    CLAPP_CHECK(!empty->contains_id("cmd2"));

    const outcome bare = clapp::parse(escape_like_terminator_hyphens, raw_args{"do", "--"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(!bare->contains_id("cmd1"));
    CLAPP_CHECK(!bare->contains_id("cmd2"));

    const outcome after =
            clapp::parse(escape_like_terminator_hyphens, raw_args{"do", "--", "after"});
    CLAPP_CHECK(after.has_value());
    CLAPP_CHECK(!after->contains_id("cmd1"));
    CLAPP_CHECK(raw_of(*after, "cmd2") == strings{"after"});

    const outcome before =
            clapp::parse(escape_like_terminator_hyphens, raw_args{"do", "before", "--"});
    CLAPP_CHECK(before.has_value());
    CLAPP_CHECK(raw_of(*before, "cmd1") == strings{"before"});
    CLAPP_CHECK(!before->contains_id("cmd2"));

    const outcome both =
            clapp::parse(escape_like_terminator_hyphens, raw_args{"do", "before", "--", "after"});
    CLAPP_CHECK(both.has_value());
    CLAPP_CHECK(raw_of(*both, "cmd1") == strings{"before"});
    CLAPP_CHECK(raw_of(*both, "cmd2") == strings{"after"});

    // clap's `find`-shaped line: `allow_hyphen_values` keeps `-type` and `-name` as data
    // inside the first argument, and the terminator still closes it.
    const outcome find_shape = clapp::parse(
            escape_like_terminator_hyphens,
            raw_args{"do", "find", "-type", "f", "-name", "special", "--", "/home/clap", "foo"});
    CLAPP_CHECK(find_shape.has_value());
    CLAPP_CHECK(raw_of(*find_shape, "cmd1") == strings{"find", "-type", "f", "-name", "special"});
    CLAPP_CHECK(raw_of(*find_shape, "cmd2") == strings{"/home/clap", "foo"});
}

CLAPP_TEST("multiple_values.rs::escape_like_value_terminator_and_last") {
    // Same five lines with `last(true)` on the second argument instead of a terminator on
    // it — the reading of `--` changes owner, and the answers must not.
    const outcome empty = clapp::parse(escape_like_terminator_last, raw_args{"do"});
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(!empty->contains_id("cmd1"));
    CLAPP_CHECK(!empty->contains_id("cmd2"));

    const outcome bare = clapp::parse(escape_like_terminator_last, raw_args{"do", "--"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(!bare->contains_id("cmd1"));
    CLAPP_CHECK(!bare->contains_id("cmd2"));

    const outcome after = clapp::parse(escape_like_terminator_last, raw_args{"do", "--", "after"});
    CLAPP_CHECK(after.has_value());
    CLAPP_CHECK(!after->contains_id("cmd1"));
    CLAPP_CHECK(raw_of(*after, "cmd2") == strings{"after"});

    const outcome before =
            clapp::parse(escape_like_terminator_last, raw_args{"do", "before", "--"});
    CLAPP_CHECK(before.has_value());
    CLAPP_CHECK(raw_of(*before, "cmd1") == strings{"before"});
    CLAPP_CHECK(!before->contains_id("cmd2"));

    const outcome both =
            clapp::parse(escape_like_terminator_last, raw_args{"do", "before", "--", "after"});
    CLAPP_CHECK(both.has_value());
    CLAPP_CHECK(raw_of(*both, "cmd1") == strings{"before"});
    CLAPP_CHECK(raw_of(*both, "cmd2") == strings{"after"});
}

CLAPP_TEST("multiple_values.rs::multiple_value_terminator_positional") {
    // The same terminator on a POSITIONAL, which has three ways to end instead of one:
    // the terminator token, a flag, or running out of argv. All three, plus the empty
    // run that `num_args(0..)` makes legal.
    const outcome terminated_empty =
            clapp::parse(terminator_positional, raw_args{"lip", ";", "otherval"});
    CLAPP_CHECK(terminated_empty.has_value());
    CLAPP_CHECK(!terminated_empty->contains_id("files"));
    CLAPP_CHECK(terminated_empty->contains_id("other"));
    CLAPP_CHECK(!terminated_empty->get_flag("stop"));
    CLAPP_CHECK(one_string(*terminated_empty, "other") == std::optional<std::string>{"otherval"});

    const outcome terminated =
            clapp::parse(terminator_positional, raw_args{"lip", "val1", "val2", ";", "otherval"});
    CLAPP_CHECK(terminated.has_value());
    CLAPP_CHECK(raw_of(*terminated, "files") == strings{"val1", "val2"});
    CLAPP_CHECK(one_string(*terminated, "other") == std::optional<std::string>{"otherval"});
    CLAPP_CHECK(!terminated->get_flag("stop"));

    const outcome nothing = clapp::parse(terminator_positional, raw_args{"lip"});
    CLAPP_CHECK(nothing.has_value());
    CLAPP_CHECK(!nothing->contains_id("files"));
    CLAPP_CHECK(!nothing->contains_id("other"));
    CLAPP_CHECK(!nothing->get_flag("stop"));

    const outcome unterminated =
            clapp::parse(terminator_positional, raw_args{"lip", "val1", "val2"});
    CLAPP_CHECK(unterminated.has_value());
    CLAPP_CHECK(raw_of(*unterminated, "files") == strings{"val1", "val2"});
    CLAPP_CHECK(!unterminated->contains_id("other"));

    // A flag closes the run — and then the word after it reopens it, landing in `files`
    // rather than in `other`. That is the surprising half.
    const outcome by_flag = clapp::parse(terminator_positional, raw_args{"lip", "-X", "otherval"});
    CLAPP_CHECK(by_flag.has_value());
    CLAPP_CHECK(raw_of(*by_flag, "files") == strings{"otherval"});
    CLAPP_CHECK(!by_flag->contains_id("other"));
    CLAPP_CHECK(by_flag->get_flag("stop"));

    // ...but only once. A second run for a `set` positional is `argument_conflict`, and
    // the whole block is compared because the message names the placeholder, not the id.
    const outcome twice =
            clapp::parse(terminator_positional, raw_args{"lip", "val1", "val2", "-X", "otherval"});
    CLAPP_CHECK(!twice.has_value());
    CLAPP_CHECK(kind_of(twice) == error_kind::argument_conflict);
    CLAPP_CHECK(same_block(message_of(twice),
                           "error: the argument '[files]...' cannot be used multiple times\n"
                           "\n"
                           "Usage: lip [OPTIONS] [files]... [other]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// `value_names()` as an arity, and arity per occurrence
// ---------------------------------------------------------------------------

CLAPP_TEST("multiple_values.rs::value_names_building_num_vals_for_positional") {
    // Three names ⇒ exactly three values, with no `num_args()` anywhere.
    const outcome got =
            clapp::parse(positional_value_names, raw_args{"myprog", "val1", "val2", "val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3"});
}

CLAPP_TEST("multiple_values.rs::num_args_preferred_over_value_names") {
    // Four requested, three named: the explicit number wins and the fourth value is
    // accepted with no placeholder to call its own.
    const outcome got = clapp::parse(num_args_over_value_names,
                                     raw_args{"myprog", "--pos", "val1", "val2", "val3", "val4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2", "val3", "val4"});
}

CLAPP_TEST("multiple_values.rs::values_per_occurrence_named") {
    const outcome one =
            clapp::parse(pair_per_occurrence_named, raw_args{"myprog", "--pos", "val1", "val2"});
    CLAPP_CHECK(one.has_value());
    CLAPP_CHECK(raw_of(*one, "pos") == strings{"val1", "val2"});

    // Twice two, not "four is too many": `num_args` is spent per occurrence.
    const outcome two =
            clapp::parse(pair_per_occurrence_named,
                         raw_args{"myprog", "--pos", "val1", "val2", "--pos", "val3", "val4"});
    CLAPP_CHECK(two.has_value());
    CLAPP_CHECK(raw_of(*two, "pos") == strings{"val1", "val2", "val3", "val4"});
}

CLAPP_TEST("multiple_values.rs::values_per_occurrence_positional") {
    const outcome got =
            clapp::parse(pair_per_occurrence_positional, raw_args{"myprog", "val1", "val2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "pos") == strings{"val1", "val2"});
}

CLAPP_TEST("multiple_values.rs::issue_2229") {
    // A positional with a fixed arity and twice as many words. The whole block is
    // compared: the message names the placeholder run `[pos] [pos] [pos]` (one entry per
    // required value, not one per argument), and the usage line repeats it.
    const outcome got = clapp::parse(
            pos_exact3, raw_args{"myprog", "val1", "val2", "val3", "val4", "val5", "val6"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::wrong_number_of_values);
    CLAPP_CHECK(same_block(message_of(got),
                           "error: 3 values required for '[pos] [pos] [pos]' but 6 were "
                           "provided\n"
                           "\n"
                           "Usage: myprog [pos] [pos] [pos]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// The `render_help()` half, unblocked by M5
//
// Arity is not only a parsing rule — it decides how the argument is *spelled* on the
// help screen, and the two must agree or the screen documents a command that does not
// exist. What these pin:
//
//   * AN OPTIONAL VALUE IS BRACKETED: `num_args(0..=1)` renders `-p [<NUM>]`, not
//     `-p <NUM>`. The brackets are the only thing on the screen that tells a user
//     `-p` alone is legal, which the parse half above proves it is.
//   * `value_names()` SUPPLIES BOTH THE ARITY AND THE PLACEHOLDER RUN. Three names make
//     the argument take exactly three values and render as `--pos <who> <what> <why>`.
//     An implementation that derives the count but prints one generic placeholder passes
//     every parse case in this file and still documents the wrong command line.
// ---------------------------------------------------------------------------

namespace {

    /**
     * \brief The page `--help` prints, with clap's `use_long` collapse applied.
     *        See the fuller note in conformance_hidden_args_test.cpp.
     */
    std::string help_page(const command_spec& cmd, bool long_form) {
        return clapp::render_help(
                       cmd,
                       clapp::help_style{.use_long = long_form && clapp::long_help_exists(cmd)})
                .to_string();
    }

    bool same_page(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    consteval command_spec make_named_values() {
        command_builder app("test");
        std::move(app).arg(arg_builder("pos").long_("pos").value_names({"who", "what", "why"}));
        return app.freeze();
    }
    constexpr command_spec named_values = make_named_values();

    // The arity the three names imply, asserted where it is derived — so a screen showing
    // the wrong number of placeholders is a renderer bug rather than a builder one.
    static_assert(named_values.find_arg("pos")->get_num_args() == value_range::exactly(3));

}  // namespace

CLAPP_TEST("multiple_values.rs::optional_value (render_help)") {
    // `-p [<NUM>]` — the brackets are the arity, and the description column is empty but
    // still padded, which is why the row ends in two spaces.
    CLAPP_CHECK(same_page(help_page(optional_port, true),
                          "Usage: test [OPTIONS]\n"
                          "\n"
                          "Options:\n"
                          "  -p [<NUM>]  \n"
                          "  -h, --help  Print help\n"));
}

CLAPP_TEST("multiple_values.rs::value_names_building_num_vals (render_help)") {
    CLAPP_CHECK(same_page(help_page(named_values, true),
                          "Usage: test [OPTIONS]\n"
                          "\n"
                          "Options:\n"
                          "      --pos <who> <what> <why>  \n"
                          "  -h, --help                    Print help\n"));
}
