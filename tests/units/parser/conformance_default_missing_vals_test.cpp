#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/value_source.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
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
    using clapp::value_source;

    using outcome = std::expected<arg_matches, error>;

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

    consteval command_spec make_color() {
        command_builder app("df");
        std::move(app).arg(arg_builder("color")
                                   .long_("color")
                                   .default_value("auto")
                                   .num_args(value_range::optional())
                                   .require_equals()
                                   .default_missing_value("always"));
        return app.freeze();
    }
    constexpr command_spec color = make_color();

    // clap's `opt_present_with_empty_value` drops `num_args(0..=1)`, so the option takes
    // exactly one value and `--color=` supplies the empty one.
    consteval command_spec make_color_one_value() {
        command_builder app("df");
        std::move(app).arg(arg_builder("color")
                                   .long_("color")
                                   .default_value("auto")
                                   .require_equals()
                                   .default_missing_value("always"));
        return app.freeze();
    }
    constexpr command_spec color_one_value = make_color_one_value();

    consteval command_spec make_both_defaults() {
        command_builder app("cmd");
        std::move(app).arg(arg_builder("o")
                                   .short_('o')
                                   .num_args(value_range::optional())
                                   .default_value("default")
                                   .default_missing_value("default_missing"));
        return app.freeze();
    }
    constexpr command_spec both_defaults = make_both_defaults();

    consteval command_spec make_both_defaults_appending() {
        command_builder app("cmd");
        std::move(app).arg(arg_builder("o")
                                   .short_('o')
                                   .num_args(value_range::optional())
                                   .action(arg_action::append)
                                   .default_value("default")
                                   .default_missing_value("default_missing"));
        return app.freeze();
    }
    constexpr command_spec both_defaults_appending = make_both_defaults_appending();

    consteval command_spec make_flag_value() {
        command_builder app("test");
        std::move(app).arg(arg_builder("flag")
                                   .long_("flag")
                                   .action(arg_action::set)
                                   .num_args(value_range::optional())
                                   .default_value("false")
                                   .default_missing_value("true"));
        return app.freeze();
    }
    constexpr command_spec flag_value = make_flag_value();

    consteval command_spec make_delimited_missing() {
        command_builder app("test");
        std::move(app).arg(arg_builder("flag")
                                   .long_("flag")
                                   .default_value("one,two")
                                   .default_missing_value("three,four")
                                   .num_args(value_range::at_least(0))
                                   .value_delimiter(',')
                                   .require_equals());
        return app.freeze();
    }
    constexpr command_spec delimited_missing = make_delimited_missing();

    consteval command_spec make_color_and_sync() {
        command_builder app("df");
        std::move(app)
                .arg(arg_builder("color")
                             .long_("color")
                             .default_value("auto")
                             .num_args(value_range::optional())
                             .require_equals()
                             .default_missing_value("always"))
                .arg(arg_builder("sync").long_("sync").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec color_and_sync = make_color_and_sync();

    static_assert(color.find_arg("color")->get_num_args() == value_range::optional());
    static_assert(color.find_arg("color")->is_require_equals_set());
    static_assert(color.find_arg("color")->get_default_missing_values().size() == 1);

}  // namespace

// ---------------------------------------------------------------------------
// The two defaults, and the source that tells them apart
// ---------------------------------------------------------------------------

CLAPP_TEST("default_missing_vals.rs::opt_missing") {
    const outcome got = clapp::parse(color, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("color"));
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"auto"});
    CLAPP_CHECK(got->value_source("color") ==
                std::optional<value_source>{value_source::default_value});
    CLAPP_CHECK(got->index_of("color") == std::optional<std::size_t>{1});
}

