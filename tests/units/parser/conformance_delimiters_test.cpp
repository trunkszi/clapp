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

    consteval command_spec make_long_opt() {
        command_builder app("no_delim");
        std::move(app).arg(arg_builder("option").long_("option").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec long_opt = make_long_opt();

    consteval command_spec make_short_opt() {
        command_builder app("no_delim");
        std::move(app).arg(arg_builder("option").short_('o').action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec short_opt = make_short_opt();

    consteval command_spec make_short_multi() {
        command_builder app("no_delim");
        std::move(app).arg(arg_builder("option")
                                   .short_('o')
                                   .action(arg_action::set)
                                   .num_args(value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec short_multi = make_short_multi();

    consteval command_spec make_delimited() {
        command_builder app("no_delim");
        std::move(app).arg(arg_builder("option")
                                   .long_("opt")
                                   .action(arg_action::set)
                                   .num_args(value_range::at_least(1))
                                   .value_delimiter(','));
        return app.freeze();
    }
    constexpr command_spec delimited = make_delimited();

    // The control and the cases really differ in the one setting under test.
    static_assert(!long_opt.find_arg("option")->get_value_delimiter().has_value());
    static_assert(delimited.find_arg("option")->get_value_delimiter() == std::optional<char>{','});

}  // namespace

CLAPP_TEST("delimiters.rs::opt_default_no_delim") {
    const outcome got = clapp::parse(long_opt, raw_args{"", "--option", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("option"));
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
}

CLAPP_TEST("delimiters.rs::opt_eq_no_delim") {
    const outcome got = clapp::parse(long_opt, raw_args{"", "--option=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
}

CLAPP_TEST("delimiters.rs::opt_s_eq_no_delim") {
    const outcome got = clapp::parse(short_opt, raw_args{"", "-o=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
}

CLAPP_TEST("delimiters.rs::opt_s_default_no_delim") {
    const outcome got = clapp::parse(short_opt, raw_args{"", "-o", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
}

CLAPP_TEST("delimiters.rs::opt_s_no_space_no_delim") {
    const outcome got = clapp::parse(short_opt, raw_args{"", "-oval1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "option") == std::optional<std::string>{"val1,val2,val3"});
}

CLAPP_TEST("delimiters.rs::opt_s_no_space_mult_no_delim") {
    // `num_args(1..)` does not imply a delimiter either.
    const outcome got = clapp::parse(short_multi, raw_args{"", "-o", "val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == std::vector<std::string>{"val1,val2,val3"});
}

CLAPP_TEST("delimiters.rs::opt_eq_mult_def_delim") {
    // The control: with the delimiter declared, the same token splits.
    const outcome got = clapp::parse(delimited, raw_args{"", "--opt=val1,val2,val3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "option") == std::vector<std::string>{"val1", "val2", "val3"});
}
