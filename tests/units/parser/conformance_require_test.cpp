#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_condition;
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

    /**
     * \brief Whole-block comparison against clap's expected string, printing both on a
     *        mismatch. `positional_required_with_requires_if_value` needs it: the message
     *        lists TWO missing arguments on separate indented lines, and their order and
     *        indentation are the whole content.
     */
    bool same_block(const outcome& got, std::string_view want) {
        const std::string text = message_of(got);
        if (text == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", text, want);
        return false;
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    // clap's `value_parser(["file", "stdout"])`, expressed the way clapp enumerates a domain.
    enum class target_kind { file, stdout_ };

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_flag_requires() {
        command_builder app("flag_required");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .requires_("color"))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_requires = make_flag_requires();

    consteval command_spec make_option_requires() {
        command_builder app("option_required");
        std::move(app)
                .arg(arg_builder("f").short_('f').value_name("flag").requires_("c"))
                .arg(arg_builder("c").short_('c').value_name("color"));
        return app.freeze();
    }
    constexpr command_spec option_requires = make_option_requires();

    consteval command_spec make_required_positional() {
        command_builder app("positional_required");
        std::move(app).arg(arg_builder("flag").index(1).required());
        return app.freeze();
    }
    constexpr command_spec required_positional = make_required_positional();

    consteval command_spec make_positional_requires() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("flag").index(1).required().requires_("opt"))
                .arg(arg_builder("opt").index(2))
                .arg(arg_builder("bar").index(3));
        return app.freeze();
    }
    constexpr command_spec positional_requires = make_positional_requires();

    consteval command_spec make_positional_requires_if() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("flag").index(1).required().requires_if("val", "opt"))
                .arg(arg_builder("opt").index(2))
                .arg(arg_builder("bar").index(3));
        return app.freeze();
    }
    constexpr command_spec positional_requires_if = make_positional_requires_if();

    consteval command_spec make_required_group() {
        command_builder app("group_required");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").action(arg_action::set_true))
                .group(group_builder("gr").required().arg("some").arg("other"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec required_group = make_required_group();

    consteval command_spec make_arg_requires_group() {
        command_builder app("arg_require_group");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .action(arg_action::set_true)
                             .requires_("gr"))
                .group(group_builder("gr").arg("some").arg("other"))
                .arg(arg_builder("some").long_("some").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec arg_requires_group = make_arg_requires_group();

    consteval command_spec make_issue_753() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("list")
                             .short_('l')
                             .long_("list")
                             .action(arg_action::set_true)
                             .help("List available interfaces (and stop there)"))
                .arg(arg_builder("iface")
                             .short_('i')
                             .long_("iface")
                             .value_name("INTERFACE")
                             .required(false)
                             .required_unless_present("list"))
                .arg(arg_builder("file")
                             .short_('f')
                             .long_("file")
                             .value_name("TESTFILE")
                             .conflicts_with("iface")
                             .required_unless_present("list"))
                .arg(arg_builder("server")
                             .short_('s')
                             .long_("server")
                             .value_name("SERVER_IP")
                             .required_unless_present("list"));
        return app.freeze();
    }
    constexpr command_spec issue_753 = make_issue_753();

    consteval command_spec make_unless_present() {
        command_builder app("unlesstest");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_unless_present("dbg")
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("dbg").long_("debug").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec unless_present = make_unless_present();

    consteval command_spec make_unless_present_with_optional() {
        command_builder app("unlesstest");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").num_args(value_range::optional()))
                .arg(arg_builder("cfg")
                             .required_unless_present("dbg")
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("dbg").long_("debug").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec unless_present_with_optional = make_unless_present_with_optional();

    consteval command_spec make_unless_all() {
        command_builder app("unlessall");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_unless_present_all({"dbg", "infile"})
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("dbg").long_("debug").action(arg_action::set_true))
                .arg(arg_builder("infile").short_('i').action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec unless_all = make_unless_all();

    consteval command_spec make_unless_any() {
        command_builder app("unlessone");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_unless_present_any({"dbg", "infile"})
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("dbg").long_("debug").action(arg_action::set_true))
                .arg(arg_builder("infile").short_('i').action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec unless_any = make_unless_any();

    consteval command_spec make_unless_any_short() {
        command_builder app("unlessone");
        std::move(app)
                .arg(arg_builder("a").short_('a').action(arg_action::set_true).conflicts_with("b"))
                .arg(arg_builder("b").short_('b').action(arg_action::set_true))
                .arg(arg_builder("x")
                             .short_('x')
                             .action(arg_action::set_true)
                             .required_unless_present_any({"a", "b"}));
        return app.freeze();
    }
    constexpr command_spec unless_any_short = make_unless_any_short();

    consteval command_spec make_unless_any_positional() {
        command_builder app("unlessone");
        std::move(app)
                .arg(arg_builder("a").short_('a').action(arg_action::set_true).conflicts_with("b"))
                .arg(arg_builder("b").short_('b').action(arg_action::set_true))
                .arg(arg_builder("x").index(1).required_unless_present_any({"a", "b"}));
        return app.freeze();
    }
    constexpr command_spec unless_any_positional = make_unless_any_positional();

    consteval command_spec make_unless_any_long() {
        command_builder app("unlessone");
        std::move(app)
                .arg(arg_builder("a").short_('a').action(arg_action::set_true).conflicts_with("b"))
                .arg(arg_builder("b").short_('b').action(arg_action::set_true))
                .arg(arg_builder("x")
                             .long_("x_is_the_option")
                             .action(arg_action::set_true)
                             .required_unless_present_any({"a", "b"}));
        return app.freeze();
    }
    constexpr command_spec unless_any_long = make_unless_any_long();

    consteval command_spec make_requires_if() {
        command_builder app("unlessone");
        std::move(app)
                .arg(arg_builder("cfg")
                             .requires_if("my.cfg", "extra")
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").long_("extra").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec requires_if_one = make_requires_if();

    consteval command_spec make_requires_ifs() {
        command_builder app("unlessone");
        std::move(app)
                .arg(arg_builder("cfg")
                             .requires_if("my.cfg", "extra")
                             .requires_if("other.cfg", "other")
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").long_("extra").action(arg_action::set_true))
                .arg(arg_builder("other").long_("other").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec requires_if_many = make_requires_ifs();

    consteval command_spec make_required_if_eq() {
        command_builder app("ri");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_if_eq("extra", "val")
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").action(arg_action::set).long_("extra"));
        return app.freeze();
    }
    constexpr command_spec required_if_eq = make_required_if_eq();

    consteval command_spec make_required_if_eq_ignore_case() {
        command_builder app("ri");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_if_eq("extra", "Val")
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").action(arg_action::set).long_("extra").ignore_case());
        return app.freeze();
    }
    constexpr command_spec required_if_eq_ignore_case = make_required_if_eq_ignore_case();

    consteval command_spec make_required_if_eq_all() {
        command_builder app("ri");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_if_eq_all({{"extra", "val"}, {"option", "spec"}})
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").action(arg_action::set).long_("extra"))
                .arg(arg_builder("option").action(arg_action::set).long_("option"));
        return app.freeze();
    }
    constexpr command_spec required_if_eq_all = make_required_if_eq_all();

    consteval command_spec make_required_if_eq_all_and_any() {
        command_builder app("ri");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_if_eq_all({{"extra", "val"}, {"option", "spec"}})
                             .required_if_eq_any({{"extra", "val2"}, {"option", "spec2"}})
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").action(arg_action::set).long_("extra"))
                .arg(arg_builder("option").action(arg_action::set).long_("option"));
        return app.freeze();
    }
    constexpr command_spec required_if_eq_all_and_any = make_required_if_eq_all_and_any();

    consteval command_spec make_required_if_eq_any() {
        command_builder app("ri");
        std::move(app)
                .arg(arg_builder("cfg")
                             .required_if_eq_any({{"extra", "val"}, {"option", "spec"}})
                             .action(arg_action::set)
                             .long_("config"))
                .arg(arg_builder("extra").action(arg_action::set).long_("extra"))
                .arg(arg_builder("option").action(arg_action::set).long_("option"));
        return app.freeze();
    }
    constexpr command_spec required_if_eq_any = make_required_if_eq_any();

    consteval command_spec make_three_required() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("target")
                             .action(arg_action::set)
                             .required()
                             .value_parser<target_kind>()
                             .long_("target"))
                .arg(arg_builder("input").action(arg_action::set).required().long_("input"))
                .arg(arg_builder("output")
                             .action(arg_action::set)
                             .required_if_eq("target", "file")
                             .long_("output"));
        return app.freeze();
    }
    constexpr command_spec three_required = make_three_required();

    consteval command_spec make_require_eq() {
        command_builder app("clap-test");
        std::move(app).version("v1.4.8").arg(arg_builder("opt")
                                                     .long_("opt")
                                                     .short_('o')
                                                     .required()
                                                     .require_equals()
                                                     .value_name("FILE")
                                                     .help("some"));
        return app.freeze();
    }
    constexpr command_spec require_eq = make_require_eq();

    consteval command_spec make_require_eq_filtered() {
        command_builder app("clap-test");
        std::move(app)
                .version("v1.4.8")
                .arg(arg_builder("opt")
                             .long_("opt")
                             .short_('o')
                             .required()
                             .require_equals()
                             .value_name("FILE")
                             .help("some"))
                .arg(arg_builder("foo")
                             .long_("foo")
                             .short_('f')
                             .required()
                             .require_equals()
                             .value_name("FILE")
                             .help("some other arg"));
        return app.freeze();
    }
    constexpr command_spec require_eq_filtered = make_require_eq_filtered();

    consteval command_spec make_require_eq_filtered_group() {
        command_builder app("clap-test");
        std::move(app)
                .version("v1.4.8")
                .arg(arg_builder("opt")
                             .long_("opt")
                             .short_('o')
                             .required()
                             .require_equals()
                             .value_name("FILE")
                             .help("some"))
                .arg(arg_builder("foo")
                             .long_("foo")
                             .short_('f')
                             .required()
                             .require_equals()
                             .value_name("FILE")
                             .help("some other arg"))
                .arg(arg_builder("g1").long_("g1").require_equals().value_name("FILE"))
                .arg(arg_builder("g2").long_("g2").require_equals().value_name("FILE"))
                .group(group_builder("test_group").args({"g1", "g2"}).required());
        return app.freeze();
    }
    constexpr command_spec require_eq_filtered_group = make_require_eq_filtered_group();

    consteval command_spec make_multiple_unless() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("a")
                             .long_("a")
                             .action(arg_action::set)
                             .required_unless_present("b")
                             .conflicts_with("b"))
                .arg(arg_builder("b")
                             .long_("b")
                             .action(arg_action::set)
                             .required_unless_present("a")
                             .conflicts_with("a"))
                .arg(arg_builder("c")
                             .long_("c")
                             .action(arg_action::set)
                             .required_unless_present("d")
                             .conflicts_with("d"))
                .arg(arg_builder("d")
                             .long_("d")
                             .action(arg_action::set)
                             .required_unless_present("c")
                             .conflicts_with("c"));
        return app.freeze();
    }
    constexpr command_spec multiple_unless = make_multiple_unless();

    // clap's `issue_1158_app`.
    consteval command_spec make_issue_1158() {
        command_builder app("example");
        std::move(app)
                .arg(arg_builder("config")
                             .short_('c')
                             .long_("config")
                             .value_name("FILE")
                             .help("Custom config file.")
                             .required_unless_present("ID")
                             .conflicts_with("ID"))
                .arg(arg_builder("ID")
                             .index(1)
                             .help("ID")
                             .required_unless_present("config")
                             .conflicts_with("config")
                             .requires_all({"x", "y", "z"}))
                .arg(arg_builder("x").short_('x').value_name("X").help("X"))
                .arg(arg_builder("y").short_('y').value_name("Y").help("Y"))
                .arg(arg_builder("z").short_('z').value_name("Z").help("Z"));
        return app.freeze();
    }
    constexpr command_spec issue_1158 = make_issue_1158();

    consteval command_spec make_mutual_requires() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("relation_id")
                             .long_("relation-id")
                             .short_('r')
                             .action(arg_action::set)
                             .requires_("remote_unit_name"))
                .arg(arg_builder("remote_unit_name")
                             .long_("remote-unit")
                             .short_('u')
                             .action(arg_action::set)
                             .requires_("relation_id"));
        return app.freeze();
    }
    constexpr command_spec mutual_requires = make_mutual_requires();

    consteval command_spec make_short_require_equals_min_zero() {
        command_builder app("foo");
        std::move(app)
                .arg(arg_builder("check")
                             .short_('c')
                             .num_args(value_range::at_least(0))
                             .require_equals())
                .arg(arg_builder("unique").short_('u').action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec short_require_equals_min_zero = make_short_require_equals_min_zero();

    // clap's `positional_required_with_requires_if_value`: `requires_if` on a POSITIONAL,
    // so the condition is met by the positional's own value rather than by a flag being
    // present. `clap-test val` supplies `<flag>`, which fires the rule for `<opt>`, and the
    // error then has to list two missing arguments at once.
    consteval command_spec make_positional_requires_if_with_foo() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("flag").index(1).required().requires_if("val", "opt"))
                .arg(arg_builder("foo").index(2).required())
                .arg(arg_builder("opt").index(3))
                .arg(arg_builder("bar").index(4));
        return app.freeze();
    }
    constexpr command_spec positional_requires_if_with_foo = make_positional_requires_if_with_foo();

    consteval command_spec make_unless_all_with_any() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("foo").long_("foo").action(arg_action::set_true))
                .arg(arg_builder("bar").long_("bar").action(arg_action::set_true))
                .arg(arg_builder("baz").long_("baz").action(arg_action::set_true))
                .arg(arg_builder("flag")
                             .long_("flag")
                             .action(arg_action::set_true)
                             .required_unless_present_any({"foo"})
                             .required_unless_present_all({"bar", "baz"}));
        return app.freeze();
    }
    constexpr command_spec unless_all_with_any = make_unless_all_with_any();

    // The six "a default satisfies nothing and triggers nothing" fixtures.
    consteval command_spec make_requires_with_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default").requires_("flag"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec requires_with_default = make_requires_with_default();

    consteval command_spec make_requires_if_with_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default").requires_if("default",
                                                                                          "flag"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec requires_if_with_default = make_requires_if_with_default();

    consteval command_spec make_group_requires_with_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default"))
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .group(group_builder("one").arg("opt").requires_("flag"));
        return app.freeze();
    }
    constexpr command_spec group_requires_with_default = make_group_requires_with_default();

    consteval command_spec make_required_if_eq_on_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default"))
                .arg(arg_builder("flag")
                             .long_("flag")
                             .action(arg_action::set_true)
                             .required_if_eq("opt", "default"));
        return app.freeze();
    }
    constexpr command_spec required_if_eq_on_default = make_required_if_eq_on_default();

    consteval command_spec make_required_if_eq_all_on_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default"))
                .arg(arg_builder("flag")
                             .long_("flag")
                             .action(arg_action::set_true)
                             .required_if_eq_all({{"opt", "default"}}));
        return app.freeze();
    }
    constexpr command_spec required_if_eq_all_on_default = make_required_if_eq_all_on_default();

    consteval command_spec make_required_unless_on_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default"))
                .arg(arg_builder("flag").long_("flag").required_unless_present("opt"));
        return app.freeze();
    }
    constexpr command_spec required_unless_on_default = make_required_unless_on_default();

    consteval command_spec make_required_unless_all_on_default() {
        command_builder app("prog");
        std::move(app)
                .arg(arg_builder("opt").long_("opt").default_value("default"))
                .arg(arg_builder("flag").long_("flag").required_unless_present_all({"opt"}));
        return app.freeze();
    }
    constexpr command_spec required_unless_all_on_default = make_required_unless_all_on_default();

    consteval command_spec make_no_duplicate_required() {
        command_builder app("clap-test");
        std::move(app)
                .arg(arg_builder("a").index(1).required())
                .arg(arg_builder("b").short_('b').action(arg_action::set).conflicts_with("c"))
                .arg(arg_builder("c").short_('c').action(arg_action::set).conflicts_with("b"));
        return app.freeze();
    }
    constexpr command_spec no_duplicate_required = make_no_duplicate_required();

    consteval command_spec make_require_with_group() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("require-first")
                             .long_("require-first")
                             .action(arg_action::set_true)
                             .requires_("first"))
                .arg(arg_builder("first")
                             .long_("first")
                             .action(arg_action::set_true)
                             .group("either_or_both"))
                .arg(arg_builder("second")
                             .long_("second")
                             .action(arg_action::set_true)
                             .group("either_or_both"))
                .group(group_builder("either_or_both").multiple().required());
        return app.freeze();
    }
    constexpr command_spec require_with_group = make_require_with_group();

    static_assert(flag_requires.find_arg("flag")->get_requires().size() == 1);
    static_assert(unless_any.find_arg("cfg")->get_required_unless_present_any().size() == 2);
    static_assert(unless_all.find_arg("cfg")->get_required_unless_present_all().size() == 2);
    static_assert(required_if_eq_all.find_arg("cfg")->get_required_if_eq_all().size() == 2);
    static_assert(required_if_eq_ignore_case.find_arg("extra")->is_ignore_case_set());

}  // namespace