CLAPP_TEST("default_missing_vals.rs::opt_present_with_missing_value") {
    const outcome got = clapp::parse(color, raw_args{"", "--color"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"always"});
    // The value came from a DEFAULT and its source is still the command line, because
    // the user typed the flag. Routing it through the defaults wave breaks exactly here.
    CLAPP_CHECK(got->value_source("color") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(got->index_of("color") == std::optional<std::size_t>{2});
}

CLAPP_TEST("default_missing_vals.rs::opt_present_with_value") {
    const outcome got = clapp::parse(color, raw_args{"", "--color=never"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"never"});
    CLAPP_CHECK(got->value_source("color") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(got->index_of("color") == std::optional<std::size_t>{2});
}

CLAPP_TEST("default_missing_vals.rs::opt_present_with_empty_value") {
    // An explicit empty value is a value; the missing default must not fire.
    const outcome got = clapp::parse(color_one_value, raw_args{"", "--color="});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{""});
    CLAPP_CHECK(got->value_source("color") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(got->index_of("color") == std::optional<std::size_t>{2});
}

// ---------------------------------------------------------------------------
// Adding default_missing_value changes nothing else
// ---------------------------------------------------------------------------

CLAPP_TEST("default_missing_vals.rs::opt_default") {
    const outcome got = clapp::parse(both_defaults, raw_args{""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("o"));
    CLAPP_CHECK(one_string(*got, "o") == std::optional<std::string>{"default"});
}

CLAPP_TEST("default_missing_vals.rs::opt_default_user_override") {
    const outcome got = clapp::parse(both_defaults, raw_args{"", "-o=value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "o") == std::optional<std::string>{"value"});
}

CLAPP_TEST("default_missing_vals.rs::default_missing_value_per_occurrence") {
    // Three sightings, two of them bare: the missing default fires per occurrence.
    const outcome got = clapp::parse(both_defaults_appending, raw_args{"", "-o", "-o=value", "-o"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "o") ==
                std::vector<std::string>{"default_missing", "value", "default_missing"});
}

CLAPP_TEST("default_missing_vals.rs::default_missing_value_flag_value") {
    const outcome absent = clapp::parse(flag_value, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(one_string(*absent, "flag") == std::optional<std::string>{"false"});
    CLAPP_CHECK(absent->value_source("flag") ==
                std::optional<value_source>{value_source::default_value});

    const outcome bare = clapp::parse(flag_value, raw_args{"test", "--flag"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(one_string(*bare, "flag") == std::optional<std::string>{"true"});
    CLAPP_CHECK(bare->value_source("flag") ==
                std::optional<value_source>{value_source::command_line});

    const outcome said_true = clapp::parse(flag_value, raw_args{"test", "--flag=true"});
    CLAPP_CHECK(said_true.has_value());
    CLAPP_CHECK(one_string(*said_true, "flag") == std::optional<std::string>{"true"});

    // The same STRING as the plain default, but a different source: the user typed it.
    const outcome said_false = clapp::parse(flag_value, raw_args{"test", "--flag=false"});
    CLAPP_CHECK(said_false.has_value());
    CLAPP_CHECK(one_string(*said_false, "flag") == std::optional<std::string>{"false"});
    CLAPP_CHECK(said_false->value_source("flag") ==
                std::optional<value_source>{value_source::command_line});
}

CLAPP_TEST("default_missing_vals.rs::delimited_missing_value") {
    const outcome absent = clapp::parse(delimited_missing, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(raw_of(*absent, "flag") == std::vector<std::string>{"one", "two"});

    const outcome bare = clapp::parse(delimited_missing, raw_args{"test", "--flag"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(raw_of(*bare, "flag") == std::vector<std::string>{"three", "four"});
}

CLAPP_TEST("default_missing_vals.rs::valid_index") {
    // The index must name `--color`'s own position, not the one after it.
    const outcome got = clapp::parse(color_and_sync, raw_args{"df", "--color", "--sync"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"always"});
    CLAPP_CHECK(got->value_source("color") ==
                std::optional<value_source>{value_source::command_line});
    CLAPP_CHECK(got->index_of("color") == std::optional<std::size_t>{2});
}
