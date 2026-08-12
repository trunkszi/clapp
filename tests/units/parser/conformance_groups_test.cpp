#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/util/id.hpp>

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
    using clapp::arg_id;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::group_builder;
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

    // A group's values are member ids, so this is clap's `get_one::<Id>`.
    std::vector<std::string> members_of(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_required_group() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true))
                .group(group_builder("req").args({"flag", "color"}).required());
        return app.freeze();
    }
    constexpr command_spec required_group = make_required_group();

    consteval command_spec make_grp_two_opts() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("color").short_('c').long_("color").value_name("color").num_args(
                        value_range::optional()))
                .arg(arg_builder("hostname").short_('n').long_("hostname").value_name("name"))
                .group(group_builder("grp").args({"hostname", "color"}));
        return app.freeze();
    }
    constexpr command_spec grp_two_opts = make_grp_two_opts();

    consteval command_spec make_grp_three() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").value_name("color").num_args(
                        value_range::optional()))
                .arg(arg_builder("hostname").short_('n').long_("hostname").value_name("name"))
                .group(group_builder("grp").args({"hostname", "color", "flag"}));
        return app.freeze();
    }
    constexpr command_spec grp_three = make_grp_three();

    consteval command_spec make_grp_three_required() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true))
                .arg(arg_builder("hostname").short_('n').long_("hostname").value_name("name"))
                .group(group_builder("grp").required().args({"hostname", "color", "flag"}));
        return app.freeze();
    }
    constexpr command_spec grp_three_required = make_grp_three_required();

    consteval command_spec make_grp_multi_value() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").value_name("color").num_args(
                        value_range::at_least(1)))
                .arg(arg_builder("hostname").short_('n').long_("hostname").value_name("name"))
                .group(group_builder("grp").args({"hostname", "color", "flag"}));
        return app.freeze();
    }
    constexpr command_spec grp_multi_value = make_grp_multi_value();

    consteval command_spec make_empty_required_group() {
        command_builder app("empty_group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .group(group_builder("vers").required());
        return app.freeze();
    }
    constexpr command_spec empty_required_group = make_empty_required_group();

    consteval command_spec make_base_or_delete() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("base").index(1).help("Base commit"))
                .arg(arg_builder("delete")
                             .short_('d')
                             .long_("delete")
                             .action(arg_action::set_true)
                             .help("Remove the base commit information"))
                .group(group_builder("base_or_delete").args({"base", "delete"}).required());
        return app.freeze();
    }
    constexpr command_spec base_or_delete = make_base_or_delete();

    consteval command_spec make_base_or_delete_conflicting() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("base").index(1).help("Base commit").conflicts_with("delete"))
                .arg(arg_builder("delete")
                             .short_('d')
                             .long_("delete")
                             .action(arg_action::set_true)
                             .help("Remove the base commit information"))
                .group(group_builder("base_or_delete").args({"base", "delete"}).required());
        return app.freeze();
    }
    constexpr command_spec base_or_delete_conflicting = make_base_or_delete_conflicting();

    consteval command_spec make_all_or_delete() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("all")
                             .short_('a')
                             .long_("all")
                             .action(arg_action::set_true)
                             .conflicts_with("delete"))
                .arg(arg_builder("delete").short_('d').long_("delete").action(arg_action::set_true))
                .group(group_builder("all_or_delete").args({"all", "delete"}).required());
        return app.freeze();
    }
    constexpr command_spec all_or_delete = make_all_or_delete();

    consteval command_spec make_conflict_with_group() {
        command_builder app("prog");
        std::move(app)
                .group(group_builder("group").multiple())
                .arg(arg_builder("a").long_("a").action(arg_action::set_true).group("group"))
                .arg(arg_builder("b").long_("b").action(arg_action::set_true).group("group"))
                .arg(arg_builder("conflict")
                             .long_("conflict")
                             .action(arg_action::set_true)
                             .conflicts_with("group"));
        return app.freeze();
    }
    constexpr command_spec conflict_with_group = make_conflict_with_group();

    consteval command_spec make_multiple_required_group() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true))
                .group(group_builder("req").args({"flag", "color"}).required().multiple());
        return app.freeze();
    }
    constexpr command_spec multiple_required_group = make_multiple_required_group();

    consteval command_spec make_single_group() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true))
                .group(group_builder("req").args({"flag", "color"}));
        return app.freeze();
    }
    constexpr command_spec single_group = make_single_group();

    consteval command_spec make_group_overrides_required() {
        command_builder app("group");
        std::move(app)
                .arg(arg_builder("foo").long_("foo").value_name("FOO").required())
                .arg(arg_builder("bar").long_("bar").value_name("BAR").required())
                .group(group_builder("group").args({"foo", "bar"}).required());
        return app.freeze();
    }
    constexpr command_spec group_overrides_required = make_group_overrides_required();

    consteval command_spec make_group_acts_like_arg() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("debug").long_("debug").group("mode").action(arg_action::set_true))
                .arg(arg_builder("verbose").long_("verbose").group("mode").action(
                        arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec group_acts_like_arg = make_group_acts_like_arg();

    consteval command_spec make_overlapping_groups() {
        command_builder app("prog");
        std::move(app)
                .group(group_builder("all").multiple())
                .arg(arg_builder("major")
                             .long_("major")
                             .action(arg_action::set_true)
                             .group("vers")
                             .group("all"))
                .arg(arg_builder("minor")
                             .long_("minor")
                             .action(arg_action::set_true)
                             .group("vers")
                             .group("all"))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true).group("all"));
        return app.freeze();
    }
    constexpr command_spec overlapping_groups = make_overlapping_groups();

    consteval command_spec make_requires_overlapping_group() {
        command_builder app("prog");
        std::move(app)
                .group(group_builder("all").multiple())
                .group(group_builder("input").required())
                .arg(arg_builder("in")
                             .long_("in")
                             .action(arg_action::set_true)
                             .group("input")
                             .group("all"))
                .arg(arg_builder("spec")
                             .long_("spec")
                             .action(arg_action::set_true)
                             .group("input")
                             .group("all"))
                .arg(arg_builder("config")
                             .long_("config")
                             .action(arg_action::set_true)
                             .requires_("input")
                             .group("all"));
        return app.freeze();
    }
    constexpr command_spec requires_overlapping_group = make_requires_overlapping_group();

    static_assert(required_group.find_group("req")->is_required_set());
    static_assert(!required_group.find_group("req")->is_multiple());
    static_assert(multiple_required_group.find_group("req")->is_multiple());
    static_assert(empty_required_group.find_group("vers")->empty());

}  // namespace

