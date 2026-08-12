#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/meta/deduce.hpp>
#include <clapp/meta/from_matches.hpp>
#include <clapp/meta/parse.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if __has_include(<unistd.h>) && __has_include(<sys/wait.h>)
#    include <sys/wait.h>
#    include <unistd.h>
#endif

// Most fixture members below are consumed through C++ reflection, which CLion's
// unused-member inspection cannot currently see. The intentionally invalid fixtures and
// the test-registration macro also exercise patterns that clang-tidy rejects outside a
// test translation unit.
// ReSharper disable CppUnusedEnumerator
// ReSharper disable CppUnusedStructMember
// NOLINTBEGIN

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::count_type;
    using clapp::deduced_arg;
    using clapp::error;
    using clapp::error_kind;
    using clapp::field_shape;
    using clapp::naming;
    using clapp::os_str;
    using clapp::raw_args;
    using clapp::value_range;

    using clapp::meta::arg_id_of;
    using clapp::meta::rename_all_of;
    using clapp::meta::subcommand_name_of;

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    /**
     * Deliberately not `color_choice`: clapp::color_choice already exists in
     * builder/command.hpp, and shadowing it would make the enum row look like it was
     * testing clapp's own type.
     */
    enum class hue : unsigned char { always, auto_, never };

    /**
     * Every argument-producing deduction rule in one struct, plus the three name-derivation
     * cases that a struct of single-word fields
     * cannot tell apart: `no_color` (long spelling differs from the id), `type_` (the
     * keyword-avoidance suffix must be stripped) and `outputPath` (camelCase normalizes to
     * kebab).
     */
    struct[[= clapp::cmd{.version = "1.2.3", .about = "wide demo"}]] wide_cli {
        bool verbose;            /**< flag */
        bool no_color;           /**< flag; id `no_color`, long `--no-color` */
        std::optional<bool> tty; /**< optional flag: absent / true / explicitly false */

        [[= clapp::arg{.short_ = 'd',
                       .act    = clapp::action::count}]] count_type debug; /**< counter */

        std::string name;     /**< scalar, required */
        unsigned port = 8080; /**< scalar with a member initializer, not required */

        [[= clapp::arg{.default_value = "info"}]] std::string
                severity; /**< scalar with an annotation default, not required */

        int type_;                                       /**< id strips the `_` */
        std::optional<std::string> config;               /**< optional scalar */
        std::optional<std::optional<int>> level;         /**< three-state scalar */
        std::vector<std::string> include;                /**< sequence */
        std::optional<std::vector<std::string>> exclude; /**< optional sequence */
        std::array<int, 3> origin;                       /**< fixed arity */
        std::pair<std::string, int> bind;                /**< two different element types */
        hue color;                                       /**< enumeration */
        std::string outputPath;                          /**< camelCase -> `--output-path` */

        [[= clapp::arg{
                .auto_short =
                        true}]] std::string kind; /**< `-k` from the long name's first letter */

        [[= clapp::skip{}]] std::string note{"untouched"}; /**< never parsed */
    };

    /** Flattened into whatever command embeds it. */
    struct shared_opts {
        std::optional<std::string> workdir;
        unsigned jobs = 4;
    };

    struct[[= clapp::cmd{.name = "add", .about = "Add files"}]] cmd_add {
        std::string message;
        bool force;
        std::optional<std::string> author;
    };

    /** No `[[= clapp::cmd]]`: the name comes from the type, `cmd_remove` -> `remove`. */
    struct cmd_remove {
        std::vector<std::string> paths;
        /** Declared by the ROOT, not here; clapp::parse() propagates it down. */
        [[= clapp::from_global{}]] bool verbose;
    };

    struct[[= clapp::cmd{.version = "9.9"}]] tree_cli {
        [[= clapp::arg{.global = true}]] bool verbose;
        [[= clapp::flatten{}]] shared_opts common;
        [[= clapp::subcommand{}]] std::variant<cmd_add, cmd_remove> command;
    };

    struct optional_tree_cli {
        [[= clapp::arg{.global = true}]] bool verbose;
        [[= clapp::subcommand{}]] std::optional<std::variant<cmd_add, cmd_remove>> command;
    };

    /** The member-initializer rule: all three survive an absent argument. */
    struct defaulted_cli {
        bool verbose    = true;
        unsigned port   = 8080;
        std::string tag = "release";
    };

    /** A command whose `name` is required, so command_for_update() has something to relax. */
    struct update_cli {
        std::string name;
        unsigned port = 80;
        std::vector<std::string> tags;
    };

    /** Relation annotations, which are stackable and therefore have their own fixture. */
    struct relation_cli {
        [[= clapp::conflicts_with{"quiet"}]][[= clapp::conflicts_with{"silent"}]]
                                            [[= clapp::requires_arg{"name"}]] bool verbose;

        bool quiet;
        bool silent;
        std::optional<std::string> name;

        [[= clapp::required_unless_any{"verbose"}]] std::optional<std::string> tag;
    };

    /**
     * Positional, help text, env and hide — the annotation fields that do not change the
     * shape but must still reach the builder.
     */
    struct annotated_cli {
        [[= clapp::arg{.index = 1, .help = "The input file"}]] std::string input;

        [[= clapp::arg{.short_     = 'o',
                       .long_      = "out",
                       .help       = "Where to write",
                       .value_name = "PATH",
                       .env        = "DEMO_OUT"}]] std::optional<std::string>
                output;

        [[= clapp::arg{.short_ = 'q', .no_long = true, .hide = true}]] bool quiet;
    };

    /** Not parsable: `nope` has no clapp::value_parser specialization. */
    struct nope {
        int unused;
    };

    struct bad_cli {
        nope bad_field;
    };

    /** Two fields whose derived long options collide. */
    struct duplicate_long_cli {
        bool verbose;
        [[= clapp::arg{.long_ = "verbose"}]] bool loud;
    };

    /** Two fields whose short options collide. */
    struct duplicate_short_cli {
        [[= clapp::arg{.short_ = 'v'}]] bool verbose;
        [[= clapp::arg{.short_ = 'v'}]] bool version_;
    };

    /** Two alternatives that name themselves the same subcommand. */
    struct[[= clapp::cmd{.name = "same"}]] sub_one {
        bool a;
    };
    struct[[= clapp::cmd{.name = "same"}]] sub_two {
        bool b;
    };
    struct duplicate_subcommand_cli {
        [[= clapp::subcommand{}]] std::variant<sub_one, sub_two> command;
    };

    /**
     * Not an aggregate: a user-provided constructor hides every field from
     * `std::meta::access_context::current()`.
     */
    struct not_an_aggregate {
        not_an_aggregate() : value(0) {}
        int value;
    };

    /**
     * `[[= clapp::flatten{}]]` on something with no fields to splice in.
     *
     * The marker is consulted before the type is, so nothing stops it from landing on an
     * `int`. What the walk does next is the question this fixture pins: it must ANSWER
     * false, without a hard compilation failure. Until 2026-08 it failed inside
     * `std::meta::nonstatic_data_members_of(^^int, ...)` four levels down inside
     * clapp::detail::collect_level(), which made clapp::parsable_command ill-formed for
     * this type rather than false — so the `static_assert` on the next line did not
     * compile, and the header's claim that the concept never hard-errors was untrue for
     * this input. What the user reads instead is
     * tests/units/meta/compile_fail/flatten_on_non_aggregate_test.cpp.
     */
    struct flatten_on_non_aggregate {
        [[= clapp::flatten{}]] int not_a_struct;
    };

    /**
     * A subcommand set reached through `[[= clapp::flatten{}]]` that CROSSES a
     * `rename_all` boundary — the one shape in which the forward and reverse directions
     * can disagree about a subcommand's name while every other fixture stays green.
     *
     * A flatten does not open a new command level, so the enclosing command's
     * clapp::naming stays in force inside the flattened type. `command_of<T>()` has always
     * done that (clapp::detail::derive_args_into() forwards its `style` argument);
     * `from_matches<T>()` used to RECOMPUTE `rename_all_of(^^T)` at each level instead,
     * which for this fixture means the forward direction freezes `do_thing` and the
     * reverse direction looks up `do-thing`. The result was a command line the parser
     * ACCEPTS and the store-back then rejects with
     * clapp::error_kind::invalid_subcommand — the error whose \note in from_matches.hpp
     * says it is reachable only when the two directions have drifted.
     *
     * Nothing about this fixture is exotic except that it combines three things no other
     * fixture combines: a flatten, a non-default `rename_all`, and a subcommand set.
     */
    struct cmd_do_thing {
        [[= clapp::arg{.long_ = "who"}]] std::string who;
    };

    struct cmd_undo_thing {
        [[= clapp::arg{.long_ = "why"}]] std::optional<std::string> why;
    };

    /** Carries no `rename_all` of its own, so it must inherit the root's. */
    struct flat_subs {
        [[= clapp::subcommand{}]] std::variant<cmd_do_thing, cmd_undo_thing> command;
    };

    struct[[= clapp::cmd{.name = "snakeroot", .rename_all = naming::snake}]] snake_root_cli {
        [[= clapp::arg{.long_ = "loud"}]] bool loud;
        [[= clapp::flatten{}]] flat_subs nested;
    };

    /**
     * The mirror: the ROOT is default-named and the FLATTENED struct carries the
     * `rename_all`. A flatten is not a command, so `.rename_all` on `flat_snake_subs` must
     * have NO effect — the root's kebab wins at both ends. An implementation that
     * recomputes per level gets this one wrong in the opposite direction from the case
     * above, which is why both are here.
     */
    struct[[= clapp::cmd{.rename_all = naming::snake}]] flat_snake_subs {
        [[= clapp::subcommand{}]] std::variant<cmd_do_thing, cmd_undo_thing> command;
    };

    struct[[= clapp::cmd{.name = "kebabroot"}]] kebab_root_cli {
        [[= clapp::flatten{}]] flat_snake_subs nested;
    };

    /**
     * Both default channels on one field. clapp can state a default twice — as a member
     * initializer and as `.default_value` — where clap has only the one; the annotation is
     * the only one of the two that reaches the clapp::command_spec, so it is the one that
     * must win. See the \warning on clapp::arg_attr::default_value.
     */
    struct[[= clapp::cmd{.name = "twodefaults"}]] two_defaults_cli {
        [[= clapp::arg{.long_ = "port", .default_value = "5"}]] unsigned port = 8080;

        /**
         * No `.default_value`: the initializer is the only default, so the `"false"` that
         * clapp::arg_action::set_true injects for an absent flag must NOT overwrite it.
         */
        [[= clapp::arg{.long_ = "verbose"}]] bool verbose = true;
    };

    // ---------------------------------------------------------------------------
    // Frozen trees
    // ---------------------------------------------------------------------------

    constexpr command_spec wide_spec          = clapp::command_of<wide_cli>();
    constexpr command_spec tree_spec          = clapp::command_of<tree_cli>();
    constexpr command_spec optional_tree_spec = clapp::command_of<optional_tree_cli>();
    constexpr command_spec defaulted_spec     = clapp::command_of<defaulted_cli>();
    constexpr command_spec relation_spec      = clapp::command_of<relation_cli>();
    constexpr command_spec annotated_spec     = clapp::command_of<annotated_cli>();
    constexpr command_spec update_parse_spec  = clapp::command_of<update_cli>();
    constexpr command_spec update_relax_spec  = clapp::command_for_update<update_cli>();
    constexpr command_spec snake_root_spec    = clapp::command_of<snake_root_cli>();
    constexpr command_spec kebab_root_spec    = clapp::command_of<kebab_root_cli>();
    constexpr command_spec two_defaults_spec  = clapp::command_of<two_defaults_cli>();

    // The forward direction's answer, spelled out. Without these the round trips below
    // would still pass if BOTH directions moved to the same wrong name.
    static_assert(snake_root_spec.has_subcommand("do_thing"));
    static_assert(snake_root_spec.has_subcommand("undo_thing"));
    static_assert(!snake_root_spec.has_subcommand("do-thing"));

    // The mirror: `.rename_all` on a FLATTENED struct is inert, because a flatten is not a
    // command level. The root's default kebab reaches all the way down.
    static_assert(kebab_root_spec.has_subcommand("do-thing"));
    static_assert(!kebab_root_spec.has_subcommand("do_thing"));

    // ===========================================================================
    // Compile-time: the name collision
    // ===========================================================================

    // The whole point of the resolution: both spellings live in one translation unit and
    // neither is ambiguous. `decltype` is unevaluated, so this tests overload resolution
    // alone — no parse runs, and no command line is needed.
    static_assert(
            std::is_same_v<decltype(clapp::parse<defaulted_cli>(
                                   0, static_cast<const clapp::native_char* const*>(nullptr))),
                           defaulted_cli>,
            "clapp: parse<T>(argc, argv) must select the derive-layer template.");

    static_assert(std::is_same_v<decltype(clapp::parse(std::declval<const command_spec&>(),
                                                       std::declval<const raw_args&>())),
                                 std::expected<arg_matches, error>>,
                  "clapp: parse(spec, raw) must still select the M3 overload.");

    // A plain `char**` from `int main(int, char**)` must bind to the parameter without a
    // cast, or the spelling in every doc example does not compile. The parameter is spelled
    // `const native_char* const*` so that it is also correct where `native_char` is not
    // `char`; on this platform the qualification conversion does the rest.
    static_assert(
            std::is_same_v<decltype(clapp::parse<defaulted_cli>(0, static_cast<char**>(nullptr))),
                           defaulted_cli>);
    static_assert(std::is_same_v<
                  decltype(clapp::try_parse<defaulted_cli>(0, static_cast<char**>(nullptr))),
                  std::expected<defaulted_cli, error>>);

    // ===========================================================================
    // Compile-time: the concept, in both directions
    // ===========================================================================

    static_assert(clapp::parsable_command<wide_cli>);
    static_assert(clapp::parsable_command<tree_cli>);
    static_assert(clapp::parsable_command<optional_tree_cli>);
    static_assert(clapp::parsable_command<relation_cli>);
    static_assert(clapp::parsable_command<annotated_cli>);

    // The negative half. Without these, a concept that answered `true` unconditionally
    // would satisfy every assertion above.
    static_assert(!clapp::parsable_command<bad_cli>);
    static_assert(!clapp::parsable_command<duplicate_long_cli>);
    static_assert(!clapp::parsable_command<duplicate_short_cli>);
    static_assert(!clapp::parsable_command<duplicate_subcommand_cli>);
    static_assert(!clapp::parsable_command<not_an_aggregate>);
    static_assert(!clapp::parsable_command<flatten_on_non_aggregate>);
    static_assert(!clapp::parsable_command<int>);
    static_assert(!clapp::parsable_command<std::string>);

    // Both atoms, separately: a flatten marker on an `int` must leave every walk answering
    // rather than hard-failing. `names_are_unique` used to fail this way, and concept
    // conjunction short-circuits, so it would never be reached through the concept alone.
    static_assert(!clapp::detail::all_fields_parsable<flatten_on_non_aggregate>());
    static_assert(clapp::detail::names_are_unique<flatten_on_non_aggregate>());

    // The atoms, so a failure says which rule broke rather than only that one did.
    static_assert(clapp::derivable_command<bad_cli>);
    static_assert(!clapp::detail::all_fields_parsable<bad_cli>());
    static_assert(clapp::detail::names_are_unique<bad_cli>());

    static_assert(clapp::detail::all_fields_parsable<duplicate_long_cli>());
    static_assert(!clapp::detail::names_are_unique<duplicate_long_cli>());

    // A collision inside a SUBCOMMAND must fail the root's concept too, or a bad subcommand
    // reaches freeze() and reports from a place the user did not write.
    struct bad_sub {
        bool verbose;
        [[= clapp::arg{.long_ = "verbose"}]] bool loud;
    };
    struct root_with_bad_sub {
        [[= clapp::subcommand{}]] std::variant<bad_sub> command;
    };
    static_assert(clapp::detail::level_is_unique<root_with_bad_sub>());
    static_assert(!clapp::detail::subcommand_levels_are_unique<root_with_bad_sub>());
    static_assert(!clapp::parsable_command<root_with_bad_sub>);

    // ===========================================================================
    // Compile-time: the diagnostic message
    // ===========================================================================

    /**
     * Build the exact string the failing `static_assert` inside
     * clapp::detail::check_fields_parsable() would carry for `bad_cli::bad_field`.
     *
     * This is the only way to test a diagnostic that, by construction, cannot be compiled:
     * it uses the same reflection the header uses and the same message builder, so a
     * message that stopped naming the field would fail here rather than in a compile-fail
     * fixture nobody runs.
     */
    consteval std::string_view message_for_bad_field() {
        constexpr std::meta::info member = std::meta::nonstatic_data_members_of(
                ^^bad_cli, std::meta::access_context::current())[0];
        constexpr std::string_view field = std::meta::identifier_of(member);
        constexpr std::string_view type  = std::meta::display_string_of(std::meta::type_of(member));
        return clapp::detail::no_value_parser_message(field, type);
    }

    static_assert(clapp::detail::message_mentions(message_for_bad_field(), "field 'bad_field'"),
                  "clapp: the no-value_parser diagnostic must name the field.");
    // The *type* is quoted from std::meta::display_string_of, which is implementation-defined
    // (CLAUDE.md trap 11), so the assertion is on the identifier appearing at all rather than
    // on one exact spelling.
    static_assert(clapp::detail::message_mentions(message_for_bad_field(), "nope"),
                  "clapp: the no-value_parser diagnostic must name the type.");
    static_assert(clapp::detail::message_mentions(message_for_bad_field(), "clapp::value_parser<>"),
                  "clapp: the no-value_parser diagnostic must name the fix.");
    static_assert(clapp::detail::message_mentions(message_for_bad_field(), "[[= clapp::skip{}]]"),
                  "clapp: the no-value_parser diagnostic must name the escape hatch.");

    // Use named objects because GCC 16.1.0 miscompiles this constexpr call when its
    // arguments are string literals.
    inline constexpr std::string_view sample_field = "bad_field";
    inline constexpr std::string_view sample_type  = "nope";
    static_assert(clapp::detail::no_value_parser_message(sample_field, sample_type) ==
                  "clapp: field 'bad_field' of type 'nope' has no value_parser. "
                  "Specialize clapp::value_parser<> for it, or mark the field "
                  "[[= clapp::skip{}]].");

    // The duplicate report has to name the level it happened on and the spelling that
    // repeated, or a three-level tree gives the reader nothing to grep for.
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_long_cli>(), "'verbose'"));
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_long_cli>(), "long option"));
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_short_cli>(), "short option"));
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_subcommand_cli>(), "subcommand"));
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_subcommand_cli>(), "'same'"));

    // ...and it has to name BOTH owners, which is the half the spelling cannot supply.
    // `duplicate_long_cli::verbose` derives `--verbose` from its own name while `loud`
    // claims it explicitly, so a reader who greps the struct for the reported spelling finds
    // `verbose` and never `loud`. ADR-0008's standard is the field; these assertions and
    // tests/units/meta/compile_fail/duplicate_long_option_test.cpp are the two halves of
    // enforcing it — this one on the message builder, that one on what the compiler prints.
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_long_cli>(), "'verbose' and 'loud'"));
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_short_cli>(),
            "'verbose' and 'version_'"));
    static_assert(clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_subcommand_cli>(),
            "'sub_one' and 'sub_two'"));

    // The report must be able to say something ELSE, or the three assertions above pass for
    // a function that returns one constant string.
    static_assert(!clapp::detail::message_mentions(
            clapp::detail::duplicate_name_report<duplicate_short_cli>(), "'verbose' and 'loud'"));

    // ===========================================================================
    // Compile-time: the deduction table, as the frozen tree encodes it
    // ===========================================================================

    // Names. The id is the FIELD NAME; the long option is the renamed one. Inverting either
    // of the first two leaves the whole file linking and every value silently vanishing.
    static_assert(wide_spec.has_arg("no_color"));
    static_assert(!wide_spec.has_arg("no-color"));
    static_assert(wide_spec.find_arg("no_color")->get_long() == "no-color");
    static_assert(wide_spec.has_arg("type"));  // `type_` -> `type`, keyword avoidance stripped
    static_assert(wide_spec.find_arg("type")->get_long() == "type");
    static_assert(wide_spec.find_arg("outputPath")->get_long() == "output-path");  // camelCase
    static_assert(wide_spec.find_arg("kind")->get_short() == 'k');                 // auto_short
    static_assert(wide_spec.find_arg("debug")->get_short() == 'd');

    // bool -> set_true / 0 / not required
    static_assert(wide_spec.find_arg("verbose")->get_action() == arg_action::set_true);
    static_assert(wide_spec.find_arg("verbose")->get_num_args() == value_range::empty());
    static_assert(!wide_spec.find_arg("verbose")->is_required_set());
    // std::optional<bool> -> the same shape, different store-back
    static_assert(wide_spec.find_arg("tty")->get_action() == arg_action::set_true);
    static_assert(wide_spec.find_arg("tty")->get_num_args() == value_range::empty());
    // integer + .act = count
    static_assert(wide_spec.find_arg("debug")->get_action() == arg_action::count);
    static_assert(wide_spec.find_arg("debug")->get_num_args() == value_range::empty());
    // scalar -> set / 1 / required
    static_assert(wide_spec.find_arg("name")->get_action() == arg_action::set);
    static_assert(wide_spec.find_arg("name")->get_num_args() == value_range::single());
    static_assert(wide_spec.find_arg("name")->is_required_set());
    // scalar + member initializer -> not required
    static_assert(!wide_spec.find_arg("port")->is_required_set());
    // scalar + .default_value -> not required, and the default reaches the spec
    static_assert(!wide_spec.find_arg("severity")->is_required_set());
    static_assert(wide_spec.find_arg("severity")->get_default_values().size() == 1);
    // std::optional<T> -> set / 1 / not required
    static_assert(!wide_spec.find_arg("config")->is_required_set());
    static_assert(wide_spec.find_arg("config")->get_num_args() == value_range::single());
    // std::optional<std::optional<T>> -> 0..=1
    static_assert(wide_spec.find_arg("level")->get_num_args() == value_range::optional());
    // std::vector<T> -> append / 1..
    static_assert(wide_spec.find_arg("include")->get_action() == arg_action::append);
    static_assert(wide_spec.find_arg("include")->get_num_args() == value_range::at_least(1));
    // std::optional<std::vector<T>> -> the same, still not required
    static_assert(wide_spec.find_arg("exclude")->get_action() == arg_action::append);
    static_assert(!wide_spec.find_arg("exclude")->is_required_set());
    // std::array<T, N> -> exactly N, required
    static_assert(wide_spec.find_arg("origin")->get_num_args() == value_range::exactly(3));
    static_assert(wide_spec.find_arg("origin")->is_required_set());
    // std::pair<A, B> -> exactly 2, required
    static_assert(wide_spec.find_arg("bind")->get_num_args() == value_range::exactly(2));
    static_assert(wide_spec.find_arg("bind")->is_required_set());
    // enum -> set / 1 / required, and its possible values come from enumerators_of with no
    // opt-in. `auto_` loses its keyword-avoidance underscore.
    static_assert(wide_spec.find_arg("color")->get_num_args() == value_range::single());
    static_assert(wide_spec.find_arg("color")->get_possible_values().size() == 3);
    static_assert(wide_spec.find_arg("color")->get_possible_values()[1].get_name() == "auto");
    // [[= clapp::skip{}]] -> no argument at all
    static_assert(!wide_spec.has_arg("note"));

    // Command-level annotation.
    static_assert(wide_spec.get_name() == "wide-cli");
    static_assert(wide_spec.get_version() == "1.2.3");
    static_assert(wide_spec.get_about() == "wide demo");

    // Subcommands: the annotation wins, then the type name with its affixes stripped.
    static_assert(tree_spec.has_subcommand("add"));
    static_assert(tree_spec.has_subcommand("remove"));
    static_assert(subcommand_name_of(^^cmd_add) == "add");
    static_assert(subcommand_name_of(^^cmd_remove) == "remove");
    // A bare std::variant is clap's `.subcommand_required(true).arg_required_else_help(true)`.
    static_assert(tree_spec.is_subcommand_required_set());
    static_assert(tree_spec.is_arg_required_else_help_set());
    // std::optional<std::variant<...>> is neither.
    static_assert(!optional_tree_spec.is_subcommand_required_set());
    static_assert(!optional_tree_spec.is_arg_required_else_help_set());
    // propagate_version is off by default, so a child has its own (absent) version.
    static_assert(tree_spec.find_subcommand("add")->get_about() == "Add files");

    // flatten splices the nested fields onto this level, under this level's naming.
    static_assert(tree_spec.has_arg("workdir"));
    static_assert(tree_spec.has_arg("jobs"));
    static_assert(!tree_spec.has_arg("common"));
    // global() reaches the spec, and propagate_global_args() copies it into the child.
    static_assert(tree_spec.find_arg("verbose")->is_global_set());
    static_assert(tree_spec.find_subcommand("remove")->has_arg("verbose"));
    // [[= clapp::from_global{}]] does NOT declare a second argument in the child.
    static_assert(clapp::meta::has_annotation<clapp::from_global>(
            std::meta::nonstatic_data_members_of(^^cmd_remove,
                                                 std::meta::access_context::current())[1]));

    // Positional, help, env, value_name, no_long, hide.
    static_assert(annotated_spec.find_arg("input")->get_index() == 1);
    static_assert(annotated_spec.find_arg("input")->get_help() == "The input file");
    static_assert(annotated_spec.find_arg("output")->get_long() == "out");
    static_assert(annotated_spec.find_arg("output")->get_short() == 'o');
    static_assert(annotated_spec.find_arg("output")->get_env() == "DEMO_OUT");
    static_assert(!annotated_spec.find_arg("quiet")->get_long().has_value());
    static_assert(annotated_spec.find_arg("quiet")->is_hide_set());

    // Stackable relations.
    static_assert(relation_spec.find_arg("verbose")->get_conflicts().size() == 2);
    static_assert(relation_spec.find_arg("verbose")->get_requires().size() == 1);
    static_assert(relation_spec.find_arg("tag")->get_required_unless_present_any().size() == 1);

    // ===========================================================================
    // Compile-time: command_for_update() is a different tree
    // ===========================================================================

    static_assert(update_parse_spec.find_arg("name")->is_required_set());
    static_assert(!update_relax_spec.find_arg("name")->is_required_set());
    static_assert(update_parse_spec.has_arg("port") && update_relax_spec.has_arg("port"));

    constexpr command_spec tree_update_spec = clapp::command_for_update<tree_cli>();
    static_assert(tree_spec.is_subcommand_required_set());
    static_assert(!tree_update_spec.is_subcommand_required_set());
    static_assert(!tree_update_spec.is_arg_required_else_help_set());
    static_assert(tree_update_spec.has_subcommand("add"));

    // ===========================================================================
    // Compile-time: the id invariant, against the tree command_of() froze
    // ===========================================================================

    /**
     * Walk every member of \p T and assert that the id from_matches() will look up is an id
     * the frozen tree actually declares.
     *
     * The drift detector. It compares two independently produced artefacts — the spec the
     * forward direction froze, and the key the reverse direction computes — so a forward
     * direction that renamed its ids fails here and nowhere else.
     */
    template<class T, naming Style>
    consteval bool ids_agree(const command_spec& spec) {
        bool ok = true;
        template for (constexpr std::meta::info member :
                      std::define_static_array(std::meta::nonstatic_data_members_of(
                              ^^T, std::meta::access_context::current()))) {
            constexpr deduced_arg deduced = clapp::meta::deduce_member(member);
            constexpr std::string_view id = arg_id_of<T, member>();
            constexpr bool inherited      = clapp::meta::has_annotation<clapp::from_global>(member);

            if constexpr (deduced.shape == field_shape::skipped) {
                // A skipped field must NOT be declared; declaring it would make `--note`
                // parse and then be thrown away.
                if (spec.has_arg(id)) ok = false;
            } else if constexpr (deduced.shape == field_shape::flattened) {
                using nested_type = [:std::meta::type_of(member):];
                if (!ids_agree<nested_type, Style>(spec)) ok = false;
            } else if constexpr (deduced.shape == field_shape::subcommand_set ||
                                 deduced.shape == field_shape::optional_subcommand_set) {
                using variant_type = [:clapp::detail::variant_of(member):];
                template for (constexpr std::meta::info alternative :
                              clapp::meta::variant_traits<variant_type>::reflections) {
                    using alternative_type              = [:alternative:];
                    constexpr std::string_view sub_name = subcommand_name_of(alternative, Style);
                    constexpr naming sub_style          = rename_all_of(alternative);
                    if (!spec.has_subcommand(sub_name)) {
                        ok = false;
                    } else if (!ids_agree<alternative_type, sub_style>(
                                       *spec.find_subcommand(sub_name))) {
                        ok = false;
                    }
                }
            } else {
                // `from_global` included: propagate_global_args() must have put it here.
                static_cast<void>(inherited);
                if (!spec.has_arg(id)) ok = false;
            }
        }
        return ok;
    }

    static_assert(ids_agree<wide_cli, rename_all_of(^^wide_cli)>(wide_spec),
                  "clapp: command_of<T>() and from_matches<T>() disagree about an id.");
    static_assert(ids_agree<tree_cli, rename_all_of(^^tree_cli)>(tree_spec));
    static_assert(
            ids_agree<optional_tree_cli, rename_all_of(^^optional_tree_cli)>(optional_tree_spec));
    static_assert(ids_agree<defaulted_cli, rename_all_of(^^defaulted_cli)>(defaulted_spec));
    static_assert(ids_agree<annotated_cli, rename_all_of(^^annotated_cli)>(annotated_spec));
    static_assert(ids_agree<update_cli, rename_all_of(^^update_cli)>(update_relax_spec));
    static_assert(ids_agree<snake_root_cli, rename_all_of(^^snake_root_cli)>(snake_root_spec),
                  "clapp: a subcommand reached through a flatten that crosses a rename_all "
                  "boundary is named differently by the two directions.");
    static_assert(ids_agree<kebab_root_cli, rename_all_of(^^kebab_root_cli)>(kebab_root_spec));
    static_assert(
            ids_agree<two_defaults_cli, rename_all_of(^^two_defaults_cli)>(two_defaults_spec));

    /** The negative half: `ids_agree` must be able to FAIL. */
    consteval bool ids_agree_rejects_a_renamed_tree() {
        command_builder wrong{"wide"};
        // The plausible bug: keying the argument on its long spelling instead of its id.
        std::move(wrong).arg(
                arg_builder("no-color").long_("no-color").action(arg_action::set_true));
        const command_spec frozen = wrong.freeze();
        return !frozen.has_arg("no_color") && frozen.has_arg("no-color");
    }
    static_assert(ids_agree_rejects_a_renamed_tree());

    // ---------------------------------------------------------------------------
    // Runtime helpers
    // ---------------------------------------------------------------------------

    /** The full command line `wide_cli` needs, since eight of its rows are required. */
    const std::vector<os_str>& wide_command_line() {
        static const std::vector<os_str> line{
                "wide-cli",
                "--verbose",
                "--no-color",
                "--tty",
                "-dd",
                "--name",
                "app",
                "--type",
                "7",
                "--config",
                "c.toml",
                "--level",
                "3",
                "--include",
                "a",
                "--include",
                "b",
                "--exclude",
                "x",
                "--origin",
                "1",
                "2",
                "3",
                "--bind",
                "localhost",
                "8080",
                "--color",
                "auto",
                "--output-path",
                "p.txt",
                "-k",
                "release",
        };
        return line;
    }

