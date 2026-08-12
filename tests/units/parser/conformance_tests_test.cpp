#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <cstdint>
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
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    // ---------------------------------------------------------------------------
    // clap's shared `utils::complex_app()`
    // ---------------------------------------------------------------------------
    //
    // The same fixture also appears in conformance_opts_test.cpp, which needs it for the two
    // suggestion cases. It is transcribed twice rather than shared: each conformance file is
    // self-contained by design, and a shared header would make a change made for one file's
    // sake silently rewrite another file's expectations.
    //
    // One deliberate simplification:
    // clap hangs `["fast", "slow"]` / `["vi", "emacs"]` on the ARGUMENT via `.value_parser`,
    // while in clapp a value domain belongs to the TYPE. `dump_matches()` still writes the
    // `option3 present quickly` / `positional3 present in vi mode` branches, and the sweep
    // below never supplies those values, so the observable output is unaffected — every
    // expected block in clap's file says `option3 NOT present` / `positional3 NOT present`.

    consteval command_spec make_complex_app() {
        command_builder app("clap-test");
        std::move(app)
                .version("v1.4.8")
                .about("tests clap library")
                .author("Kevin K. <kbknapp@gmail.com>")
                .arg(arg_builder("option")
                             .short_('o')
                             .long_("option")
                             .value_name("opt")
                             .help("tests options")
                             .required(false)
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("positional").index(1).help("tests positionals"))
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .help("tests flags")
                             .action(arg_action::count)
                             .global())
                .arg(arg_builder("flag2")
                             .short_('F')
                             .help("tests flags with exclusions")
                             .conflicts_with("flag")
                             .requires_("long-option-2")
                             .action(arg_action::set_true))
                .arg(arg_builder("long-option-2")
                             .long_("long-option-2")
                             .value_name("option2")
                             .help("tests long options with exclusions")
                             .conflicts_with("option")
                             .requires_("positional2")
                             .action(arg_action::set))
                .arg(arg_builder("positional2").index(2).help("tests positionals with exclusions"))
                .arg(arg_builder("option3")
                             .short_('O')
                             .long_("option3")
                             .value_name("option3")
                             .help("specific vals")
                             .action(arg_action::set))
                .arg(arg_builder("positional3")
                             .index(3)
                             .help("tests specific values")
                             .num_args(value_range::at_least(1))
                             .action(arg_action::append))
                .arg(arg_builder("multvals")
                             .long_("multvals")
                             .help("Tests multiple values, not mult occs")
                             .value_names({"one", "two"}))
                .arg(arg_builder("multvalsmo")
                             .long_("multvalsmo")
                             .help("Tests multiple values, and mult occs")
                             .value_names({"one", "two"})
                             .action(arg_action::append))
                .arg(arg_builder("minvals2")
                             .long_("minvals2")
                             .value_name("minvals")
                             .help("Tests 2 min vals")
                             .num_args(value_range::at_least(2)))
                .arg(arg_builder("maxvals3")
                             .long_("maxvals3")
                             .value_name("maxvals")
                             .help("Tests 3 max vals")
                             .num_args(value_range{1, 3}))
                .arg(arg_builder("optvaleq")
                             .long_("optvaleq")
                             .value_name("optval")
                             .help("Tests optional value, require = sign")
                             .num_args(value_range{0, 1})
                             .require_equals())
                .arg(arg_builder("optvalnoeq")
                             .long_("optvalnoeq")
                             .value_name("optval")
                             .help("Tests optional value")
                             .num_args(value_range{0, 1}))
                .subcommand(command_builder("subcmd")
                                    .about("tests subcommands")
                                    .version("0.1")
                                    .arg(arg_builder("option")
                                                 .short_('o')
                                                 .long_("option")
                                                 .value_name("scoption")
                                                 .help("tests options")
                                                 .num_args(value_range::at_least(1)))
                                    .arg(arg_builder("subcmdarg")
                                                 .short_('s')
                                                 .long_("subcmdarg")
                                                 .value_name("subcmdarg")
                                                 .help("tests other args")
                                                 .action(arg_action::set))
                                    .arg(arg_builder("scpositional")
                                                 .index(1)
                                                 .help("tests positionals")));
        return app.freeze();
    }
    constexpr command_spec complex_app = make_complex_app();

    // The fixture's shape, asserted where it is built: the sweep below is meaningless if
    // `-f` is not a counter or `-o` does not append.
    static_assert(complex_app.find_arg("flag")->get_action() == arg_action::count);
    static_assert(complex_app.find_arg("option")->get_action() == arg_action::append);
    static_assert(complex_app.has_subcommand("subcmd"));

    // ---------------------------------------------------------------------------
    // clap's `check_complex_output`, transcribed
    // ---------------------------------------------------------------------------

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    std::vector<std::string> many_strings(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    void write_option_and_positional(std::string& w, const arg_matches& m) {
        if (m.contains_id("option")) {
            if (const std::optional<std::string> v = one_string(m, "option"); v.has_value())
                w += "option present with value: " + *v + "\n";
            for (const std::string& o : many_strings(m, "option")) w += "An option: " + o + "\n";
        } else {
            w += "option NOT present\n";
        }

        if (const std::optional<std::string> p = one_string(m, "positional"); p.has_value())
            w += "positional present with value: " + *p + "\n";
        else
            w += "positional NOT present\n";
    }

    std::string complex_output(const arg_matches& m) {
        std::string w;

        const std::optional<const std::uint8_t*> flag = m.get_one<std::uint8_t>("flag");
        const unsigned n                              = flag.has_value() ? **flag : 0U;
        if (n == 0)
            w += "flag NOT present\n";
        else
            w += "flag present " + std::to_string(n) + " times\n";

        write_option_and_positional(w, m);

        if (m.get_flag("flag2")) {
            w += "flag2 present\n";
            w += "option2 present with value of: " + one_string(m, "long-option-2").value_or("") +
                 "\n";
            w += "positional2 present with value of: " + one_string(m, "positional2").value_or("") +
                 "\n";
        } else {
            w += "flag2 NOT present\n";
            w += "option2 maybe present with value of: " +
                 one_string(m, "long-option-2").value_or("Nothing") + "\n";
            w += "positional2 maybe present with value of: " +
                 one_string(m, "positional2").value_or("Nothing") + "\n";
        }

        const std::string option3 = one_string(m, "option3").value_or("");
        if (option3 == "fast")
            w += "option3 present quickly\n";
        else if (option3 == "slow")
            w += "option3 present slowly\n";
        else
            w += "option3 NOT present\n";

        const std::string positional3 = one_string(m, "positional3").value_or("");
        if (positional3 == "vi")
            w += "positional3 present in vi mode\n";
        else if (positional3 == "emacs")
            w += "positional3 present in emacs mode\n";
        else
            w += "positional3 NOT present\n";

        write_option_and_positional(w, m);

        if (m.subcommand_name() == std::optional<std::string_view>{"subcmd"}) {
            w += "subcmd present\n";
            if (const arg_matches* sub = m.subcommand_matches("subcmd"); sub != nullptr) {
                const std::optional<const std::uint8_t*> sc_flag =
                        sub->get_one<std::uint8_t>("flag");
                const unsigned sc_n = sc_flag.has_value() ? **sc_flag : 0U;
                if (sc_n == 0)
                    w += "flag NOT present\n";
                else
                    w += "flag present " + std::to_string(sc_n) + " times\n";

                if (sub->contains_id("option")) {
                    if (const std::optional<std::string> v = one_string(*sub, "option");
                        v.has_value())
                        w += "scoption present with value: " + *v + "\n";
                    for (const std::string& o : many_strings(*sub, "option"))
                        w += "An scoption: " + o + "\n";
                } else {
                    w += "scoption NOT present\n";
                }

                if (const std::optional<std::string> p = one_string(*sub, "scpositional");
                    p.has_value())
                    w += "scpositional present with value: " + *p + "\n";
            }
        } else {
            w += "subcmd NOT present\n";
        }

        return w;
    }

    /**
     * \brief Parse one argv against `complex_app` and compare the full dump, printing both
     *        on a mismatch. A parse failure is reported as a failure too, with the error
     *        text — clap's helper `unwrap()`s, which would only say "panicked".
     */
    bool dump_matches(raw_args argv, std::string_view want) {
        const outcome got = clapp::parse(complex_app, argv);
        if (!got.has_value()) {
            std::println("--- parse failed ---\n{}", got.error().render().to_string());
            return false;
        }
        const std::string text = complex_output(*got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
    }

}  // namespace

CLAPP_TEST("tests.rs::flag_x2_opt") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "-f", "-f", "-o", "some"},
                             "flag present 2 times\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::long_opt_x2_pos") {
    CLAPP_CHECK(
            dump_matches(raw_args{"clap-test", "value", "--option", "some", "--option", "other"},
                         "flag NOT present\n"
                         "option present with value: some\n"
                         "An option: some\n"
                         "An option: other\n"
                         "positional present with value: value\n"
                         "flag2 NOT present\n"
                         "option2 maybe present with value of: Nothing\n"
                         "positional2 maybe present with value of: Nothing\n"
                         "option3 NOT present\n"
                         "positional3 NOT present\n"
                         "option present with value: some\n"
                         "An option: some\n"
                         "An option: other\n"
                         "positional present with value: value\n"
                         "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::long_opt_eq_x2_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "--option=some", "--option=other"},
                             "flag NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "An option: other\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "An option: other\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::short_opt_x2_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "-o", "some", "-o", "other"},
                             "flag NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "An option: other\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "An option: other\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::short_opt_eq_x2_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "-o=some", "-o=other"},
                             "flag NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "An option: other\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "An option: other\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::short_flag_x2_comb_short_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "-ff", "-o", "some"},
                             "flag present 2 times\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::short_flag_short_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "-f", "-o", "some"},
                             "flag present 1 times\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::long_flag_long_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "--flag", "--option", "some"},
                             "flag present 1 times\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::long_flag_long_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "value", "--flag", "--option=some"},
                             "flag present 1 times\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option present with value: some\n"
                             "An option: some\n"
                             "positional present with value: value\n"
                             "subcmd NOT present\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_long_opt") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "--flag", "--option", "some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_short_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "--flag", "-o", "some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_long_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "--flag", "--option=some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_long_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "--option", "some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_short_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "-o", "some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_short_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "-o=some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_long_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "--option=some"},
                             "flag present 1 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 1 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_comb_long_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-ff", "--option", "some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_comb_short_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-ff", "-o", "some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_comb_long_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-ff", "--option=some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_comb_short_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-ff", "-o=some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_x2_long_opt_pos") {
    CLAPP_CHECK(dump_matches(
            raw_args{"clap-test", "subcmd", "value", "--flag", "--flag", "--option", "some"},
            "flag present 2 times\n"
            "option NOT present\n"
            "positional NOT present\n"
            "flag2 NOT present\n"
            "option2 maybe present with value of: Nothing\n"
            "positional2 maybe present with value of: Nothing\n"
            "option3 NOT present\n"
            "positional3 NOT present\n"
            "option NOT present\n"
            "positional NOT present\n"
            "subcmd present\n"
            "flag present 2 times\n"
            "scoption present with value: some\n"
            "An scoption: some\n"
            "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_x2_short_opt_pos") {
    CLAPP_CHECK(
            dump_matches(raw_args{"clap-test", "subcmd", "value", "--flag", "--flag", "-o", "some"},
                         "flag present 2 times\n"
                         "option NOT present\n"
                         "positional NOT present\n"
                         "flag2 NOT present\n"
                         "option2 maybe present with value of: Nothing\n"
                         "positional2 maybe present with value of: Nothing\n"
                         "option3 NOT present\n"
                         "positional3 NOT present\n"
                         "option NOT present\n"
                         "positional NOT present\n"
                         "subcmd present\n"
                         "flag present 2 times\n"
                         "scoption present with value: some\n"
                         "An scoption: some\n"
                         "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_x2_short_opt_eq_pos") {
    CLAPP_CHECK(
            dump_matches(raw_args{"clap-test", "subcmd", "value", "--flag", "--flag", "-o=some"},
                         "flag present 2 times\n"
                         "option NOT present\n"
                         "positional NOT present\n"
                         "flag2 NOT present\n"
                         "option2 maybe present with value of: Nothing\n"
                         "positional2 maybe present with value of: Nothing\n"
                         "option3 NOT present\n"
                         "positional3 NOT present\n"
                         "option NOT present\n"
                         "positional NOT present\n"
                         "subcmd present\n"
                         "flag present 2 times\n"
                         "scoption present with value: some\n"
                         "An scoption: some\n"
                         "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_long_flag_x2_long_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(
            raw_args{"clap-test", "subcmd", "value", "--flag", "--flag", "--option=some"},
            "flag present 2 times\n"
            "option NOT present\n"
            "positional NOT present\n"
            "flag2 NOT present\n"
            "option2 maybe present with value of: Nothing\n"
            "positional2 maybe present with value of: Nothing\n"
            "option3 NOT present\n"
            "positional3 NOT present\n"
            "option NOT present\n"
            "positional NOT present\n"
            "subcmd present\n"
            "flag present 2 times\n"
            "scoption present with value: some\n"
            "An scoption: some\n"
            "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_long_opt_pos") {
    CLAPP_CHECK(
            dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "-f", "--option", "some"},
                         "flag present 2 times\n"
                         "option NOT present\n"
                         "positional NOT present\n"
                         "flag2 NOT present\n"
                         "option2 maybe present with value of: Nothing\n"
                         "positional2 maybe present with value of: Nothing\n"
                         "option3 NOT present\n"
                         "positional3 NOT present\n"
                         "option NOT present\n"
                         "positional NOT present\n"
                         "subcmd present\n"
                         "flag present 2 times\n"
                         "scoption present with value: some\n"
                         "An scoption: some\n"
                         "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_short_opt_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "-f", "-o", "some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_short_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "-f", "-o=some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}

CLAPP_TEST("tests.rs::sc_short_flag_x2_long_opt_eq_pos") {
    CLAPP_CHECK(dump_matches(raw_args{"clap-test", "subcmd", "value", "-f", "-f", "--option=some"},
                             "flag present 2 times\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "flag2 NOT present\n"
                             "option2 maybe present with value of: Nothing\n"
                             "positional2 maybe present with value of: Nothing\n"
                             "option3 NOT present\n"
                             "positional3 NOT present\n"
                             "option NOT present\n"
                             "positional NOT present\n"
                             "subcmd present\n"
                             "flag present 2 times\n"
                             "scoption present with value: some\n"
                             "An scoption: some\n"
                             "scpositional present with value: value\n"));
}
