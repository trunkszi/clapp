#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <span>
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
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;
    using groups  = std::vector<std::vector<std::string>>;

    // clap's `occurrences_as_vec_vec`.
    groups occurrences_of(const arg_matches& matches, std::string_view id) {
        groups out;
        const std::optional<clapp::raw_occurrences_ref> found = matches.get_raw_occurrences(id);
        if (!found.has_value()) return out;
        for (std::span<const clapp::os_string> one : *found) {
            std::vector<std::string> group;
            for (const clapp::os_string& value : one) group.emplace_back(value.chars());
            out.push_back(std::move(group));
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_appending_option() {
        command_builder app("cli");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec appending_option = make_appending_option();

    consteval command_spec make_backup() {
        command_builder app("cli");
        std::move(app)
                .arg(arg_builder("server").short_('s').action(arg_action::set))
                .arg(arg_builder("user").short_('u').action(arg_action::set))
                .arg(arg_builder("target")
                             .long_("target")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec backup = make_backup();

    consteval command_spec make_long_delimited() {
        command_builder app("myapp");
        std::move(app).arg(arg_builder("option")
                                   .long_("option")
                                   .value_delimiter(',')
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec long_delimited = make_long_delimited();

    consteval command_spec make_short_delimited() {
        command_builder app("myapp");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .value_delimiter(',')
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec short_delimited = make_short_delimited();

    consteval command_spec make_one_positional() {
        command_builder app("multiple_values");
        std::move(app).arg(arg_builder("pos")
                                   .index(1)
                                   .help("multiple positionals")
                                   .action(arg_action::set)
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec one_positional = make_one_positional();

    consteval command_spec make_two_positionals() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("pos1").index(1).help("multiple positionals"))
                .arg(arg_builder("pos2")
                             .index(2)
                             .help("multiple positionals")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec two_positionals = make_two_positionals();

    consteval command_spec make_last_positional() {
        command_builder app("multiple_values");
        std::move(app)
                .arg(arg_builder("pos1").index(1).help("multiple positionals"))
                .arg(arg_builder("pos2")
                             .index(2)
                             .help("multiple positionals")
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1))
                             .last());
        return app.freeze();
    }
    constexpr command_spec last_positional = make_last_positional();

    consteval command_spec make_interleaved() {
        command_builder app("foo");
        std::move(app)
                .arg(arg_builder("pos").index(1).num_args(value_range::at_least(1)))
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec interleaved = make_interleaved();

    consteval command_spec make_ripgrep_1701() {
        command_builder app("ripgrep#1701 reproducer");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("pretty").short_('p').long_("pretty").action(arg_action::set_true))
                .arg(arg_builder("search_zip")
                             .short_('z')
                             .long_("search-zip")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec ripgrep_1701 = make_ripgrep_1701();

    static_assert(appending_option.find_arg("option")->get_action() == arg_action::append);
    static_assert(long_delimited.find_arg("option")->get_value_delimiter() ==
                  std::optional<char>{','});
    static_assert(ripgrep_1701.is_args_override_self());

}  // namespace

// ---------------------------------------------------------------------------
// One group per sighting
// ---------------------------------------------------------------------------

CLAPP_TEST("occurrences.rs::grouped_value_works") {
    const outcome got = clapp::parse(appending_option,
                                     raw_args{"cli",
                                              "--option",
                                              "fr_FR:mon option 1",
                                              "en_US:my option 1",
                                              "--option",
                                              "fr_FR:mon option 2",
                                              "en_US:my option 2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "option") ==
                groups{{"fr_FR:mon option 1", "en_US:my option 1"},
                       {"fr_FR:mon option 2", "en_US:my option 2"}});
}

CLAPP_TEST("occurrences.rs::issue_1026") {
    const outcome got = clapp::parse(backup,
                                     raw_args{"backup",
                                              "-s",
                                              "server",
                                              "-u",
                                              "user",
                                              "--target",
                                              "target1",
                                              "file1",
                                              "file2",
                                              "file3",
                                              "--target",
                                              "target2",
                                              "file4",
                                              "file5",
                                              "file6",
                                              "file7",
                                              "--target",
                                              "target3",
                                              "file8"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "target") ==
                groups{{"target1", "file1", "file2", "file3"},
                       {"target2", "file4", "file5", "file6", "file7"},
                       {"target3", "file8"}});
}

// ---------------------------------------------------------------------------
// A delimiter splits values, not occurrences
// ---------------------------------------------------------------------------

CLAPP_TEST("occurrences.rs::grouped_value_long_flag_delimiter") {
    const outcome got = clapp::parse(
            long_delimited,
            raw_args{"myapp", "--option=hmm", "--option=val1,val2,val3", "--option", "alice,bob"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "option") ==
                groups{{"hmm"}, {"val1", "val2", "val3"}, {"alice", "bob"}});
}

CLAPP_TEST("occurrences.rs::grouped_value_short_flag_delimiter") {
    const outcome got = clapp::parse(short_delimited,
                                     raw_args{"myapp", "-o=foo", "-o=val1,val2,val3", "-o=bar"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "option") ==
                groups{{"foo"}, {"val1", "val2", "val3"}, {"bar"}});
}

// ---------------------------------------------------------------------------
// Positionals group by contiguous run
// ---------------------------------------------------------------------------

CLAPP_TEST("occurrences.rs::grouped_value_positional_arg") {
    const outcome got = clapp::parse(
            one_positional, raw_args{"myprog", "val1", "val2", "val3", "val4", "val5", "val6"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "pos") ==
                groups{{"val1", "val2", "val3", "val4", "val5", "val6"}});
}

CLAPP_TEST("occurrences.rs::grouped_value_multiple_positional_arg") {
    const outcome got = clapp::parse(
            two_positionals, raw_args{"myprog", "val1", "val2", "val3", "val4", "val5", "val6"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "pos2") == groups{{"val2", "val3", "val4", "val5", "val6"}});
}

CLAPP_TEST("occurrences.rs::grouped_value_multiple_positional_arg_last_multiple") {
    const outcome got =
            clapp::parse(last_positional,
                         raw_args{"myprog", "val1", "--", "val2", "val3", "val4", "val5", "val6"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "pos2") == groups{{"val2", "val3", "val4", "val5", "val6"}});
}

CLAPP_TEST("occurrences.rs::grouped_interleaved_positional_values") {
    // Also covers clap's `grouped_interleaved_positional_occurrences`, whose body is
    // byte-identical to this one's in `/Users/quincy/Desktop/clap/tests/builder/
    // occurrences.rs` — same command, same argv, same two assertions. Verified by
    // reading both, not assumed from the names.
    //
    // THREE groups for the positional, because `-f` interrupted it twice.
    const outcome got =
            clapp::parse(interleaved, raw_args{"foo", "1", "2", "-f", "a", "3", "-f", "b", "4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(occurrences_of(*got, "pos") == groups{{"1", "2"}, {"3"}, {"4"}});
    CLAPP_CHECK(occurrences_of(*got, "flag") == groups{{"a"}, {"b"}});
}

// ---------------------------------------------------------------------------
// Repeats inside and across clusters
// ---------------------------------------------------------------------------

CLAPP_TEST("occurrences.rs::issue_2171") {
    // Seven spellings of one ripgrep bug: a repeated flag must not become a conflict
    // just because the repeat happened inside a cluster.
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-pz", "-p"}).has_value());
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-pzp"}).has_value());
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-zpp"}).has_value());
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-pp", "-z"}).has_value());
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-p", "-p", "-z"}).has_value());
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-p", "-pz"}).has_value());
    CLAPP_CHECK(clapp::parse(ripgrep_1701, raw_args{"reproducer", "-ppz"}).has_value());
}
