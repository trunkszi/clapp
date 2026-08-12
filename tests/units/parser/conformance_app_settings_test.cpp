#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/output/help.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <initializer_list>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::color_choice;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::help_style;
    using clapp::os_string;
    using clapp::raw_args;
    using clapp::value_range;

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

    /** \brief Every raw byte string recorded for \p id, or an empty vector when absent. */
    std::vector<std::string> raw_of(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    std::string page(const command_spec& cmd, bool long_form, std::string_view usage_name = {}) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd),
                                             .usage_name = usage_name})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    outcome run(const command_spec& cmd, std::initializer_list<std::string_view> argv) {
        std::vector<std::string> owned;
        for (const std::string_view one : argv) owned.emplace_back(one);
        return clapp::parse(cmd, raw_args(std::from_range, owned));
    }

    // ---------------------------------------------------------------------------
    // Fixtures — subcommand requirements
    // ---------------------------------------------------------------------------

    consteval command_spec make_negate_required() {
        command_builder app("sub_command_negate");
        std::move(app)
                .subcommand_negates_reqs()
                .arg(arg_builder("test").required().index(1))
                .subcommand(command_builder("sub1"));
        return app.freeze();
    }
    constexpr command_spec negate_required = make_negate_required();

    consteval command_spec make_sc_required() {
        command_builder app("sc_required");
        std::move(app).subcommand_required().subcommand(command_builder("sub1"));
        return app.freeze();
    }
    constexpr command_spec sc_required = make_sc_required();

    // ---------------------------------------------------------------------------
    // Fixtures — arg_required_else_help
    // ---------------------------------------------------------------------------

    consteval command_spec make_ares_optional() {
        command_builder app("arg_required");
        std::move(app).arg_required_else_help().arg(arg_builder("test").index(1));
        return app.freeze();
    }
    constexpr command_spec ares_optional = make_ares_optional();

    consteval command_spec make_ares_required() {
        command_builder app("arg_required");
        std::move(app).arg_required_else_help().arg(arg_builder("test").index(1).required());
        return app.freeze();
    }
    constexpr command_spec ares_required = make_ares_required();

    consteval command_spec make_ares_subcommand() {
        command_builder app("sub_required");
        std::move(app).arg_required_else_help().subcommand_required().subcommand(
                command_builder("sub1"));
        return app.freeze();
    }
    constexpr command_spec ares_subcommand = make_ares_subcommand();

    consteval command_spec make_ares_default() {
        command_builder app("arg_required");
        std::move(app).arg_required_else_help().arg(arg_builder("input")
                                                            .long_("input")
                                                            .value_name("PATH")
                                                            .action(arg_action::set)
                                                            .default_value("-"));
        return app.freeze();
    }
    constexpr command_spec ares_default = make_ares_default();

    consteval command_spec make_ares_message() {
        command_builder app("test");
        std::move(app).arg_required_else_help().version("1.0").arg(
                arg_builder("info")
                        .help("Provides more info")
                        .short_('i')
                        .long_("info")
                        .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec ares_message = make_ares_message();

    // ---------------------------------------------------------------------------
    // Fixtures — inference
    // ---------------------------------------------------------------------------

    consteval command_spec make_infer_two() {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("test"))
                .subcommand(command_builder("temp"));
        return app.freeze();
    }
    constexpr command_spec infer_two = make_infer_two();

    consteval command_spec make_infer_two_with_positional() {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .arg(arg_builder("some").index(1))
                .subcommand(command_builder("test"))
                .subcommand(command_builder("temp"));
        return app.freeze();
    }
    constexpr command_spec infer_two_with_positional = make_infer_two_with_positional();

    consteval command_spec make_infer_one() {
        command_builder app("prog");
        std::move(app).infer_subcommands().subcommand(command_builder("test"));
        return app.freeze();
    }
    constexpr command_spec infer_one = make_infer_one();

    consteval command_spec make_infer_exact() {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("test"))
                .subcommand(command_builder("testa"))
                .subcommand(command_builder("testb"));
        return app.freeze();
    }
    constexpr command_spec infer_exact = make_infer_exact();

    consteval command_spec make_infer_conflicting_aliases() {
        command_builder app("prog");
        std::move(app).infer_subcommands().subcommand(
                command_builder("test").aliases({"testa", "t", "testb"}));
        return app.freeze();
    }
    constexpr command_spec infer_conflicting_aliases = make_infer_conflicting_aliases();

    consteval command_spec make_infer_long_flag_conflicting_aliases() {
        command_builder app("prog");
        std::move(app).infer_subcommands().subcommand(
                command_builder("c").long_flag("test").long_flag_aliases({"testa", "t", "testb"}));
        return app.freeze();
    }
    constexpr command_spec infer_long_flag_conflicting_aliases =
            make_infer_long_flag_conflicting_aliases();

    consteval command_spec make_infer_long_flag_one() {
        command_builder app("prog");
        std::move(app).infer_subcommands().subcommand(command_builder("test").long_flag("testa"));
        return app.freeze();
    }
    constexpr command_spec infer_long_flag_one = make_infer_long_flag_one();

    consteval command_spec make_infer_long_flag_two() {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("a").long_flag("test"))
                .subcommand(command_builder("b").long_flag("temp"));
        return app.freeze();
    }
    constexpr command_spec infer_long_flag_two = make_infer_long_flag_two();

    // ---------------------------------------------------------------------------
    // Fixtures — no_binary_name, hide_possible_values
    // ---------------------------------------------------------------------------

    consteval command_spec make_no_bin_name() {
        command_builder app("arg_required");
        std::move(app).no_binary_name().arg(arg_builder("test").required().index(1));
        return app.freeze();
    }
    constexpr command_spec no_bin_name = make_no_bin_name();

    // clap declares the domains inline (`value_parser(["one", "two"])`); clapp derives them
    // from the type — see the DIVERGENCE note in conformance_possible_values_test.cpp.
    enum class opt_domain { one, two };
    enum class pos_domain { three, four };

    consteval command_builder skip_pv_shell() {
        return command_builder("test")
                .author("Kevin K.")
                .about("tests stuff")
                .version("1.3")
                .arg(arg_builder("opt")
                             .short_('o')
                             .long_("opt")
                             .value_name("opt")
                             .help("some option")
                             .action(arg_action::set)
                             .value_parser<opt_domain>())
                .arg(arg_builder("arg1").index(1).help("some pos arg").value_parser<pos_domain>());
    }

    consteval command_spec make_skip_possible_values() {
        command_builder app = skip_pv_shell();
        std::move(app).hide_possible_values();
        return app.freeze();
    }
    constexpr command_spec skip_possible_values = make_skip_possible_values();

    /**
     * The negative control: the same command WITHOUT the setting. Without it the
     * `skip_possible_values` assertion would also pass for a renderer that never printed
     * possible values at all.
     */
    consteval command_spec make_show_possible_values() {
        command_builder app = skip_pv_shell();
        return app.freeze();
    }
    constexpr command_spec show_possible_values = make_show_possible_values();

    // ---------------------------------------------------------------------------
    // Fixtures — delimiters and trailing values
    // ---------------------------------------------------------------------------

    consteval command_spec make_onlypos_no_delim() {
        command_builder app("onlypos");
        std::move(app)
                .dont_delimit_trailing_values()
                .arg(arg_builder("f").short_('f').value_name("flag").action(arg_action::set))
                .arg(arg_builder("arg")
                             .index(1)
                             .help("some arg")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec onlypos_no_delim = make_onlypos_no_delim();

    consteval command_spec make_dont_delim_trailing() {
        command_builder app("positional");
        std::move(app).dont_delimit_trailing_values().arg(
                arg_builder("opt")
                        .index(1)
                        .help("some pos")
                        .num_args(value_range::at_least(1))
                        .action(arg_action::append)
                        .trailing_var_arg());
        return app.freeze();
    }
    constexpr command_spec dont_delim_trailing = make_dont_delim_trailing();

    consteval command_spec make_onlypos() {
        command_builder app("onlypos");
        std::move(app)
                .arg(arg_builder("f").short_('f').value_name("flag").num_args(
                        value_range::optional()))
                .arg(arg_builder("arg")
                             .index(1)
                             .help("some arg")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec onlypos = make_onlypos();

    consteval command_spec make_delim_trailing() {
        command_builder app("positional");
        std::move(app).arg(arg_builder("opt")
                                   .index(1)
                                   .help("some pos")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append)
                                   .trailing_var_arg());
        return app.freeze();
    }
    constexpr command_spec delim_trailing = make_delim_trailing();

    consteval command_spec make_onlypos_with_delim() {
        command_builder app("onlypos");
        std::move(app)
                .arg(arg_builder("f").short_('f').value_name("flag").num_args(
                        value_range::optional()))
                .arg(arg_builder("arg")
                             .index(1)
                             .help("some arg")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append)
                             .value_delimiter(','));
        return app.freeze();
    }
    constexpr command_spec onlypos_with_delim = make_onlypos_with_delim();

    /**
     * The discriminating fixture clap does not have: `onlypos_with_delim` PLUS
     * `dont_delimit_trailing_values`. Every one of clap's six delimiter cases declares the
     * setting only on a command whose positional has no delimiter to suppress, so all six
     * stay green if the setting does nothing at all. This one does not.
     */
    consteval command_spec make_onlypos_with_delim_stopped() {
        command_builder app("onlypos");
        std::move(app)
                .dont_delimit_trailing_values()
                .arg(arg_builder("f").short_('f').value_name("flag").num_args(
                        value_range::optional()))
                .arg(arg_builder("arg")
                             .index(1)
                             .help("some arg")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append)
                             .value_delimiter(','));
        return app.freeze();
    }
    constexpr command_spec onlypos_with_delim_stopped = make_onlypos_with_delim_stopped();

    consteval command_spec make_delim_trailing_with_delim() {
        command_builder app("positional");
        std::move(app).arg(arg_builder("opt")
                                   .index(1)
                                   .help("some pos")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append)
                                   .value_delimiter(',')
                                   .trailing_var_arg());
        return app.freeze();
    }
    constexpr command_spec delim_trailing_with_delim = make_delim_trailing_with_delim();

    // ---------------------------------------------------------------------------
    // Fixtures — hyphen values
    // ---------------------------------------------------------------------------

    consteval command_spec make_leading_hyphen_pos() {
        command_builder app("leadhy");
        std::move(app)
                .arg(arg_builder("some").index(1).allow_hyphen_values())
                .arg(arg_builder("other").short_('o').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec leading_hyphen_pos = make_leading_hyphen_pos();

    consteval command_spec make_leading_hyphen_option() {
        command_builder app("leadhy");
        std::move(app)
                .arg(arg_builder("some").action(arg_action::set).long_("opt").allow_hyphen_values())
                .arg(arg_builder("other").short_('o').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec leading_hyphen_option = make_leading_hyphen_option();

    consteval command_spec make_negnum() {
        command_builder app("negnum");
        std::move(app)
                .arg(arg_builder("panum").index(1).allow_negative_numbers())
                .arg(arg_builder("onum")
                             .short_('o')
                             .action(arg_action::set)
                             .allow_negative_numbers());
        return app.freeze();
    }
    constexpr command_spec negnum = make_negnum();

    consteval command_spec make_var_args() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("prog").short_('p').long_("prog").action(arg_action::set_true))
                .arg(arg_builder("opt")
                             .index(1)
                             .help("some pos")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append)
                             .trailing_var_arg()
                             .allow_hyphen_values());
        return app.freeze();
    }
    constexpr command_spec var_args = make_var_args();

    consteval command_spec make_required_hyphen_option() {
        command_builder app("prog");
        std::move(app).arg(arg_builder("some-argument")
                                   .long_("some-argument")
                                   .value_name("val")
                                   .action(arg_action::set)
                                   .required()
                                   .allow_hyphen_values());
        return app.freeze();
    }
    constexpr command_spec required_hyphen_option = make_required_hyphen_option();

    consteval command_spec make_hyphen_positional() {
        command_builder app("tmp");
        std::move(app).arg(arg_builder("pat").index(1).allow_hyphen_values().required().action(
                arg_action::set));
        return app.freeze();
    }
    constexpr command_spec hyphen_positional = make_hyphen_positional();

    // ---------------------------------------------------------------------------
    // Fixtures — misc command settings
    // ---------------------------------------------------------------------------

    consteval command_spec make_disable_help_sub() {
        command_builder app("disablehelp");
        std::move(app).disable_help_subcommand().subcommand(command_builder("sub1"));
        return app.freeze();
    }
    constexpr command_spec disable_help_sub = make_disable_help_sub();

    consteval command_spec make_three_positionals() {
        command_builder app("clap-test");
        std::move(app).version("v1.4.8").args({arg_builder("arg1").index(1).help("some"),
                                               arg_builder("arg2").index(2).help("some"),
                                               arg_builder("arg3").index(3).help("some")});
        return app.freeze();
    }
    constexpr command_spec three_positionals = make_three_positionals();

    consteval command_spec make_require_eq() {
        command_builder app("clap-test");
        std::move(app).version("v1.4.8").arg(arg_builder("opt")
                                                     .long_("opt")
                                                     .short_('o')
                                                     .required()
                                                     .require_equals()
                                                     .action(arg_action::set)
                                                     .value_name("FILE")
                                                     .help("some"));
        return app.freeze();
    }
    constexpr command_spec require_eq = make_require_eq();

    consteval command_spec make_propagate_down() {
        command_builder app("myprog");
        std::move(app)
                .arg(arg_builder("cmd").index(1).help("command to run").global())
                .subcommand(command_builder("foo"));
        return app.freeze();
    }
    constexpr command_spec propagate_down = make_propagate_down();

    // ---------------------------------------------------------------------------
    // Fixtures — allow_missing_positional
    // ---------------------------------------------------------------------------

    consteval command_spec make_amp_default() {
        command_builder app("test");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("src").index(1).help("some file").default_value("src"))
                .arg(arg_builder("dest").index(2).help("some file").required());
        return app.freeze();
    }
    constexpr command_spec amp_default = make_amp_default();

    consteval command_spec make_amp_no_default() {
        command_builder app("test");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("src").index(1).help("some file"))
                .arg(arg_builder("dest").index(2).help("some file").required());
        return app.freeze();
    }
    constexpr command_spec amp_no_default = make_amp_no_default();

    consteval command_spec make_bench_one() {
        command_builder app("bench");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("BENCH").index(1).help("some bench"))
                .arg(arg_builder("ARGS")
                             .index(2)
                             .help("some args")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec bench_one = make_bench_one();

    consteval command_spec make_bench_three() {
        command_builder app("bench");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("BENCH1").index(1).help("some bench"))
                .arg(arg_builder("BENCH2").index(2).help("some bench"))
                .arg(arg_builder("BENCH3").index(3).help("some bench"))
                .arg(arg_builder("ARGS")
                             .index(4)
                             .help("some args")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec bench_three = make_bench_three();

    consteval command_spec make_bench_required() {
        command_builder app("bench");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("BENCH1").index(1).help("some bench"))
                .arg(arg_builder("BENCH2").index(2).help("some bench").required())
                .arg(arg_builder("ARGS")
                             .index(3)
                             .help("some args")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec bench_required = make_bench_required();

    // ---------------------------------------------------------------------------
    // Fixtures — external subcommands
    // ---------------------------------------------------------------------------

    consteval command_spec make_ext_sc() {
        command_builder app("clap-test");
        std::move(app).version("v1.4.8").allow_external_subcommands();
        return app.freeze();
    }
    constexpr command_spec ext_sc = make_ext_sc();

    consteval command_spec make_ext_sc_required() {
        command_builder app("clap-test");
        std::move(app).version("v1.4.8").allow_external_subcommands().subcommand_required();
        return app.freeze();
    }
    constexpr command_spec ext_sc_required = make_ext_sc_required();

    consteval command_spec make_package_manager() {
        command_builder app("pkg");
        std::move(app).version("1.26.0").allow_external_subcommands().subcommand(
                command_builder("install"));
        return app.freeze();
    }
    constexpr command_spec package_manager = make_package_manager();

    // ---------------------------------------------------------------------------
    // Fixtures — args_override_self
    // ---------------------------------------------------------------------------

    consteval command_spec make_aaos_other_overrides() {
        command_builder app("posix");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("opt")
                             .long_("opt")
                             .value_name("val")
                             .help("some option")
                             .action(arg_action::set))
                .arg(arg_builder("other")
                             .long_("other")
                             .value_name("val")
                             .help("some other option")
                             .overrides_with("opt")
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec aaos_other_overrides = make_aaos_other_overrides();

    consteval command_spec make_aaos_other_overrides_required() {
        command_builder app("posix");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("opt")
                             .long_("opt")
                             .value_name("val")
                             .help("some option")
                             .required()
                             .action(arg_action::set))
                .arg(arg_builder("other")
                             .long_("other")
                             .value_name("val")
                             .help("some other option")
                             .required()
                             .overrides_with("opt")
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec aaos_other_overrides_required = make_aaos_other_overrides_required();

    consteval command_spec make_aaos_opt_overrides() {
        command_builder app("posix");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("opt")
                             .long_("opt")
                             .value_name("val")
                             .help("some option")
                             .overrides_with("other")
                             .action(arg_action::set))
                .arg(arg_builder("other")
                             .long_("other")
                             .value_name("val")
                             .help("some other option")
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec aaos_opt_overrides = make_aaos_opt_overrides();

    consteval command_spec make_aaos_opt_overrides_required() {
        command_builder app("posix");
        std::move(app)
                .args_override_self()
                .arg(arg_builder("opt")
                             .long_("opt")
                             .value_name("val")
                             .help("some option")
                             .required()
                             .overrides_with("other")
                             .action(arg_action::set))
                .arg(arg_builder("other")
                             .long_("other")
                             .value_name("val")
                             .help("some other option")
                             .required()
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec aaos_opt_overrides_required = make_aaos_opt_overrides_required();

    // No `args_override_self` here: clap's `aaos_opts_w_override_as_conflict_*` drop it.
    consteval command_spec make_override_as_conflict() {
        command_builder app("posix");
        std::move(app)
                .arg(arg_builder("opt")
                             .long_("opt")
                             .value_name("val")
                             .help("some option")
                             .required()
                             .overrides_with("other")
                             .action(arg_action::set))
                .arg(arg_builder("other")
                             .long_("other")
                             .value_name("val")
                             .help("some other option")
                             .required()
                             .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec override_as_conflict = make_override_as_conflict();

    consteval command_spec make_aaos_mult_delims() {
        command_builder app("posix");
        std::move(app).arg(arg_builder("opt")
                                   .long_("opt")
                                   .value_name("val")
                                   .help("some option")
                                   .action(arg_action::set)
                                   .value_delimiter(',')
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec aaos_mult_delims = make_aaos_mult_delims();

    consteval command_spec make_aaos_mult() {
        command_builder app("posix");
        std::move(app).arg(arg_builder("opt")
                                   .long_("opt")
                                   .value_name("val")
                                   .help("some option")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec aaos_mult = make_aaos_mult();

    consteval command_spec make_aaos_pos_mult() {
        command_builder app("posix");
        std::move(app).arg(arg_builder("val")
                                   .index(1)
                                   .help("some pos")
                                   .num_args(value_range::at_least(1))
                                   .action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec aaos_pos_mult = make_aaos_pos_mult();

    consteval command_spec make_aaos_no_delim() {
        command_builder app("posix");
        std::move(app).args_override_self().arg(arg_builder("opt")
                                                        .long_("opt")
                                                        .value_name("val")
                                                        .help("some option")
                                                        .required()
                                                        .action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec aaos_no_delim = make_aaos_no_delim();

    // ---------------------------------------------------------------------------
    // clap's `color_is_global`, answered at compile time
    // ---------------------------------------------------------------------------

    consteval command_spec make_coloured() {
        command_builder app("myprog");
        std::move(app).color(color_choice::never).subcommand(command_builder("foo"));
        return app.freeze();
    }
    constexpr command_spec coloured = make_coloured();

    static_assert(coloured.get_color() == color_choice::never);
    static_assert(coloured.find_subcommand("foo")->get_color() == color_choice::never);

    static_assert(negate_required.is_subcommand_negates_reqs_set());
    static_assert(sc_required.is_subcommand_required_set());
    static_assert(ares_optional.is_arg_required_else_help_set());
    static_assert(no_bin_name.is_no_binary_name_set());
    static_assert(skip_possible_values.is_hide_possible_values_set());
    static_assert(!show_possible_values.is_hide_possible_values_set());
    static_assert(onlypos_no_delim.is_dont_delimit_trailing_values_set());
    static_assert(disable_help_sub.is_disable_help_subcommand_set());
    static_assert(amp_default.is_allow_missing_positional_set());
    static_assert(ext_sc.is_allow_external_subcommands_set());
    static_assert(aaos_other_overrides.is_args_override_self());
    static_assert(!override_as_conflict.is_args_override_self());

}  // namespace

// ---------------------------------------------------------------------------
// subcommand_negates_reqs / subcommand_required
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::sub_command_negate_required") {
    CLAPP_CHECK(clapp::parse(negate_required, raw_args{"myprog", "sub1"}).has_value());
}

CLAPP_TEST("app_settings.rs::sub_command_negate_required_2") {
    const outcome got = clapp::parse(negate_required, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("app_settings.rs::sub_command_required") {
    const outcome got = clapp::parse(sc_required, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_subcommand);
}

CLAPP_TEST("app_settings.rs::sub_command_required_error") {
    const outcome got = clapp::parse(sc_required, raw_args{"sc_required"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(same(message_of(got),
                     "error: 'sc_required' requires a subcommand but one was not provided\n"
                     "  [subcommands: sub1, help]\n"
                     "\n"
                     "Usage: sc_required <COMMAND>\n"
                     "\n"
                     "For more information, try '--help'.\n"));
}

// ---------------------------------------------------------------------------
// arg_required_else_help outranks every other requirement rule
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::arg_required_else_help") {
    const outcome got = clapp::parse(ares_optional, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help_on_missing_argument_or_subcommand);
}

CLAPP_TEST("app_settings.rs::arg_required_else_help_over_req_arg") {
    const outcome got = clapp::parse(ares_required, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help_on_missing_argument_or_subcommand);
}

CLAPP_TEST("app_settings.rs::arg_required_else_help_over_req_subcommand") {
    const outcome got = clapp::parse(ares_subcommand, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help_on_missing_argument_or_subcommand);
}

CLAPP_TEST("app_settings.rs::arg_required_else_help_with_default") {
    // The default satisfies every requirement; the setting still fires, because it asks
    // what the USER supplied rather than what the matches ended up holding.
    const outcome got = clapp::parse(ares_default, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help_on_missing_argument_or_subcommand);
}

CLAPP_TEST("app_settings.rs::arg_required_else_help_error_message") {
    const outcome got = clapp::parse(ares_message, raw_args{"test"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help_on_missing_argument_or_subcommand);
    // "Unlike normal displaying of help, we should provide a fatal exit code."
    CLAPP_CHECK(got.error().exit_code() != 0);
    CLAPP_CHECK(same(message_of(got),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -i, --info     Provides more info\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

// ---------------------------------------------------------------------------
// infer_subcommands
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::infer_subcommands_fail_no_args") {
    const outcome got = clapp::parse(infer_two, raw_args{"prog", "te"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
}

CLAPP_TEST("app_settings.rs::infer_subcommands_fail_with_args") {
    // With a positional declared, an ambiguous prefix is simply its value.
    const outcome got = clapp::parse(infer_two_with_positional, raw_args{"prog", "t"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "some") == std::optional<std::string>{"t"});
}

CLAPP_TEST("app_settings.rs::infer_subcommands_fail_with_args2") {
    const outcome got = clapp::parse(infer_two_with_positional, raw_args{"prog", "te"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "some") == std::optional<std::string>{"te"});
}

CLAPP_TEST("app_settings.rs::infer_subcommands_pass") {
    const outcome got = clapp::parse(infer_one, raw_args{"prog", "te"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("app_settings.rs::infer_subcommands_pass_close") {
    const outcome got = clapp::parse(infer_two, raw_args{"prog", "tes"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("app_settings.rs::infer_subcommands_pass_exact_match") {
    const outcome got = clapp::parse(infer_exact, raw_args{"prog", "test"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("app_settings.rs::infer_subcommands_pass_conflicting_aliases") {
    // `te` is a prefix of `test`, `testa` and `testb` — but the last two are ALIASES of
    // the first, so there is only one candidate command and the inference succeeds.
    const outcome got = clapp::parse(infer_conflicting_aliases, raw_args{"prog", "te"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("app_settings.rs::infer_long_flag_pass_conflicting_aliases") {
    const outcome got = clapp::parse(infer_long_flag_conflicting_aliases, raw_args{"prog", "--te"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"c"});
}

CLAPP_TEST("app_settings.rs::infer_long_flag") {
    const outcome got = clapp::parse(infer_long_flag_one, raw_args{"prog", "--te"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("app_settings.rs::infer_subcommands_long_flag_fail_with_args2") {
    // Ambiguous through the FLAG path: an unknown argument, not an invalid subcommand.
    const outcome got = clapp::parse(infer_long_flag_two, raw_args{"prog", "--te"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("app_settings.rs::infer_subcommands_fail_suggestions") {
    // `temps` is not a prefix of anything, so inference does not apply at all.
    const outcome got = clapp::parse(infer_two, raw_args{"prog", "temps"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
}

// ---------------------------------------------------------------------------
// no_binary_name
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::no_bin_name") {
    const outcome got = clapp::parse(no_bin_name, raw_args{"testing"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "test") == std::optional<std::string>{"testing"});
}

// ---------------------------------------------------------------------------
// hide_possible_values
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::skip_possible_values") {
    CLAPP_CHECK(same(page(skip_possible_values, true),
                     "tests stuff\n"
                     "\n"
                     "Usage: test [OPTIONS] [arg1]\n"
                     "\n"
                     "Arguments:\n"
                     "  [arg1]  some pos arg\n"
                     "\n"
                     "Options:\n"
                     "  -o, --opt <opt>  some option\n"
                     "  -h, --help       Print help\n"
                     "  -V, --version    Print version\n"));

    // The negative control: the same command without the setting DOES list them, so the
    // page above is a statement about `hide_possible_values` rather than about a
    // renderer that never lists anything.
    const std::string shown = page(show_possible_values, true);
    CLAPP_CHECK(shown.find("[possible values: one, two]") != std::string::npos);
    CLAPP_CHECK(shown.find("[possible values: three, four]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Delimiters after `--`, and under trailing_var_arg
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::stop_delim_values_only_pos_follows") {
    const outcome got = clapp::parse(onlypos_no_delim, raw_args{"", "--", "-f", "-g,x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(!got->contains_id("f"));
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"-f", "-g,x"});
}

CLAPP_TEST("app_settings.rs::dont_delim_values_trailingvararg") {
    const outcome got =
            clapp::parse(dont_delim_trailing, raw_args{"", "test", "--foo", "-Wl,-bar"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt") == std::vector<std::string>{"test", "--foo", "-Wl,-bar"});
}

CLAPP_TEST("app_settings.rs::delim_values_only_pos_follows") {
    const outcome got = clapp::parse(onlypos, raw_args{"", "--", "-f", "-g,x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(!got->contains_id("f"));
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"-f", "-g,x"});
}

CLAPP_TEST("app_settings.rs::delim_values_trailingvararg") {
    const outcome got = clapp::parse(delim_trailing, raw_args{"", "test", "--foo", "-Wl,-bar"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt") == std::vector<std::string>{"test", "--foo", "-Wl,-bar"});
}

CLAPP_TEST("app_settings.rs::delim_values_only_pos_follows_with_delim") {
    // `--` does not suppress the delimiter: `-g,x` becomes two values.
    const outcome got = clapp::parse(onlypos_with_delim, raw_args{"", "--", "-f", "-g,x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("arg"));
    CLAPP_CHECK(!got->contains_id("f"));
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"-f", "-g", "x"});
}

CLAPP_TEST("app_settings.rs::delim_values_only_pos_follows_with_delim, stopped") {
    // Not one of clap's cases, and the reason is in the fixture's doc comment: without
    // it, every `dont_delimit_trailing_values` assertion in this file would also hold
    // for an implementation that ignored the setting. Same command line as the case
    // above, same declared delimiter, setting ON — `-g,x` stays whole.
    const outcome got = clapp::parse(onlypos_with_delim_stopped, raw_args{"", "--", "-f", "-g,x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "arg") == std::vector<std::string>{"-f", "-g,x"});
}

CLAPP_TEST("app_settings.rs::delim_values_trailingvararg_with_delim") {
    const outcome got =
            clapp::parse(delim_trailing_with_delim, raw_args{"", "test", "--foo", "-Wl,-bar"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt") == std::vector<std::string>{"test", "--foo", "-Wl", "-bar"});
}

// ---------------------------------------------------------------------------
// allow_hyphen_values / allow_negative_numbers
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::leading_hyphen_short") {
    const outcome got = clapp::parse(leading_hyphen_pos, raw_args{"", "-bar", "-o"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("some"));
    CLAPP_CHECK(got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "some") == std::optional<std::string>{"-bar"});
    CLAPP_CHECK(got->get_flag("other"));
}

CLAPP_TEST("app_settings.rs::leading_hyphen_long") {
    const outcome got = clapp::parse(leading_hyphen_pos, raw_args{"", "--bar", "-o"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("some"));
    CLAPP_CHECK(got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "some") == std::optional<std::string>{"--bar"});
    CLAPP_CHECK(got->get_flag("other"));
}

CLAPP_TEST("app_settings.rs::leading_hyphen_opt") {
    const outcome got = clapp::parse(leading_hyphen_option, raw_args{"", "--opt", "--bar", "-o"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("some"));
    CLAPP_CHECK(got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "some") == std::optional<std::string>{"--bar"});
    CLAPP_CHECK(got->get_flag("other"));
}

CLAPP_TEST("app_settings.rs::allow_negative_numbers_success") {
    const outcome got = clapp::parse(negnum, raw_args{"negnum", "-20", "-o", "-1.2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "panum") == std::optional<std::string>{"-20"});
    CLAPP_CHECK(one_string(*got, "onum") == std::optional<std::string>{"-1.2"});
}

CLAPP_TEST("app_settings.rs::allow_negative_numbers_fail") {
    // A number is a value; a flag is still a flag.
    const outcome got = clapp::parse(negnum, raw_args{"negnum", "--foo", "-o", "-1.2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

// ---------------------------------------------------------------------------
// trailing_var_arg — three token shapes, two positions each
// ---------------------------------------------------------------------------

namespace {
    /** clap's `assert_trailing_var_args`. */
    bool trailing_var_args(std::initializer_list<std::string_view> argv,
                           const std::vector<std::string>& expected_values,
                           bool expected_flag) {
        const outcome got = run(var_args, argv);
        if (!got.has_value()) {
            std::println("--- unexpected error ---\n{}", message_of(got));
            return false;
        }
        if (raw_of(*got, "opt") != expected_values) {
            std::println("--- got {} values ---", raw_of(*got, "opt").size());
            for (const std::string& one : raw_of(*got, "opt")) std::println("  '{}'", one);
            return false;
        }
        return got->get_flag("prog") == expected_flag;
    }
}  // namespace

CLAPP_TEST("app_settings.rs::trailing_var_arg_with_hyphen_values_escape_first") {
    CLAPP_CHECK(trailing_var_args({"test", "--", "foo", "bar"}, {"foo", "bar"}, false));
}

CLAPP_TEST("app_settings.rs::trailing_var_arg_with_hyphen_values_escape_middle") {
    // The var-arg has already started, so the second `--` is one of its values.
    CLAPP_CHECK(trailing_var_args({"test", "foo", "--", "bar"}, {"foo", "--", "bar"}, false));
}

CLAPP_TEST("app_settings.rs::trailing_var_arg_with_hyphen_values_short_first") {
    CLAPP_CHECK(trailing_var_args({"test", "-p", "foo", "bar"}, {"foo", "bar"}, true));
}

CLAPP_TEST("app_settings.rs::trailing_var_arg_with_hyphen_values_short_middle") {
    CLAPP_CHECK(trailing_var_args({"test", "foo", "-p", "bar"}, {"foo", "-p", "bar"}, false));
}

CLAPP_TEST("app_settings.rs::trailing_var_arg_with_hyphen_values_long_first") {
    CLAPP_CHECK(trailing_var_args({"test", "--prog", "foo", "bar"}, {"foo", "bar"}, true));
}

CLAPP_TEST("app_settings.rs::trailing_var_arg_with_hyphen_values_long_middle") {
    CLAPP_CHECK(
            trailing_var_args({"test", "foo", "--prog", "bar"}, {"foo", "--prog", "bar"}, false));
}

// ---------------------------------------------------------------------------
// disable_help_subcommand, and two help screens
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::disable_help_subcommand") {
    const outcome got = clapp::parse(disable_help_sub, raw_args{"", "help"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
}

CLAPP_TEST("app_settings.rs::dont_collapse_args") {
    CLAPP_CHECK(same(page(three_positionals, true),
                     "Usage: clap-test [arg1] [arg2] [arg3]\n"
                     "\n"
                     "Arguments:\n"
                     "  [arg1]  some\n"
                     "  [arg2]  some\n"
                     "  [arg3]  some\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("app_settings.rs::require_eq") {
    CLAPP_CHECK(same(page(require_eq, true),
                     "Usage: clap-test --opt=<FILE>\n"
                     "\n"
                     "Options:\n"
                     "  -o, --opt=<FILE>  some\n"
                     "  -h, --help        Print help\n"
                     "  -V, --version     Print version\n"));
}

// ---------------------------------------------------------------------------
// A global positional
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::propagate_vals_down") {
    const outcome got = clapp::parse(propagate_down, raw_args{"myprog", "set", "foo"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "cmd") == std::optional<std::string>{"set"});
    const arg_matches* sub = got->subcommand_matches("foo");
    CLAPP_CHECK(sub != nullptr);
    CLAPP_CHECK(one_string(*sub, "cmd") == std::optional<std::string>{"set"});
}

// ---------------------------------------------------------------------------
// allow_missing_positional
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::allow_missing_positional") {
    const outcome got = clapp::parse(amp_default, raw_args{"test", "file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "src") == std::optional<std::string>{"src"});
    CLAPP_CHECK(one_string(*got, "dest") == std::optional<std::string>{"file"});
}

CLAPP_TEST("app_settings.rs::allow_missing_positional_no_default") {
    const outcome got = clapp::parse(amp_no_default, raw_args{"test", "file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "src") == std::nullopt);
    CLAPP_CHECK(one_string(*got, "dest") == std::optional<std::string>{"file"});
}

CLAPP_TEST("app_settings.rs::missing_positional_no_hyphen") {
    const outcome got = clapp::parse(bench_one, raw_args{"bench", "foo", "arg1", "arg2", "arg3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "BENCH") == std::optional<std::string>{"foo"});
    CLAPP_CHECK(raw_of(*got, "ARGS") == std::vector<std::string>{"arg1", "arg2", "arg3"});
}

CLAPP_TEST("app_settings.rs::missing_positional_hyphen") {
    // `--` right away: the FIRST slot is the one left empty.
    const outcome got = clapp::parse(bench_one, raw_args{"bench", "--", "arg1", "arg2", "arg3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "BENCH") == std::nullopt);
    CLAPP_CHECK(raw_of(*got, "ARGS") == std::vector<std::string>{"arg1", "arg2", "arg3"});
}

CLAPP_TEST("app_settings.rs::missing_positional_hyphen_far_back") {
    const outcome got =
            clapp::parse(bench_three, raw_args{"bench", "foo", "--", "arg1", "arg2", "arg3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "BENCH1") == std::optional<std::string>{"foo"});
    CLAPP_CHECK(one_string(*got, "BENCH2") == std::nullopt);
    CLAPP_CHECK(one_string(*got, "BENCH3") == std::nullopt);
    CLAPP_CHECK(raw_of(*got, "ARGS") == std::vector<std::string>{"arg1", "arg2", "arg3"});
}

CLAPP_TEST("app_settings.rs::missing_positional_hyphen_req_error") {
    // The setting redistributes slots; it does not excuse a required one.
    const outcome got =
            clapp::parse(bench_required, raw_args{"bench", "foo", "--", "arg1", "arg2", "arg3"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

// ---------------------------------------------------------------------------
// allow_hyphen_values does not make an unknown flag a value
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::issue_1066_allow_leading_hyphen_and_unknown_args_option") {
    const outcome got = clapp::parse(required_hyphen_option, raw_args{"prog", "-fish"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
}

CLAPP_TEST("app_settings.rs::issue_1437_allow_hyphen_values_for_positional_arg") {
    const outcome got = clapp::parse(hyphen_positional, raw_args{"tmp", "-file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "pat") == std::optional<std::string>{"-file"});
}

CLAPP_TEST("app_settings.rs::issue_3880_allow_long_flag_hyphen_value_for_positional_arg") {
    const outcome got = clapp::parse(hyphen_positional, raw_args{"", "--file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "pat") == std::optional<std::string>{"--file"});
}

// ---------------------------------------------------------------------------
// allow_external_subcommands
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::issue_1093_allow_ext_sc") {
    CLAPP_CHECK(same(page(ext_sc, true),
                     "Usage: clap-test [COMMAND]\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

CLAPP_TEST("app_settings.rs::allow_ext_sc_empty_args") {
    const outcome got = clapp::parse(ext_sc, raw_args{"clap-test", "external-cmd"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"external-cmd"});
    const auto selected = got->subcommand();
    CLAPP_CHECK(selected.has_value());
    if (!selected.has_value()) return;
    const arg_matches& child = selected->second;
    // clap asserts `get_many::<OsString>("").unwrap()` is EMPTY — present with zero
    // values, not absent. `raw_of` alone cannot tell the two apart, so both halves are
    // asserted: an implementation that never created the entry would pass an
    // `.empty()` check and fail here.
    CLAPP_CHECK(child.contains_id(""));
    const std::optional<std::span<const os_string>> values = child.get_raw("");
    CLAPP_CHECK(values.has_value() && values->empty());
}

CLAPP_TEST("app_settings.rs::allow_ext_sc_when_sc_required") {
    const outcome got = clapp::parse(ext_sc_required, raw_args{"clap-test", "external-cmd", "foo"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"external-cmd"});
    const auto selected = got->subcommand();
    CLAPP_CHECK(selected.has_value());
    if (!selected.has_value()) return;
    CLAPP_CHECK(raw_of(selected->second, "") == std::vector<std::string>{"foo"});
}

CLAPP_TEST("app_settings.rs::external_subcommand_looks_like_built_in") {
    const outcome got = clapp::parse(package_manager,
                                     raw_args{"pkg", "install-update", "foo"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"install-update"});
    const auto selected = got->subcommand();
    CLAPP_CHECK(selected.has_value());
    if (!selected.has_value()) return;
    CLAPP_CHECK(raw_of(selected->second, "") == std::vector<std::string>{"foo"});
}

CLAPP_TEST("app_settings.rs::built_in_subcommand_escaped") {
    // `--` forces the DECLARED `install` down the external path.
    const outcome got = clapp::parse(package_manager,
                                     raw_args{"pkg", "--", "install", "foo"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"install"});
    const arg_matches* child = got->subcommand_matches("install");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(raw_of(*child, "") == std::vector<std::string>{"foo"});
}

// ---------------------------------------------------------------------------
// args_override_self, paired with overrides_with
// ---------------------------------------------------------------------------

CLAPP_TEST("app_settings.rs::aaos_opts_w_other_overrides") {
    const outcome got = clapp::parse(aaos_other_overrides,
                                     raw_args{"", "--opt=some", "--other=test", "--opt=other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(!got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"other"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_w_other_overrides_rev") {
    const outcome got = clapp::parse(aaos_other_overrides_required,
                                     raw_args{"", "--opt=some", "--opt=other", "--other=val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("opt"));
    CLAPP_CHECK(got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "other") == std::optional<std::string>{"val"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_w_other_overrides_2") {
    const outcome got = clapp::parse(aaos_opt_overrides,
                                     raw_args{"", "--opt=some", "--other=test", "--opt=other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(!got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"other"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_w_other_overrides_rev_2") {
    const outcome got = clapp::parse(aaos_opt_overrides_required,
                                     raw_args{"", "--opt=some", "--opt=other", "--other=val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("opt"));
    CLAPP_CHECK(got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "other") == std::optional<std::string>{"val"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_w_override_as_conflict_1") {
    // Both are `required`, and only one is supplied: the declared override discharges
    // the other's requirement without ever firing.
    const outcome got = clapp::parse(override_as_conflict, raw_args{"", "--opt=some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(!got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"some"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_w_override_as_conflict_2") {
    const outcome got = clapp::parse(override_as_conflict, raw_args{"", "--other=some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("opt"));
    CLAPP_CHECK(got->contains_id("other"));
    CLAPP_CHECK(one_string(*got, "other") == std::optional<std::string>{"some"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_mult_req_delims") {
    const outcome got = clapp::parse(aaos_mult_delims,
                                     raw_args{"", "--opt=some", "--opt=other", "--opt=one,two"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt") == std::vector<std::string>{"some", "other", "one", "two"});
}

CLAPP_TEST("app_settings.rs::aaos_opts_mult") {
    const outcome got = clapp::parse(
            aaos_mult,
            raw_args{"", "--opt", "first", "overrides", "--opt", "some", "other", "val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt") ==
                std::vector<std::string>{"first", "overrides", "some", "other", "val"});
}

CLAPP_TEST("app_settings.rs::aaos_pos_mult") {
    const outcome got = clapp::parse(aaos_pos_mult, raw_args{"", "some", "other", "value"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("val"));
    CLAPP_CHECK(raw_of(*got, "val") == std::vector<std::string>{"some", "other", "value"});
}

CLAPP_TEST("app_settings.rs::aaos_option_use_delim_false") {
    // No delimiter declared, so `some,other` is ONE value — and the second `--opt`
    // replaces it wholesale under `args_override_self`.
    const outcome got =
            clapp::parse(aaos_no_delim, raw_args{"", "--opt=some,other", "--opt=one,two"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(raw_of(*got, "opt") == std::vector<std::string>{"one,two"});
}
