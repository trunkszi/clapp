#include <clapp/meta/parse.hpp>
#include "support/check.hpp"

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <variant>
#include <vector>

namespace {
    enum class mode { fast, slow };
    struct [[= clapp::cmd{.auto_default_value = true}]] defaults {
        int port = 8080;
        bool enabled = true;
        std::string host = "localhost";
        mode speed = mode::fast;
        [[= clapp::required_if_eq{.id = "port", .value = "8080"}]] std::optional<std::string> token;
        [[= clapp::arg{.default_value = "91"}]] int explicit_port = 12;
    };
    struct [[= clapp::cmd{.auto_default_value = true}]] floating_defaults {
        double fraction = 0.1;
        double integral = 1.5;
        double large = 1e200;
        double small = 1e-200;
    };
    struct [[= clapp::cmd{.auto_default_value = true}]] default_dependencies {
        [[= clapp::default_value_if{.id = "mode", .predicate_value = "slow", .default_value = "3"}]]
        int retries = 1;
        std::string mode = "slow";
    };
    struct [[= clapp::cmd{.color = clapp::color_choice::always, .styled = true}]] styled_pair {
        std::pair<mode, int> values;
    };
    struct [[= clapp::cmd{.rename_all_env = clapp::naming::snake, .env_prefix = "APP_", .auto_env = true}]] env_names {
        std::optional<std::string> output_path;
        [[= clapp::arg{.env = "CUSTOM"}]] std::optional<int> limit;
        [[= clapp::arg{.env = "APP_EXISTING"}]] std::optional<int> existing;
    };
    struct plain_defaults { int port = 8080; };
    struct field_default { [[= clapp::arg{.auto_default_value = true}]] int port = -42; };
    struct [[= clapp::external_subcommand{}]] external_one { std::vector<std::string> args; };
    struct [[= clapp::external_subcommand{}]] external_two { std::string name; std::vector<std::string> args; };
    struct [[= clapp::external_subcommand{}]] external_colliding_names {
        std::string command_name;
        std::vector<std::string> commandName;
    };
    struct external_collision_command { std::variant<external_colliding_names> command; };
    static_assert(clapp::parsable_command<external_collision_command>);
    struct command_one { std::variant<external_one> command; };
    struct command_two { std::variant<external_two> command; };
    struct [[= clapp::external_subcommand{}]] invalid_external { std::string args; };
    struct invalid_external_command { std::variant<invalid_external> command; };
    struct invalid_subcommand { [[= clapp::subcommand{}]] int command; };
    struct invalid_monostate { std::variant<std::monostate> command; };
    static_assert(!clapp::parsable_command<invalid_external_command>);
    static_assert(!clapp::parsable_command<invalid_subcommand>);
    static_assert(!clapp::parsable_command<invalid_monostate>);
    struct [[= clapp::group{.id = "input", .required = true}]] inputs {
        bool file;
        bool stdin_;
    };
    struct grouped { [[= clapp::flatten{}]] inputs source; };
    struct field_grouped {
        [[= clapp::flatten{}]] [[= clapp::group{.id = "flags", .required = true}]]
        inputs source;
    };
    struct [[= clapp::group{.id = "other", .arg = "trace"}]] explicit_group {
        bool trace;
        bool debug;
    };
    struct conditions {
        std::optional<std::string> mode;
        [[= clapp::required_if_eq{.id = "mode", .value = "fast"}]]
        std::optional<int> threads;
        [[= clapp::requires_if{.value = "yes", .target_id = "threads"}]]
        std::optional<std::string> parallel;
        [[= clapp::default_value_if{.id = "mode", .predicate_value = "slow", .default_value = "3"}]]
        int retries = 1;
    };
    struct aliases {
        [[= clapp::arg{.num_args = clapp::value_range::exactly(2), .alias = "legacy",
                      .visible_alias = "values", .short_alias = 'o', .visible_short_alias = 'v',
                      .value_terminator = ";", .hide_default_value = true, .hide_env = true,
                      .hide_env_values = true, .hide_possible_values = true, .hide_short_help = true,
                      .hide_long_help = true, .next_line_help = true}]]
        std::vector<int> numbers;
    };
    struct [[= clapp::cmd{.version = "1", .alias = "alternate", .visible_alias = "visible",
                          .short_flag = 'x', .long_flag = "execute", .long_version = "1.0-full",
                          .display_name = "Displayed", .bin_name = "bin", .subcommands_negate_reqs = true,
                          .args_negate_subcommands = true, .subcommand_precedence_over_arg = true,
                          .allow_missing_positional = true, .trailing_var_arg = true,
                          .dont_delimit_trailing_values = true, .all_args_override_self = true,
                          .flatten_help = true, .next_line_help = true,
                          .color = clapp::color_choice::always, .styled = true,
                          .term_width = 90, .max_term_width = 100}]] command_options {};
    struct containers {
        std::tuple<int, std::string, mode> values;
        std::set<int> sorted;
        std::unordered_set<int> unique;
    };

