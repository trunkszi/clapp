#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
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
    using indices = std::vector<std::size_t>;

    indices indices_of(const arg_matches& matches, std::string_view id) {
        const std::optional<std::span<const std::size_t>> found = matches.indices_of(id);
        if (!found.has_value()) return {};
        return indices(found->begin(), found->end());
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_multi_opts() {
        command_builder app("ind");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("exclude")
                             .short_('e')
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("include")
                             .short_('i')
                             .action(arg_action::set)
                             .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec multi_opts = make_multi_opts();

    consteval command_spec make_two_flags() {
        command_builder app("ind");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("exclude").short_('e').action(arg_action::set_true))
                .arg(arg_builder("include").short_('i').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec two_flags = make_two_flags();

    consteval command_spec make_flags_and_option() {
        command_builder app("ind");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("exclude").short_('e').action(arg_action::set_true))
                .arg(arg_builder("include").short_('i').action(arg_action::set_true))
                .arg(arg_builder("option").short_('o').action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec flags_and_option = make_flags_and_option();

    consteval command_spec make_delimited() {
        command_builder app("myapp");
        std::move(app).args_override_self().arg(arg_builder("option")
                                                        .short_('o')
                                                        .action(arg_action::set)
                                                        .value_delimiter(',')
                                                        .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec delimited = make_delimited();

    consteval command_spec make_undelimited() {
        command_builder app("myapp");
        std::move(app).args_override_self().arg(arg_builder("option")
                                                        .short_('o')
                                                        .action(arg_action::set)
                                                        .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec undelimited = make_undelimited();

    consteval command_spec make_append_and_flag() {
        command_builder app("myapp");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("option").short_('o').action(arg_action::append))
                .arg(arg_builder("flag").short_('f').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec append_and_flag = make_append_and_flag();

    static_assert(multi_opts.is_args_override_self());
    static_assert(delimited.find_arg("option")->get_value_delimiter() == std::optional<char>{','});
    static_assert(!undelimited.find_arg("option")->get_value_delimiter().has_value());

}  // namespace

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

CLAPP_TEST("indices.rs::indices_mult_opts") {
    const outcome got =
            clapp::parse(multi_opts, raw_args{"ind", "-e", "A", "B", "-i", "B", "C", "-e", "C"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "exclude") == indices{2, 3, 8});
    CLAPP_CHECK(indices_of(*got, "include") == indices{5, 6});
}

CLAPP_TEST("indices.rs::index_mult_opts") {
    // `index_of` is the FIRST index, not the last.
    const outcome got =
            clapp::parse(multi_opts, raw_args{"ind", "-e", "A", "B", "-i", "B", "C", "-e", "C"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->index_of("exclude") == std::optional<std::size_t>{2});
    CLAPP_CHECK(got->index_of("include") == std::optional<std::size_t>{5});
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------

CLAPP_TEST("indices.rs::index_flag") {
    const outcome got = clapp::parse(two_flags, raw_args{"ind", "-e", "-i"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->index_of("exclude") == std::optional<std::size_t>{1});
    CLAPP_CHECK(got->index_of("include") == std::optional<std::size_t>{2});
}

CLAPP_TEST("indices.rs::index_flags") {
    // Each sighting replaces the stored value; the index follows it to the LAST one.
    const outcome got = clapp::parse(two_flags, raw_args{"ind", "-e", "-i", "-e", "-e", "-i"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->index_of("exclude") == std::optional<std::size_t>{4});
    CLAPP_CHECK(got->index_of("include") == std::optional<std::size_t>{5});
}

CLAPP_TEST("indices.rs::indices_mult_flags") {
    const outcome got = clapp::parse(two_flags, raw_args{"ind", "-e", "-i", "-e", "-e", "-i"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "exclude") == indices{4});
    CLAPP_CHECK(indices_of(*got, "include") == indices{5});
}

CLAPP_TEST("indices.rs::indices_mult_flags_combined") {
    // ONE argv entry, five positions: the counter advances per cluster LETTER.
    const outcome got = clapp::parse(two_flags, raw_args{"ind", "-eieei"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "exclude") == indices{4});
    CLAPP_CHECK(indices_of(*got, "include") == indices{5});
}

CLAPP_TEST("indices.rs::indices_mult_flags_opt_combined") {
    const outcome got = clapp::parse(flags_and_option, raw_args{"ind", "-eieeio", "val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "exclude") == indices{4});
    CLAPP_CHECK(indices_of(*got, "include") == indices{5});
    CLAPP_CHECK(indices_of(*got, "option") == indices{7});
}

CLAPP_TEST("indices.rs::indices_mult_flags_opt_combined_eq") {
    // The attached form must cost exactly as many positions as the detached one.
    const outcome got = clapp::parse(flags_and_option, raw_args{"ind", "-eieeio=val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "exclude") == indices{4});
    CLAPP_CHECK(indices_of(*got, "include") == indices{5});
    CLAPP_CHECK(indices_of(*got, "option") == indices{7});
}

// ---------------------------------------------------------------------------
// Delimiters
// ---------------------------------------------------------------------------

CLAPP_TEST("indices.rs::indices_mult_opt_value_delim_eq") {
    const outcome got = clapp::parse(delimited, raw_args{"myapp", "-o=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "option") == indices{2, 3, 4});
}

CLAPP_TEST("indices.rs::indices_mult_opt_value_no_delim_eq") {
    // Same six characters, one value, one index.
    const outcome got = clapp::parse(undelimited, raw_args{"myapp", "-o=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "option") == indices{2});
}

CLAPP_TEST("indices.rs::indices_mult_opt_mult_flag") {
    const outcome got = clapp::parse(append_and_flag,
                                     raw_args{"myapp", "-o", "val1", "-f", "-o", "val2", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(indices_of(*got, "option") == indices{2, 5});
    CLAPP_CHECK(indices_of(*got, "flag") == indices{6});
}