// ---------------------------------------------------------------------------
// requires
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::flag_required") {
    const outcome got = clapp::parse(flag_requires, raw_args{"", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::flag_required_2") {
    const outcome got = clapp::parse(flag_requires, raw_args{"", "-f", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("require.rs::option_required") {
    const outcome got = clapp::parse(option_requires, raw_args{"", "-f", "val"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::option_required_2") {
    const outcome got = clapp::parse(option_requires, raw_args{"", "-f", "val", "-c", "other_val"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "c") == std::optional<std::string>{"other_val"});
    CLAPP_CHECK(one_string(*got, "f") == std::optional<std::string>{"val"});
}

CLAPP_TEST("require.rs::positional_required") {
    const outcome got = clapp::parse(required_positional, raw_args{""});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::positional_required_2") {
    const outcome got = clapp::parse(required_positional, raw_args{"", "someval"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"someval"});
}

CLAPP_TEST("require.rs::positional_required_with_requires") {
    // BOTH the required positional and the one it pulls in are listed, and the usage
    // line brackets them accordingly.
    const outcome got = clapp::parse(positional_requires, raw_args{"clap-test"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the following required arguments were not provided:"));
    CLAPP_CHECK(says(got, "<flag>"));
    CLAPP_CHECK(says(got, "<opt>"));
    CLAPP_CHECK(says(got, "Usage: clap-test <flag> <opt> [bar]"));
}

CLAPP_TEST("require.rs::positional_required_with_requires_if_no_value") {
    // The conditional requirement did not fire, so `opt` stays optional everywhere.
    const outcome got = clapp::parse(positional_requires_if, raw_args{"clap-test"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "<flag>"));
    CLAPP_CHECK(says(got, "Usage: clap-test <flag> [opt] [bar]"));
}

// ---------------------------------------------------------------------------
// Groups as requirements
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::group_required") {
    const outcome got = clapp::parse(required_group, raw_args{"", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::group_required_2") {
    const outcome got = clapp::parse(required_group, raw_args{"", "-f", "--some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("some"));
    CLAPP_CHECK(!got->get_flag("other"));
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("require.rs::group_required_3") {
    const outcome got = clapp::parse(required_group, raw_args{"", "-f", "--other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->get_flag("some"));
    CLAPP_CHECK(got->get_flag("other"));
}

CLAPP_TEST("require.rs::arg_require_group") {
    const outcome got = clapp::parse(arg_requires_group, raw_args{"", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::arg_require_group_2") {
    const outcome got = clapp::parse(arg_requires_group, raw_args{"", "-f", "--some"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("some"));
    CLAPP_CHECK(!got->get_flag("other"));
}

CLAPP_TEST("require.rs::arg_require_group_3") {
    const outcome got = clapp::parse(arg_requires_group, raw_args{"", "-f", "--other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->get_flag("some"));
    CLAPP_CHECK(got->get_flag("other"));
}

// ---------------------------------------------------------------------------
// required_unless_present, _any, _all
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::issue_753") {
    // Three arguments all excused by the same `--list`.
    const outcome got = clapp::parse(issue_753, raw_args{"test", "--list"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_unless_present") {
    const outcome got = clapp::parse(unless_present, raw_args{"unlesstest", "--debug"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("dbg"));
    CLAPP_CHECK(!got->contains_id("cfg"));
}

CLAPP_TEST("require.rs::required_unless_present_err") {
    const outcome got = clapp::parse(unless_present, raw_args{"unlesstest"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_unless_present_with_optional_value") {
    // `--opt` is not `--debug`; supplying an unrelated argument excuses nothing.
    const outcome got = clapp::parse(unless_present_with_optional, raw_args{"unlesstest", "--opt"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_unless_present_all") {
    const outcome got = clapp::parse(unless_all, raw_args{"unlessall", "--debug", "-i", "file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("dbg"));
    CLAPP_CHECK(got->contains_id("infile"));
    CLAPP_CHECK(!got->contains_id("cfg"));
}

CLAPP_TEST("require.rs::required_unless_all_err") {
    // ONE of the two excuses. `_all` needs both; an implementation that reads it as
    // `_any` accepts this and passes every other case in this file.
    const outcome got = clapp::parse(unless_all, raw_args{"unlessall", "--debug"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_unless_present_any") {
    const outcome got = clapp::parse(unless_any, raw_args{"unlessone", "--debug"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("dbg"));
    CLAPP_CHECK(!got->contains_id("cfg"));
}

CLAPP_TEST("require.rs::required_unless_any_2") {
    // The SECOND name in the list, not the first.
    const outcome got = clapp::parse(unless_any, raw_args{"unlessone", "-i", "file"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("infile"));
    CLAPP_CHECK(!got->contains_id("cfg"));
}

CLAPP_TEST("require.rs::required_unless_any_1") {
    const outcome got = clapp::parse(unless_any, raw_args{"unlessone", "--debug"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("infile"));
    CLAPP_CHECK(!got->contains_id("cfg"));
    CLAPP_CHECK(got->get_flag("dbg"));
}

CLAPP_TEST("require.rs::required_unless_any_err") {
    const outcome got = clapp::parse(unless_any, raw_args{"unlessone"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_unless_any_works_with_short") {
    const outcome got = clapp::parse(unless_any_short, raw_args{"unlessone", "-a"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_unless_any_works_with_short_err") {
    const outcome got = clapp::parse(unless_any_short, raw_args{"unlessone"});
    CLAPP_CHECK(!got.has_value());
}

CLAPP_TEST("require.rs::required_unless_any_works_without") {
    const outcome got = clapp::parse(unless_any_positional, raw_args{"unlessone", "-a"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_unless_any_works_with_long") {
    const outcome got = clapp::parse(unless_any_long, raw_args{"unlessone", "-a"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_unless_all_with_any") {
    // `_any(["foo"])` OR `_all(["bar","baz"])`. Neither, one, both, and half.
    CLAPP_CHECK(!clapp::parse(unless_all_with_any, raw_args{"myprog"}).has_value());

    const outcome with_foo = clapp::parse(unless_all_with_any, raw_args{"myprog", "--foo"});
    CLAPP_CHECK(with_foo.has_value());
    CLAPP_CHECK(!with_foo->get_flag("flag"));

    const outcome with_both =
            clapp::parse(unless_all_with_any, raw_args{"myprog", "--bar", "--baz"});
    CLAPP_CHECK(with_both.has_value());
    CLAPP_CHECK(!with_both->get_flag("flag"));

    CLAPP_CHECK(!clapp::parse(unless_all_with_any, raw_args{"myprog", "--bar"}).has_value());
}

// ---------------------------------------------------------------------------
// requires_if
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::requires_if_present_val") {
    const outcome got = clapp::parse(requires_if_one, raw_args{"unlessone", "--config=my.cfg"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::requires_if_present_mult") {
    const outcome got = clapp::parse(requires_if_many, raw_args{"unlessone", "--config=other.cfg"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::requires_if_present_mult_pass") {
    // A value that matches NEITHER rule.
    const outcome got = clapp::parse(requires_if_many, raw_args{"unlessone", "--config=some.cfg"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::requires_if_present_val_no_present_pass") {
    const outcome got = clapp::parse(requires_if_one, raw_args{"unlessone"});
    CLAPP_CHECK(got.has_value());
}

// ---------------------------------------------------------------------------
// required_if_eq, _any, _all
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::required_if_val_present_pass") {
    const outcome got =
            clapp::parse(required_if_eq, raw_args{"ri", "--extra", "val", "--config", "my.cfg"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_if_val_present_fail") {
    const outcome got = clapp::parse(required_if_eq, raw_args{"ri", "--extra", "val"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_if_wrong_val") {
    const outcome got = clapp::parse(required_if_eq, raw_args{"ri", "--extra", "other"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_if_val_present_ignore_case_pass") {
    // `ignore_case` on the WATCHED argument must reach the predicate: "vaL" must satisfy
    // the rule written as "Val".
    const outcome got = clapp::parse(required_if_eq_ignore_case,
                                     raw_args{"ri", "--extra", "vaL", "--config", "my.cfg"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_if_val_present_ignore_case_fail") {
    const outcome got = clapp::parse(required_if_eq_ignore_case, raw_args{"ri", "--extra", "vaL"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_if_all_values_present_pass") {
    const outcome got = clapp::parse(
            required_if_eq_all,
            raw_args{"ri", "--extra", "val", "--option", "spec", "--config", "my.cfg"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_if_some_values_present_pass") {
    // Only one of the two conditions holds, so `_all` does not fire.
    const outcome got = clapp::parse(required_if_eq_all, raw_args{"ri", "--extra", "val"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_if_all_values_present_fail") {
    const outcome got =
            clapp::parse(required_if_eq_all, raw_args{"ri", "--extra", "val", "--option", "spec"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_if_any_all_values_present_pass") {
    const outcome got = clapp::parse(
            required_if_eq_all_and_any,
            raw_args{"ri", "--extra", "val", "--option", "spec", "--config", "my.cfg"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_if_any_all_values_present_fail") {
    const outcome got = clapp::parse(required_if_eq_all_and_any,
                                     raw_args{"ri", "--extra", "val", "--option", "spec"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_ifs_val_present_pass") {
    const outcome got = clapp::parse(required_if_eq_any,
                                     raw_args{"ri", "--option", "spec", "--config", "my.cfg"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_ifs_val_present_fail") {
    const outcome got = clapp::parse(required_if_eq_any, raw_args{"ri", "--option", "spec"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_ifs_wrong_val") {
    const outcome got = clapp::parse(required_if_eq_any, raw_args{"ri", "--option", "other"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_ifs_wrong_val_mult_fail") {
    // One rule misses and the other matches: `_any` fires.
    const outcome got = clapp::parse(required_if_eq_any,
                                     raw_args{"ri", "--extra", "other", "--option", "spec"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_if_val_present_fail_error_output") {
    const outcome got = clapp::parse(three_required,
                                     raw_args{"test", "--input", "somepath", "--target", "file"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "--output <output>"));
    CLAPP_CHECK(says(got, "Usage: test --target <target> --input <input> --output <output>"));
}

// ---------------------------------------------------------------------------
// The shape of the message
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::require_eq") {
    // `require_equals` changes the SPELLING in the required list, not only in the parser.
    const outcome got = clapp::parse(require_eq, raw_args{"clap-test"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "--opt=<FILE>"));
    CLAPP_CHECK(says(got, "Usage: clap-test --opt=<FILE>"));
}

CLAPP_TEST("require.rs::require_eq_filtered") {
    // `-f=blah` is satisfied, so it is dropped from the LIST and kept in the USAGE.
    const outcome got = clapp::parse(require_eq_filtered, raw_args{"clap-test", "-f=blah"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "Usage: clap-test --opt=<FILE> --foo=<FILE>"));
    CLAPP_CHECK(says(got, "the following required arguments were not provided:"));
}

CLAPP_TEST("require.rs::require_eq_filtered_group") {
    const outcome got =
            clapp::parse(require_eq_filtered_group, raw_args{"clap-test", "-f=blah", "--g1=blah"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "--opt=<FILE>"));
    CLAPP_CHECK(says(got, "Usage: clap-test --opt=<FILE> --foo=<FILE> <--g1=<FILE>|--g2=<FILE>>"));
}

CLAPP_TEST("require.rs::multiple_required_unless_usage_printing") {
    const outcome got = clapp::parse(multiple_unless, raw_args{"test", "--c", "asd"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "--a <a>"));
    CLAPP_CHECK(says(got, "--b <b>"));
    CLAPP_CHECK(says(got, "Usage: test --c <c> --a <a> --b <b>"));
}

CLAPP_TEST("require.rs::issue_1158_conflicting_requirements") {
    const outcome got = clapp::parse(issue_1158, raw_args{"example", "id"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "-x <X>"));
    CLAPP_CHECK(says(got, "-y <Y>"));
    CLAPP_CHECK(says(got, "-z <Z>"));
    CLAPP_CHECK(says(got, "Usage: example -x <X> -y <Y> -z <Z> <ID>"));
}

CLAPP_TEST("require.rs::issue_1158_conflicting_requirements_rev") {
    const outcome got = clapp::parse(issue_1158, raw_args{"", "--config", "some.conf"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::required_error_doesnt_duplicate") {
    // A conflict wins over the missing positional, and the usage line names each
    // argument once.
    const outcome got = clapp::parse(no_duplicate_required,
                                     raw_args{"clap-test", "aaa", "-b", "bbb", "-c", "ccc"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "the argument '-b <b>' cannot be used with '-c <c>'"));
    CLAPP_CHECK(says(got, "Usage: clap-test -b <b> <a>"));
}

CLAPP_TEST("require.rs::required_require_with_group_shows_flag") {
    // The required list names `--first` (the specific argument that `--require-first`
    // demands), while the usage line names the whole group.
    const outcome got =
            clapp::parse(require_with_group, raw_args{"test", "--require-first", "--second"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(says(got, "--first"));
    CLAPP_CHECK(says(got, "Usage: test --require-first <--first|--second>"));
}

// ---------------------------------------------------------------------------
// Mutual and self-referential requirements
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::issue_1643_args_mutually_require_each_other") {
    const outcome got =
            clapp::parse(mutual_requires, raw_args{"test", "-u", "hello", "-r", "farewell"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("require.rs::short_flag_require_equals_with_minvals_zero") {
    // `-cu` is a cluster: `-c` takes no value (it needs an `=`), so `u` is the next flag.
    const outcome got = clapp::parse(short_require_equals_min_zero, raw_args{"foo", "-cu"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("check"));
    CLAPP_CHECK(got->get_flag("unique"));
}

// ---------------------------------------------------------------------------
// A default satisfies nothing and triggers nothing
// ---------------------------------------------------------------------------

CLAPP_TEST("require.rs::requires_with_default_value") {
    const outcome got = clapp::parse(requires_with_default, raw_args{"myprog"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("require.rs::requires_if_with_default_value") {
    const outcome got = clapp::parse(requires_if_with_default, raw_args{"myprog"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("require.rs::group_requires_with_default_value") {
    const outcome got = clapp::parse(group_requires_with_default, raw_args{"myprog"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("require.rs::required_if_eq_on_default_value") {
    const outcome got = clapp::parse(required_if_eq_on_default, raw_args{"myprog"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("require.rs::required_if_eq_all_on_default_value") {
    const outcome got = clapp::parse(required_if_eq_all_on_default, raw_args{"myprog"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"default"});
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("require.rs::required_unless_on_default_value") {
    // The OTHER direction: a default does NOT excuse a `required_unless_present`, because
    // that rule asks what the user supplied. Same fixture shape as the two above, and the
    // opposite outcome — which is exactly why one uniform "skip defaults" rule is wrong.
    const outcome got = clapp::parse(required_unless_on_default, raw_args{"myprog"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::required_unless_all_on_default_value") {
    const outcome got = clapp::parse(required_unless_all_on_default, raw_args{"myprog"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("require.rs::positional_required_with_requires_if_value") {
    // Two missing arguments, one indented line each, in DECLARATION order — and `<opt>`
    // is on the list only because `<flag>` happened to be spelled `val`. The usage line
    // then shows all four positionals, `[bar]` included, even though nothing requires it.
    const outcome got = clapp::parse(positional_requires_if_with_foo, raw_args{"clap-test", "val"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
    CLAPP_CHECK(same_block(got,
                           "error: the following required arguments were not provided:\n"
                           "  <foo>\n"
                           "  <opt>\n"
                           "\n"
                           "Usage: clap-test <flag> <foo> <opt> [bar]\n"
                           "\n"
                           "For more information, try '--help'.\n"));
}

CLAPP_TEST("require.rs::issue_2624") {
    // clap's `issue_2624` is `short_flag_require_equals_with_minvals_zero` plus
    // `.value_parser(["silent", "quiet", "diagnose-first"])` on `-c`. In clapp a value
    // domain belongs to the TYPE rather than to the argument, so the domain is not
    // expressible on this fixture and is pinned instead by
    // conformance_possible_values_test.cpp. What issue #2624 is actually ABOUT — that
    // `-cu` splits into `-c` (which takes nothing without an `=`) and `-u` — is the same
    // line as the case above, and is asserted here under its own name so a name diff
    // against clap's file does not report it missing.
    const outcome got = clapp::parse(short_require_equals_min_zero, raw_args{"foo", "-cu"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("check"));
    CLAPP_CHECK(got->get_flag("unique"));
}