    CLAPP_TEST("member defaults opt in to parser defaults and dependency validation") {
        static constexpr auto plain = clapp::command_of<plain_defaults>();
        CLAPP_CHECK(plain.find_arg("port")->get_default_values().empty());
        static constexpr auto spec = clapp::command_of<defaults>();
        CLAPP_CHECK(spec.find_arg("port")->get_default_values()[0].name() == "8080");
        CLAPP_CHECK(spec.find_arg("host")->get_default_values()[0].name() == "localhost");
        CLAPP_CHECK(spec.find_arg("speed")->get_default_values()[0].name() == "fast");
        CLAPP_CHECK(!clapp::try_parse_from<defaults>({"app"}));
        const auto parsed = clapp::try_parse_from<defaults>({"app", "--token", "abc"});
        CLAPP_CHECK(parsed);
        CLAPP_CHECK(parsed->port == 8080 && parsed->enabled && parsed->host == "localhost");
        CLAPP_CHECK(parsed->explicit_port == 91);
        const auto field = clapp::try_parse_from<field_default>({"app"});
        CLAPP_CHECK(field && field->port == -42);
    }
    CLAPP_TEST("floating initializer defaults round trip across magnitude ranges") {
        const auto parsed = clapp::try_parse_from<floating_defaults>({"app"});
        CLAPP_CHECK(parsed && parsed->fraction == 0.1 && parsed->integral == 1.5);
        CLAPP_CHECK(parsed->large == 1e200 && parsed->small == 1e-200);
        static constexpr auto spec = clapp::command_of<floating_defaults>();
        CLAPP_CHECK(spec.find_arg("integral")->get_default_values()[0].name() == "1.5");
    }
    CLAPP_TEST("reflected defaults resolve forward dependency predicates") {
        const auto parsed = clapp::try_parse_from<default_dependencies>({"app"});
        CLAPP_CHECK(parsed && parsed->retries == 3 && parsed->mode == "slow");
        const auto override_value = clapp::try_parse_from<default_dependencies>({"app", "--mode", "fast"});
        CLAPP_CHECK(override_value && override_value->retries == 1);
        const auto ignored_condition = clapp::try_parse_from<defaults>({"app", "--port", "9090"});
        CLAPP_CHECK(ignored_condition && !ignored_condition->token);
    }
    CLAPP_TEST("derive storage errors retain configured ANSI output policy") {
        const auto parsed = clapp::try_parse_from<styled_pair>({"app", "--values", "invalid", "3"});
        CLAPP_CHECK(!parsed && parsed.error().render_ansi().contains("\x1b["));
        styled_pair updated{};
        const auto outcome = clapp::try_update_from(updated, {"app", "--values", "invalid", "3"});
        CLAPP_CHECK(!outcome && outcome.error().render_ansi().contains("\x1b["));
    }
    CLAPP_TEST("command environment naming honors prefix without duplicating it") {
        static constexpr auto spec = clapp::command_of<env_names>();
        CLAPP_CHECK(spec.find_arg("output_path")->get_env() == "APP_output_path");
        CLAPP_CHECK(spec.find_arg("limit")->get_env() == "APP_CUSTOM");
        CLAPP_CHECK(spec.find_arg("existing")->get_env() == "APP_EXISTING");
    }
    CLAPP_TEST("external alternatives capture name and trailing arguments in both shapes") {
        const auto one = clapp::try_parse_from<command_one>({"app", "plugin", "--verbose", "x"});
        CLAPP_CHECK(one);
        CLAPP_CHECK(std::get<external_one>(one->command).args == std::vector<std::string>({"plugin", "--verbose", "x"}));
        const auto two = clapp::try_parse_from<command_two>({"app", "plugin", "--verbose", "x"});
        CLAPP_CHECK(two);
        CLAPP_CHECK(std::get<external_two>(two->command).name == "plugin");
        CLAPP_CHECK(std::get<external_two>(two->command).args == std::vector<std::string>({"--verbose", "x"}));
    }
    CLAPP_TEST("external payload member names do not occupy CLI option namespaces") {
        const auto parsed = clapp::try_parse_from<external_collision_command>({"app", "plugin", "one", "two"});
        CLAPP_CHECK(parsed);
        const auto& captured = std::get<external_colliding_names>(parsed->command);
        CLAPP_CHECK(captured.command_name == "plugin");
        CLAPP_CHECK(captured.commandName == std::vector<std::string>({"one", "two"}));
    }
    CLAPP_TEST("group annotations collect flattened arguments and enforce exclusivity") {
        CLAPP_CHECK(!clapp::try_parse_from<grouped>({"app"}));
        CLAPP_CHECK(clapp::try_parse_from<grouped>({"app", "--file"}));
        CLAPP_CHECK(!clapp::try_parse_from<grouped>({"app", "--file", "--stdin"}));
        static constexpr auto spec = clapp::command_of<field_grouped>();
        CLAPP_CHECK(spec.has_group("flags") && spec.has_group("input"));
        static constexpr auto explicit_spec = clapp::command_of<explicit_group>();
        CLAPP_CHECK(explicit_spec.find_group("other")->get_args().size() == 1);
    }
    CLAPP_TEST("stacked conditional annotations drive required and default values") {
        CLAPP_CHECK(!clapp::try_parse_from<conditions>({"app", "--mode", "fast"}));
        CLAPP_CHECK(!clapp::try_parse_from<conditions>({"app", "--parallel", "yes"}));
        const auto parsed = clapp::try_parse_from<conditions>({"app", "--mode", "slow"});
        CLAPP_CHECK(parsed && parsed->retries == 3);
    }
    CLAPP_TEST("argument aliases and explicit arity reach the builder") {
        static constexpr auto spec = clapp::command_of<aliases>();
        const auto* arg = spec.find_arg("numbers");
        CLAPP_CHECK(arg->get_num_args() == clapp::value_range::exactly(2));
        CLAPP_CHECK(arg->get_all_aliases().size() == 2);
        CLAPP_CHECK(arg->get_value_terminator() == ";");
        const auto parsed = clapp::try_parse_from<aliases>({"app", "--legacy", "1", "2"});
        CLAPP_CHECK(parsed && parsed->numbers == std::vector<int>({1, 2}));
    }
    CLAPP_TEST("command annotation forwards presentation and parser settings") {
        static constexpr auto spec = clapp::command_of<command_options>();
        CLAPP_CHECK(spec.get_long_version() == "1.0-full");
        CLAPP_CHECK(spec.get_color() == clapp::color_choice::always);
        CLAPP_CHECK(spec.get_term_width() == 90);
        CLAPP_CHECK(spec.is_subcommand_negates_reqs_set());
        CLAPP_CHECK(spec.is_args_conflicts_with_subcommands_set());
        CLAPP_CHECK(spec.is_allow_missing_positional_set());
    }
    CLAPP_TEST("tuple and associative containers are derived as heterogeneous and sequence fields") {
        const auto parsed = clapp::try_parse_from<containers>({"app", "--values", "4", "word", "fast", "--sorted", "2", "2", "1", "--unique", "3", "3"});
        CLAPP_CHECK(parsed);
        CLAPP_CHECK(std::get<0>(parsed->values) == 4 && std::get<1>(parsed->values) == "word");
        CLAPP_CHECK(std::get<2>(parsed->values) == mode::fast);
        CLAPP_CHECK(parsed->sorted == std::set<int>({1, 2}));
        CLAPP_CHECK(parsed->unique.size() == 1);
    }
    static_assert(requires { clapp::parse<plain_defaults>(); clapp::try_parse<plain_defaults>(); });
}
