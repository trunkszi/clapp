#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {
    struct required_text {
        std::string value;
    };
}  // namespace

namespace clapp {
    /** clap's NonEmptyStringValueParser, expressed as a clapp value_parser specialization. */
    template<>
    struct value_parser<required_text> {
        [[nodiscard]] static constexpr std::expected<required_text, parse_error>
        parse(os_str value) {
            const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
            if (!text.has_value())
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                                   .input     = value,
                                                   .type_name = "required_text",
                                                   .reason    = "invalid UTF-8"});
            if (text->empty())
                return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                                   .input     = value,
                                                   .type_name = "required_text",
                                                   .reason    = "value must not be empty"});
            return required_text{std::string(*text)};
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

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    consteval command_spec make_long_config() {
        command_builder app("config");
        std::move(app).arg(arg_builder("config").long_("config").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec long_config = make_long_config();

    consteval command_spec make_short_config() {
        command_builder app("config");
        std::move(app).arg(arg_builder("config").short_('c').action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec short_config = make_short_config();

    consteval command_spec make_long_nonempty() {
        command_builder app("config");
        std::move(app).arg(arg_builder("config")
                                   .long_("config")
                                   .action(arg_action::set)
                                   .value_parser<required_text>());
        return app.freeze();
    }
    constexpr command_spec long_nonempty = make_long_nonempty();

    consteval command_spec make_short_nonempty() {
        command_builder app("config");
        std::move(app).arg(arg_builder("config")
                                   .short_('c')
                                   .action(arg_action::set)
                                   .value_parser<required_text>());
        return app.freeze();
    }
    constexpr command_spec short_nonempty = make_short_nonempty();

    consteval command_spec make_nonempty_require_equals() {
        command_builder app("config");
        std::move(app).arg(arg_builder("config")
                                   .long_("config")
                                   .action(arg_action::set)
                                   .value_parser<required_text>()
                                   .require_equals());
        return app.freeze();
    }
    constexpr command_spec nonempty_require_equals = make_nonempty_require_equals();

    static_assert(nonempty_require_equals.find_arg("config")->is_require_equals_set());

}  // namespace

CLAPP_TEST("empty_values.rs::empty_values") {
    const outcome got = clapp::parse(long_config, raw_args{"config", "--config", ""});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "config") == std::optional<std::string>{""});
}

CLAPP_TEST("empty_values.rs::empty_values_with_equals") {
    const outcome long_form = clapp::parse(long_config, raw_args{"config", "--config="});
    CLAPP_CHECK(long_form.has_value());
    CLAPP_CHECK(one_string(*long_form, "config") == std::optional<std::string>{""});

    const outcome short_form = clapp::parse(short_config, raw_args{"config", "-c="});
    CLAPP_CHECK(short_form.has_value());
    CLAPP_CHECK(one_string(*short_form, "config") == std::optional<std::string>{""});
}

CLAPP_TEST("empty_values.rs::no_empty_values") {
    // clap's kind here is InvalidValue; clapp's is value_validation — see the file note.
    const outcome long_form = clapp::parse(long_nonempty, raw_args{"config", "--config", ""});
    CLAPP_CHECK(!long_form.has_value());
    CLAPP_CHECK(kind_of(long_form) == error_kind::value_validation);
    CLAPP_CHECK(message_of(long_form).find("must not be empty") != std::string::npos);

    const outcome short_form = clapp::parse(short_nonempty, raw_args{"config", "-c", ""});
    CLAPP_CHECK(!short_form.has_value());
    CLAPP_CHECK(kind_of(short_form) == error_kind::value_validation);
}

CLAPP_TEST("empty_values.rs::no_empty_values_with_equals") {
    const outcome long_form = clapp::parse(long_nonempty, raw_args{"config", "--config="});
    CLAPP_CHECK(!long_form.has_value());
    CLAPP_CHECK(kind_of(long_form) == error_kind::value_validation);

    const outcome short_form = clapp::parse(short_nonempty, raw_args{"config", "-c="});
    CLAPP_CHECK(!short_form.has_value());
    CLAPP_CHECK(kind_of(short_form) == error_kind::value_validation);
}

CLAPP_TEST("empty_values.rs::no_empty_values_without_equals") {
    // No value at all. clap reports it the same way as an empty one.
    const outcome long_form = clapp::parse(long_nonempty, raw_args{"config", "--config"});
    CLAPP_CHECK(!long_form.has_value());
    CLAPP_CHECK(kind_of(long_form) == error_kind::invalid_value);

    const outcome short_form = clapp::parse(short_nonempty, raw_args{"config", "-c"});
    CLAPP_CHECK(!short_form.has_value());
    CLAPP_CHECK(kind_of(short_form) == error_kind::invalid_value);
}

CLAPP_TEST("empty_values.rs::no_empty_values_without_equals_but_requires_equals") {
    // The ordering case: the SYNTAX complaint wins over the value complaint.
    const outcome got = clapp::parse(nonempty_require_equals, raw_args{"config", "--config"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::no_equals);
    CLAPP_CHECK(message_of(got).find("--config=<config>") != std::string::npos);
}