// ---------------------------------------------------------------------------
// A group has a value
// ---------------------------------------------------------------------------

CLAPP_TEST("groups.rs::group_single_value") {
    const outcome got = clapp::parse(grp_two_opts, raw_args{"", "-c", "blue"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("grp"));
    // The value is the MEMBER'S ID, not the member's value.
    CLAPP_CHECK(members_of(*got, "grp") == std::vector<std::string>{"color"});
    CLAPP_CHECK(got->get_one<arg_id>("grp").has_value());
}

CLAPP_TEST("groups.rs::group_multi_value_single_arg") {
    // Three values on one member is still ONE entry in the group.
    const outcome got = clapp::parse(grp_multi_value, raw_args{"", "-c", "blue", "red", "green"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("grp"));
    CLAPP_CHECK(members_of(*got, "grp") == std::vector<std::string>{"color"});
}

CLAPP_TEST("groups.rs::group_empty") {
    const outcome got = clapp::parse(grp_three, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("grp"));
    CLAPP_CHECK(members_of(*got, "grp").empty());
}

CLAPP_TEST("groups.rs::group_acts_like_arg") {
    const outcome got = clapp::parse(group_acts_like_arg, raw_args{"prog", "--debug"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("mode"));
    CLAPP_CHECK(members_of(*got, "mode") == std::vector<std::string>{"debug"});
}

// ---------------------------------------------------------------------------
// required
// ---------------------------------------------------------------------------

CLAPP_TEST("groups.rs::required_group_missing_arg") {
    const outcome got = clapp::parse(required_group, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("groups.rs::group_required_flags_empty") {
    const outcome got = clapp::parse(grp_three_required, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("groups.rs::empty_group") {
    // A required group with NO members can never be satisfied, and saying nothing about
    // it is the failure mode.
    const outcome got = clapp::parse(empty_required_group, raw_args{"empty_prog"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("groups.rs::group_overrides_required") {
    // `--foo` and `--bar` are each required(true); the group they share supersedes both.
    const outcome got = clapp::parse(group_overrides_required, raw_args{"group", "--foo", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("foo"));
    CLAPP_CHECK(!got->contains_id("bar"));
}

CLAPP_TEST("groups.rs::required_group_multiple_args") {
    const outcome got = clapp::parse(multiple_required_group, raw_args{"group", "-f", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(got->get_flag("color"));
    // Both members, in the order they were typed.
    CLAPP_CHECK(members_of(*got, "req") == std::vector<std::string>{"flag", "color"});
}

// ---------------------------------------------------------------------------
// multiple(false) is a conflict between the members
// ---------------------------------------------------------------------------

CLAPP_TEST("groups.rs::group_multiple_args_error") {
    const outcome got = clapp::parse(single_group, raw_args{"group", "-f", "-c"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

// ---------------------------------------------------------------------------
// The message names the members
// ---------------------------------------------------------------------------

CLAPP_TEST("groups.rs::req_group_usage_string") {
    const outcome got = clapp::parse(base_or_delete, raw_args{"clap-test"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the following required arguments were not provided:"));
    CLAPP_CHECK(says(got, "<base|--delete>"));
    CLAPP_CHECK(says(got, "Usage: clap-test <base|--delete>"));
}

CLAPP_TEST("groups.rs::req_group_with_conflict_usage_string") {
    const outcome got =
            clapp::parse(base_or_delete_conflicting, raw_args{"clap-test", "--delete", "base"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '--delete' cannot be used with '[base]'"));
    CLAPP_CHECK(says(got, "Usage: clap-test <base|--delete>"));
}

CLAPP_TEST("groups.rs::req_group_with_conflict_usage_string_only_options") {
    const outcome got = clapp::parse(all_or_delete, raw_args{"clap-test", "--delete", "--all"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '--delete' cannot be used with '--all'"));
    CLAPP_CHECK(says(got, "Usage: clap-test <--all|--delete>"));
}

CLAPP_TEST("groups.rs::conflict_with_group") {
    // The group id is unrolled into its members: `--a` and `--b`, never `group`.
    const outcome got = clapp::parse(conflict_with_group, raw_args{"prog", "--a", "--conflict"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '--conflict' cannot be used with:"));
    CLAPP_CHECK(says(got, "--a"));
    CLAPP_CHECK(says(got, "--b"));
    CLAPP_CHECK(!says(got, "'group'"));
}

CLAPP_TEST("groups.rs::conflict_with_overlapping_group_in_error") {
    // `--major` and `--minor` share two groups; only the single-valued one conflicts,
    // and the message must name the other ARGUMENT rather than either group.
    const outcome got = clapp::parse(overlapping_groups, raw_args{"prog", "--major", "--minor"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '--major' cannot be used with '--minor'"));
}

CLAPP_TEST("groups.rs::requires_group_with_overlapping_group_in_error") {
    // `--config` requires the group `input`; the requirement is reported as the group's
    // members, and the usage line carries both.
    const outcome got = clapp::parse(requires_overlapping_group, raw_args{"prog", "--config"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(says(got, "<--in|--spec>"));
    CLAPP_CHECK(says(got, "Usage: prog --config <--in|--spec>"));
}

// ---------------------------------------------------------------------------
// The `--help` rendering half, unblocked by M5
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

    consteval command_spec make_group_val_name() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("a").index(1).value_name("A"))
                .group(group_builder("group").args({"a"}).required());
        return app.freeze();
    }
    constexpr command_spec group_val_name = make_group_val_name();

}  // namespace

CLAPP_TEST("groups.rs::group_usage_use_val_name") {
    // The two halves disagree on purpose, and that is the whole case: a REQUIRED group
    // makes the usage line spell its single member as `<A>` — the group is required, so
    // something must be supplied — while the `Arguments:` row still shows `[A]`, because
    // the ARGUMENT itself was never marked required. A renderer that derives both from
    // one flag makes them agree and gets one of them wrong. Note also that the usage line
    // uses the argument's `value_name`, not the group's id.
    CLAPP_CHECK(same_page(help_page(group_val_name, true),
                          "Usage: prog <A>\n"
                          "\n"
                          "Arguments:\n"
                          "  [A]  \n"
                          "\n"
                          "Options:\n"
                          "  -h, --help  Print help\n"));
}