#if __has_include(<unistd.h>) && __has_include(<sys/wait.h>)
    /** What one forked `clapp::parse_from<T>()` did. */
    struct exit_result {
        int status = -1; /**< The child's exit status, or -1 when it did not exit. */
        std::string out; /**< Everything it wrote to stdout. */
        std::string err; /**< Everything it wrote to stderr. */
    };

    std::string drain(int fd) {
        std::string text;
        char buffer[512];
        for (;;) {
            const ::ssize_t got = ::read(fd, buffer, sizeof buffer);
            if (got <= 0) break;
            text.append(buffer, static_cast<std::size_t>(got));
        }
        ::close(fd);
        return text;
    }

    /**
     * Run `clapp::parse_from<T>(args)` in a child process and report what it did.
     *
     * The exit path is the behaviour being tested — `--help` to stdout with status 0, an
     * error to stderr with status 2 — and `std::exit` cannot be observed in-process.
     *
     * The status is decoded by hand rather than with `WIFEXITED` / `WEXITSTATUS`: those
     * macros expand to C-style casts on this platform, which `-Wold-style-cast -Werror`
     * rejects at the *use* site.
     *
     * Output is read before `waitpid`, and the messages involved are a few hundred bytes,
     * well under a pipe buffer.
     */
    template<class T>
    exit_result run_child(const std::vector<os_str>& args) {
        int out_pipe[2] = {-1, -1};
        int err_pipe[2] = {-1, -1};
        if (::pipe(out_pipe) != 0) return {};
        if (::pipe(err_pipe) != 0) return {};

        // Flush FIRST. The child inherits this process's stdio buffers, and
        // clapp::detail::report_and_exit() calls `std::exit`, which flushes them — so every
        // `ok  <case>` line the harness had buffered would be written a second time, down the
        // pipe, and land in `out`. Measured: without this the "prints an error to stderr"
        // case fails on `out.empty()` with the whole preceding test log in it.
        static_cast<void>(std::fflush(nullptr));

        const ::pid_t child = ::fork();
        if (child == 0) {
            static_cast<void>(::dup2(out_pipe[1], 1));
            static_cast<void>(::dup2(err_pipe[1], 2));
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            const T value = clapp::parse_from<T>(std::span<const os_str>(args));
            static_cast<void>(value);
            // parse_from() returned, which none of these cases expects.
            ::_exit(70);
        }
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);

        exit_result result;
        result.out = drain(out_pipe[0]);
        result.err = drain(err_pipe[0]);

        int raw = 0;
        static_cast<void>(::waitpid(child, &raw, 0));
        const bool exited = (raw & 0x7f) == 0;
        result.status     = exited ? ((raw >> 8) & 0xff) : -1;
        return result;
    }
#endif

    // ===========================================================================
    // Runtime: the round trip, every row of the deduction table at once
    // ===========================================================================

    CLAPP_TEST("try_parse_from fills every row of the deduction table") {
        const std::expected<wide_cli, error> got =
                clapp::try_parse_from<wide_cli>(std::span<const os_str>(wide_command_line()));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;

        const wide_cli& cli = got.value();
        CLAPP_CHECK(cli.verbose);
        CLAPP_CHECK(cli.no_color);
        CLAPP_CHECK(cli.tty.has_value() && cli.tty.value());
        CLAPP_CHECK(cli.debug == 2);
        CLAPP_CHECK(cli.name == "app");
        CLAPP_CHECK(cli.port == 8080);        // member initializer survived
        CLAPP_CHECK(cli.severity == "info");  // annotation default reached the field
        CLAPP_CHECK(cli.type_ == 7);
        CLAPP_CHECK(cli.config.has_value() && cli.config.value() == "c.toml");
        CLAPP_CHECK(cli.level.has_value() && cli.level.value().has_value() &&
                    cli.level.value().value() == 3);
        CLAPP_CHECK(cli.include.size() == 2 && cli.include[0] == "a" && cli.include[1] == "b");
        CLAPP_CHECK(cli.exclude.has_value() && cli.exclude.value().size() == 1);
        CLAPP_CHECK(cli.origin[0] == 1 && cli.origin[1] == 2 && cli.origin[2] == 3);
        CLAPP_CHECK(cli.bind.first == "localhost" && cli.bind.second == 8080);
        CLAPP_CHECK(cli.color == hue::auto_);
        CLAPP_CHECK(cli.outputPath == "p.txt");
        CLAPP_CHECK(cli.kind == "release");
        CLAPP_CHECK(cli.note == "untouched");  // [[= clapp::skip{}]] never touched
    }

    CLAPP_TEST("absent is not empty, and absent is not false") {
        const std::vector<os_str> line{"wide-cli",
                                       "--name",
                                       "app",
                                       "--type",
                                       "0",
                                       "--origin",
                                       "0",
                                       "0",
                                       "0",
                                       "--bind",
                                       "h",
                                       "1",
                                       "--color",
                                       "never",
                                       "--output-path",
                                       "p",
                                       "-k",
                                       "x"};
        const std::expected<wide_cli, error> got =
                clapp::try_parse_from<wide_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;

        // std::optional<bool>: never passed, so still nullopt rather than `false`.
        CLAPP_CHECK(!got.value().tty.has_value());
        // std::optional<std::vector<T>>: never passed, so nullopt rather than an empty vector.
        CLAPP_CHECK(!got.value().exclude.has_value());
        // std::vector<T>: never passed, so empty.
        CLAPP_CHECK(got.value().include.empty());
        CLAPP_CHECK(!got.value().config.has_value());
        CLAPP_CHECK(!got.value().level.has_value());
        CLAPP_CHECK(got.value().debug == 0);
    }

    CLAPP_TEST("a member initializer survives an absent argument") {
        const std::vector<os_str> line{"defaulted-cli"};
        const std::expected<defaulted_cli, error> got =
                clapp::try_parse_from<defaulted_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;
        // The parser injects `false` for every absent set_true flag; letting it win would
        // make this `false`.
        CLAPP_CHECK(got.value().verbose);
        CLAPP_CHECK(got.value().port == 8080);
        CLAPP_CHECK(got.value().tag == "release");
    }

    CLAPP_TEST("an explicit value still beats a member initializer") {
        const std::vector<os_str> line{"defaulted-cli", "--port", "9090"};
        const std::expected<defaulted_cli, error> got =
                clapp::try_parse_from<defaulted_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (got.has_value()) CLAPP_CHECK(got.value().port == 9090);
    }

    // ===========================================================================
    // Runtime: subcommands, flatten, globals
    // ===========================================================================

    CLAPP_TEST("a subcommand round trips through the variant") {
        const std::vector<os_str> line{
                "tree-cli", "--verbose", "--jobs", "8", "add", "--message", "hello"};
        const std::expected<tree_cli, error> got =
                clapp::try_parse_from<tree_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;

        CLAPP_CHECK(got.value().verbose);
        CLAPP_CHECK(got.value().common.jobs == 8);
        CLAPP_CHECK(!got.value().common.workdir.has_value());
        CLAPP_CHECK(std::holds_alternative<cmd_add>(got.value().command));
        if (std::holds_alternative<cmd_add>(got.value().command)) {
            CLAPP_CHECK(std::get<cmd_add>(got.value().command).message == "hello");
            CLAPP_CHECK(!std::get<cmd_add>(got.value().command).force);
        }
    }

    CLAPP_TEST("from_global reads a flag spelled before the subcommand") {
        const std::vector<os_str> line{"tree-cli", "--verbose", "remove", "--paths", "a"};
        const std::expected<tree_cli, error> got =
                clapp::try_parse_from<tree_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;
        CLAPP_CHECK(std::holds_alternative<cmd_remove>(got.value().command));
        if (std::holds_alternative<cmd_remove>(got.value().command)) {
            CLAPP_CHECK(std::get<cmd_remove>(got.value().command).verbose);
        }
    }

    CLAPP_TEST("from_global reads a flag spelled after the subcommand") {
        const std::vector<os_str> line{"tree-cli", "remove", "--paths", "a", "--verbose"};
        const std::expected<tree_cli, error> got =
                clapp::try_parse_from<tree_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;
        if (std::holds_alternative<cmd_remove>(got.value().command)) {
            CLAPP_CHECK(std::get<cmd_remove>(got.value().command).verbose);
        }
    }

    CLAPP_TEST("an optional subcommand may be omitted") {
        const std::vector<os_str> line{"optional-tree-cli", "--verbose"};
        const std::expected<optional_tree_cli, error> got =
                clapp::try_parse_from<optional_tree_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(got.has_value());
        if (!got.has_value()) return;
        CLAPP_CHECK(got.value().verbose);
        CLAPP_CHECK(!got.value().command.has_value());
    }

    CLAPP_TEST("a required subcommand with an empty command line reports arg_required_else_help") {
        // clap's implicit pair for a non-Option `#[command(subcommand)]` is
        // `.subcommand_required(true).arg_required_else_help(true)`, so an empty command line
        // is help rather than a missing-subcommand error.
        const std::vector<os_str> line{"tree-cli"};
        const std::expected<tree_cli, error> got =
                clapp::try_parse_from<tree_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (got.has_value()) return;
        CLAPP_CHECK(got.error().kind() ==
                    error_kind::display_help_on_missing_argument_or_subcommand);
        CLAPP_CHECK(got.error().exit_code() == 2);
        CLAPP_CHECK(got.error().use_stderr());
    }

    CLAPP_TEST("an unknown subcommand is rejected by the parser, not by the variant") {
        const std::vector<os_str> line{"tree-cli", "nope"};
        const std::expected<tree_cli, error> got =
                clapp::try_parse_from<tree_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (!got.has_value()) {
            CLAPP_CHECK(got.error().kind() == error_kind::invalid_subcommand);
        }
    }

    // ===========================================================================
    // Runtime: errors, --help and --version
    // ===========================================================================

    CLAPP_TEST("a missing required argument is an error the caller can read") {
        const std::vector<os_str> line{"update-cli"};
        const std::expected<update_cli, error> got =
                clapp::try_parse_from<update_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (got.has_value()) return;
        CLAPP_CHECK(got.error().kind() == error_kind::missing_required_argument);
        CLAPP_CHECK(got.error().exit_code() == 2);
        CLAPP_CHECK(got.error().use_stderr());
        CLAPP_CHECK(got.error().render().contains("--name"));
    }

    CLAPP_TEST("--help is control flow, not failure") {
        const std::vector<os_str> line{"defaulted-cli", "--help"};
        const std::expected<defaulted_cli, error> got =
                clapp::try_parse_from<defaulted_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (got.has_value()) return;
        CLAPP_CHECK(got.error().kind() == error_kind::display_help);
        CLAPP_CHECK(got.error().exit_code() == 0);
        CLAPP_CHECK(!got.error().use_stderr());
    }

    CLAPP_TEST("--version needs a version and reports one") {
        const std::vector<os_str> line{"wide-cli", "--version"};
        const std::expected<wide_cli, error> got =
                clapp::try_parse_from<wide_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (got.has_value()) return;
        CLAPP_CHECK(got.error().kind() == error_kind::display_version);
        CLAPP_CHECK(got.error().exit_code() == 0);
        CLAPP_CHECK(!got.error().use_stderr());
        CLAPP_CHECK(got.error().render().contains("1.2.3"));
    }

    CLAPP_TEST("an unknown argument names itself") {
        const std::vector<os_str> line{"defaulted-cli", "--nope"};
        const std::expected<defaulted_cli, error> got =
                clapp::try_parse_from<defaulted_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (got.has_value()) return;
        CLAPP_CHECK(got.error().kind() == error_kind::unknown_argument);
        CLAPP_CHECK(got.error().render().contains("--nope"));
    }

    CLAPP_TEST("an invalid enum value lists the possible ones") {
        const std::vector<os_str> line{"wide-cli",
                                       "--name",
                                       "a",
                                       "--type",
                                       "0",
                                       "--origin",
                                       "0",
                                       "0",
                                       "0",
                                       "--bind",
                                       "h",
                                       "1",
                                       "--color",
                                       "chartreuse",
                                       "--output-path",
                                       "p",
                                       "-k",
                                       "x"};
        const std::expected<wide_cli, error> got =
                clapp::try_parse_from<wide_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (got.has_value()) return;
        CLAPP_CHECK(got.error().kind() == error_kind::invalid_value);
        CLAPP_CHECK(got.error().render().contains("chartreuse"));
    }

    // ===========================================================================
    // Runtime: try_parse over argc/argv
    // ===========================================================================

    CLAPP_TEST("try_parse takes main's parameters") {
        const char* argv[] = {"defaulted-cli", "--port", "1234"};
        const std::expected<defaulted_cli, error> got =
                clapp::try_parse<defaulted_cli>(3, static_cast<const char* const*>(argv));
        CLAPP_CHECK(got.has_value());
        if (got.has_value()) CLAPP_CHECK(got.value().port == 1234);
    }

    // ===========================================================================
    // Runtime: the braced spelling
    //
    // THESE CASES EXIST FOR THEIR COMPILATION, not for their assertions. Both
    // `std::span<const os_str>` (P2447; libc++ reports
    // `__cpp_lib_span_initializer_list == 202311`) and clapp::raw_args are implicitly
    // constructible from a `std::initializer_list<os_str>`, so with only those two
    // overloads a braced argument offers two user-defined conversion sequences of equal
    // rank. GCC 16.1.0 accepted it; clang-p2996 answered
    // `error: call to 'parse_from' is ambiguous` and named both candidates. That is a
    // build green on the primary gate and red on the second — the exact failure mode this
    // project has both gates for.
    //
    // The fix is the `std::initializer_list<os_str>` overload on each of the four entry
    // points, which is an exact match for a braced argument and beats both conversions
    // outright. It cannot be pinned by a `static_assert`: a braced-init-list is not an
    // expression, so `decltype(parse_from<T>({...}))` does not exist and only a real call
    // exercises the resolution. Spelling `std::initializer_list<os_str>{...}` at the call
    // site would test a different overload than the one users reach for.
    //
    // A regression therefore shows up as this file failing to COMPILE on clang, which is
    // the correct place for it to show up.
    // ===========================================================================

    CLAPP_TEST("a braced command line resolves on both compilers") {
        const std::expected<defaulted_cli, error> got =
                clapp::try_parse_from<defaulted_cli>({"defaulted-cli", "--port", "1234"});
        CLAPP_CHECK(got.has_value());
        if (got.has_value()) {
            CLAPP_CHECK(got.value().port == 1234);
            // The member initializers the braced line did not mention.
            CLAPP_CHECK(got.value().verbose);
            CLAPP_CHECK(got.value().tag == "release");
        }
    }

    CLAPP_TEST("a braced command line updates in place") {
        update_cli held{.name = "kept", .port = 80, .tags = {}};

        const std::expected<void, error> outcome =
                clapp::try_update_from(held, {"update-cli", "--port", "9090"});
        CLAPP_CHECK(outcome.has_value());
        CLAPP_CHECK(held.port == 9090);
        CLAPP_CHECK(held.name == "kept");
    }

    /**
     * The braced overload must not have quietly become the only one: a named `std::vector`
     * still has to reach the `span` overload, and a named clapp::raw_args the `raw_args`
     * one. Both are spellings real callers use, and adding an overload is exactly the kind
     * of change that can steal a call from its neighbour.
     */
    CLAPP_TEST("the named spellings still reach their own overloads") {
        const std::vector<os_str> line{"defaulted-cli", "--port", "4321"};
        const std::expected<defaulted_cli, error> from_span =
                clapp::try_parse_from<defaulted_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(from_span.has_value());
        if (from_span.has_value()) CLAPP_CHECK(from_span.value().port == 4321);

        const clapp::raw_args raw{std::span<const os_str>(line)};
        const std::expected<defaulted_cli, error> from_raw =
                clapp::try_parse_from<defaulted_cli>(raw);
        CLAPP_CHECK(from_raw.has_value());
        if (from_raw.has_value()) CLAPP_CHECK(from_raw.value().port == 4321);
    }

    // ===========================================================================
    // Runtime: the incremental half
    // ===========================================================================

    CLAPP_TEST("try_update_from leaves untouched what the command line does not mention") {
        update_cli held{.name = "kept", .port = 80, .tags = {"a"}};

        const std::vector<os_str> line{"update-cli", "--port", "9090"};
        const std::expected<void, error> outcome =
                clapp::try_update_from(held, std::span<const os_str>(line));
        CLAPP_CHECK(outcome.has_value());
        CLAPP_CHECK(held.port == 9090);
        // `name` is required in the parse tree and NOT in the update tree, which is the whole
        // reason command_for_update() exists.
        CLAPP_CHECK(held.name == "kept");
        CLAPP_CHECK(held.tags.size() == 1 && held.tags[0] == "a");
    }

    CLAPP_TEST("try_update_from does write what the command line does mention") {
        update_cli held{.name = "old", .port = 80, .tags = {}};

        const std::vector<os_str> line{"update-cli", "--name", "new"};
        const std::expected<void, error> outcome =
                clapp::try_update_from(held, std::span<const os_str>(line));
        CLAPP_CHECK(outcome.has_value());
        CLAPP_CHECK(held.name == "new");
        CLAPP_CHECK(held.port == 80);
    }

    CLAPP_TEST("try_parse_from would have rejected the same command line") {
        // The mirror of the case above: without command_for_update(), an incremental parse
        // would demand `--name` on every call.
        const std::vector<os_str> line{"update-cli", "--port", "9090"};
        const std::expected<update_cli, error> got =
                clapp::try_parse_from<update_cli>(std::span<const os_str>(line));
        CLAPP_CHECK(!got.has_value());
        if (!got.has_value()) {
            CLAPP_CHECK(got.error().kind() == error_kind::missing_required_argument);
        }
    }

    CLAPP_TEST("an update keeps a subcommand the new command line does not name") {
        optional_tree_cli held{};
        held.command = cmd_add{.message = "first", .force = false, .author = std::nullopt};

        const std::vector<os_str> line{"optional-tree-cli", "--verbose"};
        const std::expected<void, error> outcome =
                clapp::try_update_from(held, std::span<const os_str>(line));
        CLAPP_CHECK(outcome.has_value());
        CLAPP_CHECK(held.verbose);
        CLAPP_CHECK(held.command.has_value());
        if (held.command.has_value() && std::holds_alternative<cmd_add>(held.command.value())) {
            CLAPP_CHECK(std::get<cmd_add>(held.command.value()).message == "first");
        }
    }

    CLAPP_TEST("an update of the same subcommand updates it in place") {
        optional_tree_cli held{};
        held.command = cmd_add{.message = "first", .force = true, .author = std::nullopt};

        const std::vector<os_str> line{"optional-tree-cli", "add", "--author", "me"};
        const std::expected<void, error> outcome =
                clapp::try_update_from(held, std::span<const os_str>(line));
        CLAPP_CHECK(outcome.has_value());
        if (held.command.has_value() && std::holds_alternative<cmd_add>(held.command.value())) {
            const cmd_add& add = std::get<cmd_add>(held.command.value());
            CLAPP_CHECK(add.author.has_value() && add.author.value() == "me");
            // A `set` argument the command line did not mention records no value at all, so
            // the caller's `message` survives — this is what makes the update in-place rather
            // than a rebuild.
            CLAPP_CHECK(add.message == "first");
            // `force` does NOT survive, and that is clap's behaviour rather than a defect:
            // clap_derive's updater guards every field with
            // `if arg_matches.contains_id(id)`, and `ArgAction::SetTrue` carries a
            // `default_value` of "false" (clap_builder/src/builder/action.rs,
            // `ArgAction::default_value`) which the parser records for an absent flag. So
            // `contains_id` is true, and clap assigns `false` too. The way to keep a flag
            // across an update is a member initializer — `bool force = true;` — which
            // clapp::detail::wins_over_initializer() honours; see the next case.
            CLAPP_CHECK(!add.force);
        }
    }

    /**
     * The same shape, with an initializer on the flag.
     *
     * clapp's one deliberate divergence from clap: a value whose clapp::value_source is
     * `default_value` does not overwrite a field that has a default member initializer. That
     * rule is what makes a flag survive an update, and it is the only way to get the
     * behaviour, so it is worth a case of its own rather than a note.
     */
    struct sticky_cli {
        bool force = true;
        std::string message;
    };

    CLAPP_TEST("a flag with a member initializer survives an update") {
        sticky_cli held{.force = true, .message = "first"};

        const std::vector<os_str> line{"sticky-cli", "--message", "second"};
        const std::expected<void, error> outcome =
                clapp::try_update_from(held, std::span<const os_str>(line));
        CLAPP_CHECK(outcome.has_value());
        CLAPP_CHECK(held.message == "second");
        CLAPP_CHECK(held.force);
    }

    // ===========================================================================
    // Runtime: the two directions agree across a flatten that crosses rename_all
    // ===========================================================================

    CLAPP_TEST("a subcommand behind a flatten round-trips under the enclosing rename_all") {
        // The regression. The command line the parser accepts must be the one the
        // store-back accepts; the failure mode was a clean parse followed by
        // error_kind::invalid_subcommand out of from_matches().
        const std::expected<snake_root_cli, error> got =
                clapp::try_parse_from<snake_root_cli>({"snakeroot", "do_thing", "--who", "bob"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(std::holds_alternative<cmd_do_thing>(got.value().nested.command));
        CLAPP_CHECK(std::get<cmd_do_thing>(got.value().nested.command).who == "bob");
    }

    CLAPP_TEST("the kebab spelling of that subcommand is not accepted") {
        // The other half: if BOTH directions had moved to kebab the case above would still
        // pass. This one pins which of the two names the tree actually carries.
        const std::expected<snake_root_cli, error> got =
                clapp::try_parse_from<snake_root_cli>({"snakeroot", "do-thing", "--who", "bob"});
        CLAPP_CHECK(!got.has_value());
        CLAPP_CHECK(got.error().kind() == error_kind::invalid_subcommand);
        CLAPP_CHECK(got.error().render().contains("do-thing"));
    }

    CLAPP_TEST("rename_all on a flattened struct is inert in both directions") {
        const std::expected<kebab_root_cli, error> got =
                clapp::try_parse_from<kebab_root_cli>({"kebabroot", "undo-thing", "--why", "x"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(std::holds_alternative<cmd_undo_thing>(got.value().nested.command));
        CLAPP_CHECK(std::get<cmd_undo_thing>(got.value().nested.command).why ==
                    std::optional<std::string>{"x"});
    }

    // ===========================================================================
    // Runtime: which of the two default channels wins
    // ===========================================================================

    CLAPP_TEST("an explicit .default_value outranks a member initializer") {
        const std::expected<two_defaults_cli, error> got =
                clapp::try_parse_from<two_defaults_cli>({"twodefaults"});
        CLAPP_CHECK(got.has_value());
        // 5, not 8080: the annotation is the only default the command_spec carries, so it
        // is the only one --help could ever render. Letting the unreadable channel win made
        // the two disagree with nothing to catch it.
        CLAPP_CHECK(got.value().port == 5u);
    }

    CLAPP_TEST("the parser's own injected default still yields to a member initializer") {
        // The narrowness of the rule above. `verbose` has no `.default_value`, so the
        // `"false"` that arg_action::set_true records for an absent flag must not win.
        const std::expected<two_defaults_cli, error> got =
                clapp::try_parse_from<two_defaults_cli>({"twodefaults"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(got.value().verbose);
    }

    CLAPP_TEST("the command line still beats both default channels") {
        const std::expected<two_defaults_cli, error> got = clapp::try_parse_from<two_defaults_cli>(
                {"twodefaults", "--port", "9090", "--verbose"});
        CLAPP_CHECK(got.has_value());
        CLAPP_CHECK(got.value().port == 9090u);
        CLAPP_CHECK(got.value().verbose);
    }

    // ===========================================================================
    // Runtime: parse<T>() prints and exits
    // ===========================================================================

#if __has_include(<unistd.h>) && __has_include(<sys/wait.h>)

    CLAPP_TEST("parse_from prints --help to stdout and exits 0") {
        const std::vector<os_str> line{"defaulted-cli", "--help"};
        const exit_result got = run_child<defaulted_cli>(line);
        CLAPP_CHECK(got.status == 0);
        CLAPP_CHECK(!got.out.empty());
        CLAPP_CHECK(got.err.empty());
    }

    CLAPP_TEST("parse_from prints an error to stderr and exits 2") {
        const std::vector<os_str> line{"defaulted-cli", "--nope"};
        const exit_result got = run_child<defaulted_cli>(line);
        CLAPP_CHECK(got.status == 2);
        CLAPP_CHECK(got.out.empty());
        CLAPP_CHECK(got.err.find("--nope") != std::string::npos);
    }

    CLAPP_TEST("parse_from returns normally on a good command line") {
        // The child exits 70 from run_child() when parse_from() returns, which is exactly
        // what a successful parse must do. Without this case, a parse_from() that exited on
        // *every* path would still pass the two cases above.
        const std::vector<os_str> line{"defaulted-cli", "--port", "1"};
        const exit_result got = run_child<defaulted_cli>(line);
        CLAPP_CHECK(got.status == 70);
        CLAPP_CHECK(got.out.empty());
        CLAPP_CHECK(got.err.empty());
    }

    CLAPP_TEST("parse_from ends its message with a newline") {
        const std::vector<os_str> line{"defaulted-cli", "--nope"};
        const exit_result got = run_child<defaulted_cli>(line);
        CLAPP_CHECK(!got.err.empty());
        if (!got.err.empty()) CLAPP_CHECK(got.err.back() == '\n');
    }

#else

    CLAPP_TEST("parse_from's exit path is not exercised on this platform") {
        // No fork(): the stream and the status are still pinned structurally by the
        // `--help is control flow` and `a missing required argument` cases above, which
        // assert exactly what clapp::detail::report_and_exit() reads.
        CLAPP_CHECK(true);
    }

#endif

}  // namespace

// NOLINTEND
