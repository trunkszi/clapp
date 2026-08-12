#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/meta/deduce.hpp>
#include <clapp/meta/from_matches.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/str.hpp>

#include "support/check.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// These fixtures are consumed through C++ reflection, which CLion's unused-member
// inspection cannot currently see.
// ReSharper disable CppUnusedEnumerator
// ReSharper disable CppUnusedStructMember

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
    using clapp::raw_args;
    using clapp::store_kind;
    using clapp::value_range;
    using clapp::value_source;

    using clapp::meta::arg_id_of;
    using clapp::meta::rename_all_of;
    using clapp::meta::subcommand_name_of;

    // ---------------------------------------------------------------------------
    // Fixture types
    // ---------------------------------------------------------------------------

    /**
     * Deliberately NOT named `color_choice`: clapp::color_choice already exists in
     * builder/command.hpp, and shadowing it here would make the enum row look like it was
     * testing clapp's own type.
     */
    enum class hue : unsigned char {
        always [[maybe_unused]],
        auto_,
        never [[maybe_unused]],
    };

    /**
     * Every argument-producing row of the deduction table, in one struct.
     *
     * `no_color` and `type_` are load-bearing: the first has an underscore the long
     * spelling turns into a dash, the second has the keyword-avoidance suffix that the id
     * must strip. A file whose fields were all single words could not tell a correct id
     * from a renamed one.
     */
    struct wide_cli {
        bool verbose;             // flag
        bool no_color;            // flag, id != long name
        std::optional<bool> tty;  // optional flag, tri-state
        [[= clapp::arg{.short_ = 'd', .act = clapp::action::count}]] count_type debug;  // counter
        std::string name;                                   // scalar, required
        unsigned port = 8080;                               // scalar with a default
        int type_;                                          // scalar, id strips `_`
        std::optional<std::string> config;                  // optional scalar
        std::optional<std::optional<int>> level;            // three-state scalar
        std::vector<std::string> include;                   // sequence
        std::optional<std::vector<std::string>> exclude;    // optional sequence
        std::array<int, 3> origin;                          // fixed arity
        std::pair<std::string, int> bind;                   // two different types
        hue color;                                          // enumeration
        [[= clapp::skip{}]] std::string note{"untouched"};  // never parsed
    };

    /** Flattened into whatever command embeds it. */
    struct shared_opts {
        std::optional<std::string> workdir;
        unsigned jobs = 4;
    };

    struct[[= clapp::cmd{.name = "add"}]] cmd_add {
        std::string message;
        bool force;
        std::optional<std::string> author;
    };

    /** No `[[= clapp::cmd]]`: the name comes from the type, `cmd_remove` -> `remove`. */
    struct cmd_remove {
        std::vector<std::string> paths;
        /** Declared by the ROOT, not here; clapp::parse() propagates it down. */
        [[maybe_unused]] [[= clapp::from_global{}]] bool verbose;
    };

    struct tree_cli {
        [[= clapp::arg{.global = true}]] bool verbose;
        [[= clapp::flatten{}]] shared_opts common;
        [[= clapp::subcommand{}]] std::variant<cmd_add, cmd_remove> command;
    };

    struct optional_tree_cli {
        /**
         * Carried for the same reason tree_cli carries it: `cmd_remove` has a
         * `[[= clapp::from_global{}]]` field, and without an ancestor declaring the global
         * there is nothing for command_builder::propagate_global_args() to copy down.
         * ids_agree() below catches the omission at compile time, which is how this
         * member came to be here.
         */
        [[maybe_unused]] [[= clapp::arg{.global = true}]] bool verbose;
        [[= clapp::subcommand{}]] std::optional<std::variant<cmd_add, cmd_remove>> command;
    };

    /**
     * Two fields that fail with DIFFERENT error kinds, in a known order.
     *
     * Exists only to make `fill_struct`'s first-failure-wins guard observable: `command`
     * reports clapp::error_kind::missing_subcommand and `name` reports
     * clapp::error_kind::missing_required_argument, so an implementation that lets the last
     * failure overwrite the first answers the wrong kind. Every other fixture here fails
     * with one kind repeatedly, which cannot tell the two readings apart.
     */
    struct order_cli {
        [[maybe_unused]] [[= clapp::subcommand{}]] std::variant<cmd_add, cmd_remove> command;
        [[maybe_unused]] std::string name;
        [[maybe_unused]] [[= clapp::arg{.global = true}]] bool verbose;
    };

    /** The member-initializer rule: both of these survive an absent argument. */
    struct defaulted_cli {
        bool verbose    = true;
        unsigned port   = 8080;
        std::string tag = "release";
    };

    // ---------------------------------------------------------------------------
    // derive_command<T>() — the stand-in forward direction
    // ---------------------------------------------------------------------------

    /**
     * Copy a promoted static string into a transient one.
     *
     * **Load-bearing under the `ubsan` preset.** clapp::arg_builder and
     * clapp::command_builder both take a `std::string_view` and construct a `std::string`
     * from it, and libstdc++'s `basic_string(const CharT*, size_type)` tests the SOURCE
     * pointer against null. Under `-fsanitize=null` GCC 16.1.0 will not fold that
     * comparison when the pointer's base object is a variable — which is exactly what
     * `std::define_static_string` produces, and therefore what
     * clapp::meta::arg_id_of() and clapp::meta::subcommand_name_of() return.
     * CLAUDE.md trap 10; measured 2026-08 with these four probes:
     *
     * | source of the `string_view`                     | `std::string s{sv}` at compile time |
     * |-------------------------------------------------|-------------------------------------|
     * | a string literal in this TU                     | folds                               |
     * | `std::define_static_string(...)`                | **`'(((const char*)(&"verbose")) == 0)' is not a constant expression`** |
     * | a transient `std::string` built with push_back  | folds                               |
     *
     * So the promoted view has to make one hop through a transient string first. The real
     * `command_of<T>()` needs the same hop, or the whole ubsan preset goes red the day it
     * lands; the alternative is to build clapp::arg_builder's `id_` with
     * clapp::detail::append_bytes() instead of from a view.
     */
    consteval std::string owned(std::string_view text) {
        std::string out;
        clapp::detail::append_bytes(out, text);
        return out;
    }

    template<class T>
    consteval void derive_args_into(command_builder& into, naming style);

    template<class V>
    consteval void derive_subcommands_into(command_builder& into, naming style);

    template<class T>
    consteval void derive_args_into(command_builder& into, naming style) {
        template for (constexpr std::meta::info member :
                      std::define_static_array(std::meta::nonstatic_data_members_of(
                              ^^T, std::meta::access_context::current()))) {
            using member_type = [:std::meta::type_of(member):];

            constexpr deduced_arg deduced  = clapp::meta::deduce_member(member);
            constexpr std::string_view id  = arg_id_of<T, member>();
            constexpr clapp::arg_attr attr = clapp::meta::annotation_or<clapp::arg_attr>(member);
            constexpr bool inherited = clapp::meta::has_annotation<clapp::from_global>(member);

            if constexpr (deduced.shape == field_shape::skipped || inherited) {
                // `skip` never becomes an argument; `from_global` is declared by an
                // ancestor and copied down by command_builder::propagate_global_args().
            } else if constexpr (deduced.shape == field_shape::flattened) {
                derive_args_into<member_type>(into, style);
            } else if constexpr (deduced.shape == field_shape::subcommand_set) {
                std::move(into).subcommand_required();
                derive_subcommands_into<member_type>(into, style);
            } else if constexpr (deduced.shape == field_shape::optional_subcommand_set) {
                using variant_type = [:clapp::meta::sole_type_argument(^^member_type):];
                derive_subcommands_into<variant_type>(into, style);
            } else {
                arg_builder argument{owned(id)};
                std::move(argument)
                        .long_(clapp::rename(std::meta::identifier_of(member), style))
                        .action(deduced.act)
                        .num_args(deduced.num_args)
                        .required(deduced.required);
                if (attr.short_ != '\0') std::move(argument).short_(attr.short_);
                if (attr.global) std::move(argument).global();

                // A value_parser is set only where the field type decides it. The three
                // action-pinned rows (`set_true` on a flag, `count` on a counter) already
                // carry the right one, and arg_builder::freeze() REJECTS a contradicting
                // parser on a counter; `std::pair` has two element types and no single
                // parser, so it keeps the default and from_matches re-parses the raw bytes.
                if constexpr (deduced.shape != field_shape::flag &&
                              deduced.shape != field_shape::optional_flag &&
                              deduced.shape != field_shape::counter &&
                              deduced.shape != field_shape::pair) {
                    using value_type = [:clapp::meta::value_type_of(^^member_type):];
                    std::move(argument).value_parser<value_type>();
                }
                std::move(into).arg(std::move(argument));
            }
        }
    }

    template<class V>
    consteval void derive_subcommands_into(command_builder& into, naming style) {
        template for (constexpr std::meta::info alternative :
                      clapp::meta::variant_traits<V>::reflections) {
            using alternative_type = [:alternative:];
            command_builder child{owned(subcommand_name_of(alternative, style))};
            derive_args_into<alternative_type>(child, rename_all_of(alternative));
            std::move(into).subcommand(std::move(child));
        }
    }

    template<class T>
    consteval command_spec derive_command(std::string_view name) {
        command_builder root{name};
        derive_args_into<T>(root, rename_all_of(^^T));
        return root.freeze();
    }

    constexpr command_spec wide_spec          = derive_command<wide_cli>("wide");
    constexpr command_spec tree_spec          = derive_command<tree_cli>("tree");
    constexpr command_spec optional_tree_spec = derive_command<optional_tree_cli>("opt");
    constexpr command_spec defaulted_spec     = derive_command<defaulted_cli>("defaulted");
    constexpr command_spec order_spec         = derive_command<order_cli>("order");

    // ---------------------------------------------------------------------------
    // Compile-time: the id invariant
    // ---------------------------------------------------------------------------

    // The id is the FIELD NAME, never the long spelling. If either of the first two
    // assertions is inverted the whole file still links and every value silently vanishes.
    static_assert(arg_id_of(std::meta::nonstatic_data_members_of(
                          ^^wide_cli, std::meta::access_context::current())[1]) == "no_color");
    static_assert(clapp::rename("no_color", naming::kebab) == "no-color");
    static_assert(arg_id_of(std::meta::nonstatic_data_members_of(
                          ^^wide_cli, std::meta::access_context::current())[6]) == "type");

    // The alternative names: annotation first, derived-from-the-type-name second.
    static_assert(subcommand_name_of(^^cmd_add) == "add");
    static_assert(subcommand_name_of(^^cmd_remove) == "remove");
    static_assert(subcommand_name_of(^^cmd_remove, naming::snake) == "remove");

    /**
     * Walk every member of \p T and assert that the id from_matches() will look up is an id
     * the frozen command tree actually declares.
     *
     * This is the drift detector. It compares two *independently produced* artefacts — the
     * spec that the forward direction froze, and the key the reverse direction computes —
     * so a forward direction that renamed its ids fails here and nowhere else.
     */
    template<class T, naming Style>
    consteval bool ids_agree(const command_spec& spec) {
        bool ok = true;
        template for (constexpr std::meta::info member :
                      std::define_static_array(std::meta::nonstatic_data_members_of(
                              ^^T, std::meta::access_context::current()))) {
            using member_type             = [:std::meta::type_of(member):];
            constexpr deduced_arg deduced = clapp::meta::deduce_member(member);
            constexpr std::string_view id = arg_id_of<T, member>();

            if constexpr (deduced.shape == field_shape::skipped) {
                // A skipped field must NOT be declared; declaring it would make `--note`
                // parse and then be thrown away.
                if (spec.has_arg(id)) ok = false;
            } else if constexpr (deduced.shape == field_shape::flattened) {
                if (!ids_agree<member_type, Style>(spec)) ok = false;
            } else if constexpr (deduced.shape == field_shape::subcommand_set ||
                                 deduced.shape == field_shape::optional_subcommand_set) {
                using variant_type = [:deduced.shape == field_shape::subcommand_set
                                               ? clapp::meta::canonical_type(^^member_type)
                                               : clapp::meta::sole_type_argument(^^member_type):];
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
    static_assert(ids_agree<order_cli, rename_all_of(^^order_cli)>(order_spec));

    // The negative half: `ids_agree` must be able to FAIL. Without this, an implementation
    // that returned `true` unconditionally would satisfy every assertion above.
    consteval bool ids_agree_rejects_a_renamed_tree() {
        command_builder wrong{"wide"};
        // The plausible bug: keying the argument on its long spelling instead of its id.
        std::move(wrong).arg(
                arg_builder("no-color").long_("no-color").action(arg_action::set_true));
        const command_spec frozen = wrong.freeze();
        return !frozen.has_arg("no_color") && frozen.has_arg("no-color");
    }
    static_assert(ids_agree_rejects_a_renamed_tree());

    // The shape the spec really has, so the runtime cases below are not vacuous.
    static_assert(wide_spec.find_arg("origin")->get_num_args() == value_range::exactly(3));
    static_assert(wide_spec.find_arg("bind")->get_num_args() == value_range::exactly(2));
    static_assert(wide_spec.find_arg("level")->get_num_args() == value_range::optional());
    static_assert(wide_spec.find_arg("include")->get_action() == arg_action::append);
    static_assert(wide_spec.find_arg("name")->is_required_set());
    static_assert(!wide_spec.find_arg("port")->is_required_set());
    static_assert(!wide_spec.has_arg("note"));
    static_assert(tree_spec.find_arg("verbose")->is_global_set());
    static_assert(tree_spec.has_subcommand("add") && tree_spec.has_subcommand("remove"));
    // propagate_global_args() is what makes `[[= clapp::from_global{}]]` work without
    // from_matches() ever seeing an ancestor.
    static_assert(tree_spec.find_subcommand("remove")->has_arg("verbose"));
    static_assert(tree_spec.is_subcommand_required_set());
    static_assert(!optional_tree_spec.is_subcommand_required_set());

    // The deduction rows this file exercises, named so that a change in deduce.hpp fails
    // here rather than making a runtime expectation quietly wrong.
    static_assert(clapp::meta::deduce_member(std::meta::nonstatic_data_members_of(
                                                     ^^wide_cli,
                                                     std::meta::access_context::current())[2])
                          .store == store_kind::optional_flag);
    static_assert(clapp::meta::deduce_member(std::meta::nonstatic_data_members_of(
                                                     ^^wide_cli,
                                                     std::meta::access_context::current())[10])
                          .store == store_kind::optional_many);
    static_assert(clapp::meta::deduce_member(std::meta::nonstatic_data_members_of(
                                                     ^^wide_cli,
                                                     std::meta::access_context::current())[12])
                          .store == store_kind::pair);

    // from_matches() cannot be constant-evaluated, and that is a property of arg_matches
    // rather than a choice made here.
    static_assert(!std::is_trivially_destructible_v<arg_matches>);
    static_assert(std::is_trivially_destructible_v<command_spec>);

    // ---------------------------------------------------------------------------
    // Runtime helpers
    // ---------------------------------------------------------------------------

    template<class T>
    std::expected<T, error> round_trip(const command_spec& spec, const raw_args& raw) {
        const std::expected<arg_matches, error> matches = clapp::parse(spec, raw);
        if (!matches.has_value()) return std::unexpected(matches.error());
        return clapp::from_matches<T>(matches.value());
    }

    // The full command line for wide_cli; every required argument is present.
    raw_args wide_line() {
        return raw_args{"wide",    "--verbose", "--no-color", "--tty",     "-ddd",      "--name",
                        "demo",    "--type",    "7",          "--config",  "/etc/x",    "--include",
                        "a",       "--include", "b",          "--exclude", "z",         "--origin",
                        "1",       "2",         "3",          "--bind",    "localhost", "8080",
                        "--color", "auto",      "--level",    "42"};
    }

    // The minimum wide_cli accepts: only the four required arguments.
    raw_args wide_minimum() {
        return raw_args{"wide",
                        "--name",
                        "demo",
                        "--type",
                        "7",
                        "--origin",
                        "1",
                        "2",
                        "3",
                        "--bind",
                        "host",
                        "1",
                        "--color",
                        "never"};
    }

}  // namespace

// ---------------------------------------------------------------------------
// The end-to-end loop
// ---------------------------------------------------------------------------

CLAPP_TEST("every row of the deduction table survives one round trip") {
    const std::expected<wide_cli, error> got = round_trip<wide_cli>(wide_spec, wide_line());
    CLAPP_CHECK(got.has_value());

    CLAPP_CHECK(got->verbose);
    CLAPP_CHECK(got->no_color);
    CLAPP_CHECK(got->tty == std::optional<bool>{true});
    CLAPP_CHECK(got->debug == count_type{3});
    CLAPP_CHECK(got->name == "demo");
    CLAPP_CHECK(got->port == 8080u);  // untouched: the member initializer
    CLAPP_CHECK(got->type_ == 7);
    CLAPP_CHECK(got->config == std::optional<std::string>{"/etc/x"});
    CLAPP_CHECK(got->level.has_value() && got->level->has_value() && **got->level == 42);
    CLAPP_CHECK(got->include.size() == 2 && got->include[0] == "a" && got->include[1] == "b");
    CLAPP_CHECK(got->exclude.has_value() && got->exclude->size() == 1 &&
                got->exclude->front() == "z");
    CLAPP_CHECK(got->origin == std::array<int, 3>{1, 2, 3});
    CLAPP_CHECK(got->bind.first == "localhost" && got->bind.second == 8080);
    CLAPP_CHECK(got->color == hue::auto_);
    CLAPP_CHECK(got->note == "untouched");
}

CLAPP_TEST("an absent optional is nullopt, not a default-constructed value") {
    const std::expected<wide_cli, error> got = round_trip<wide_cli>(wide_spec, wide_minimum());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->config.has_value());
    CLAPP_CHECK(!got->level.has_value());
    CLAPP_CHECK(!got->exclude.has_value());
    CLAPP_CHECK(got->include.empty());
    CLAPP_CHECK(!got->verbose);
}

// ---------------------------------------------------------------------------
// optional<bool>: absent / explicitly present
// ---------------------------------------------------------------------------

CLAPP_TEST("optional<bool> stays nullopt when the flag is absent") {
    // The trap: clapp::arg_action::set_true injects `false` for every absent flag, so
    // `try_get_one<bool>` DOES answer, and engaging on that collapses the tri-state.
    const std::expected<arg_matches, error> matches = clapp::parse(wide_spec, wide_minimum());
    CLAPP_CHECK(matches.has_value());
    CLAPP_CHECK(matches->contains_id("tty"));
    CLAPP_CHECK(matches->value_source("tty") == value_source::default_value);

    const std::expected<wide_cli, error> got = clapp::from_matches<wide_cli>(matches.value());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->tty.has_value());
}

CLAPP_TEST("optional<bool> engages only for an explicit source") {
    const std::expected<wide_cli, error> got = round_trip<wide_cli>(wide_spec, wide_line());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->tty == std::optional<bool>{true});
}

// ---------------------------------------------------------------------------
// optional<optional<T>>: three states
// ---------------------------------------------------------------------------

CLAPP_TEST("optional<optional<T>> tells absent from present-without-a-value") {
    const std::expected<wide_cli, error> absent = round_trip<wide_cli>(wide_spec, wide_minimum());
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!absent->level.has_value());

    const raw_args bare{"wide",
                        "--name",
                        "demo",
                        "--type",
                        "7",
                        "--origin",
                        "1",
                        "2",
                        "3",
                        "--bind",
                        "host",
                        "1",
                        "--color",
                        "never",
                        "--level"};
    const std::expected<wide_cli, error> empty = round_trip<wide_cli>(wide_spec, bare);
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(empty->level.has_value());
    CLAPP_CHECK(!empty->level->has_value());

    const raw_args filled{"wide",
                          "--name",
                          "demo",
                          "--type",
                          "7",
                          "--origin",
                          "1",
                          "2",
                          "3",
                          "--bind",
                          "host",
                          "1",
                          "--color",
                          "never",
                          "--level",
                          "9"};
    const std::expected<wide_cli, error> valued = round_trip<wide_cli>(wide_spec, filled);
    CLAPP_CHECK(valued.has_value());
    CLAPP_CHECK(valued->level.has_value() && valued->level->has_value());
    CLAPP_CHECK(**valued->level == 9);
}

// ---------------------------------------------------------------------------
// optional<vector<T>>: absent is NOT empty
// ---------------------------------------------------------------------------

namespace {

    struct list_probe {
        std::optional<std::vector<std::string>> exclude;
        std::vector<std::string> include;
    };

    // Hand-built rather than derived: the deduction table gives an optional sequence
    // `num_args(1..)`, so "present with no values" is unreachable through the derived spec.
    // The state is still reachable in general — clap's `num_args(0..)` produces it — and it
    // is exactly the state that separates `nullopt` from an empty vector.
    consteval command_spec make_list_probe() {
        command_builder app("probe");
        std::move(app)
                .arg(arg_builder("exclude")
                             .long_("exclude")
                             .action(arg_action::append)
                             .num_args(value_range::at_least(0))
                             .value_parser<std::string>())
                .arg(arg_builder("include")
                             .long_("include")
                             .action(arg_action::append)
                             .num_args(value_range::at_least(1))
                             .value_parser<std::string>());
        return app.freeze();
    }
    constexpr command_spec list_probe_spec = make_list_probe();
    static_assert(ids_agree<list_probe, naming::kebab>(list_probe_spec));

}  // namespace

CLAPP_TEST("optional<vector<T>>: absent yields nullopt") {
    const std::expected<list_probe, error> got =
            round_trip<list_probe>(list_probe_spec, raw_args{"probe"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->exclude.has_value());
}

CLAPP_TEST("optional<vector<T>>: present with no values yields an ENGAGED empty vector") {
    const std::expected<arg_matches, error> matches =
            clapp::parse(list_probe_spec, raw_args{"probe", "--exclude"});
    CLAPP_CHECK(matches.has_value());
    CLAPP_CHECK(matches->contains_id("exclude"));

    const std::expected<list_probe, error> got = clapp::from_matches<list_probe>(matches.value());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->exclude.has_value());
    CLAPP_CHECK(got->exclude->empty());
    // The distinction the row exists for: these two are different objects.
    CLAPP_CHECK(got->exclude != std::optional<std::vector<std::string>>{});
}

CLAPP_TEST("optional<vector<T>>: present with values yields them all") {
    const std::expected<list_probe, error> got = round_trip<list_probe>(
            list_probe_spec, raw_args{"probe", "--exclude", "a", "b", "--exclude", "c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->exclude.has_value());
    CLAPP_CHECK(got->exclude->size() == 3);
    CLAPP_CHECK((*got->exclude)[0] == "a" && (*got->exclude)[2] == "c");
}

CLAPP_TEST("a plain vector is empty rather than absent") {
    const std::expected<list_probe, error> got =
            round_trip<list_probe>(list_probe_spec, raw_args{"probe"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->include.empty());
}

// ---------------------------------------------------------------------------
// Fixed arity: array and pair
// ---------------------------------------------------------------------------

CLAPP_TEST("array<T, N> takes exactly N values") {
    const std::expected<wide_cli, error> got = round_trip<wide_cli>(wide_spec, wide_minimum());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->origin == std::array<int, 3>{1, 2, 3});
}

CLAPP_TEST("pair<A, B> parses its two elements with two different value parsers") {
    const std::expected<wide_cli, error> got = round_trip<wide_cli>(wide_spec, wide_minimum());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->bind.first == "host");
    CLAPP_CHECK(got->bind.second == 1);
}

CLAPP_TEST("a pair element the parser rejects is a value_validation error") {
    // The arg itself accepts any two tokens (its value_parser is std::string); the
    // SECOND element is the one that has to be an int, and only from_matches knows that.
    const raw_args bad{"wide",
                       "--name",
                       "demo",
                       "--type",
                       "7",
                       "--origin",
                       "1",
                       "2",
                       "3",
                       "--bind",
                       "host",
                       "not-a-number",
                       "--color",
                       "never"};
    const std::expected<arg_matches, error> matches = clapp::parse(wide_spec, bad);
    CLAPP_CHECK(matches.has_value());

    const std::expected<wide_cli, error> got = clapp::from_matches<wide_cli>(matches.value());
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(got.error().kind() == error_kind::value_validation);
    // The kind alone cannot tell WHICH field failed, and "the right kind for the wrong
    // field" is precisely the drift this module exists to prevent. `bind` is the id;
    // `not-a-number` is the token.
    CLAPP_CHECK(got.error().render().contains("bind"));
    CLAPP_CHECK(got.error().render().contains("not-a-number"));
}

// ---------------------------------------------------------------------------
// Member initializers are defaults
// ---------------------------------------------------------------------------

CLAPP_TEST("a member initializer survives an absent argument") {
    const std::expected<defaulted_cli, error> got =
            round_trip<defaulted_cli>(defaulted_spec, raw_args{"defaulted"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->port == 8080u);
    CLAPP_CHECK(got->tag == "release");
    // The one that a naive implementation gets wrong: `set_true` injects `false`, and
    // letting that win turns `bool verbose = true;` into `false`.
    CLAPP_CHECK(got->verbose);
}

CLAPP_TEST("the command line still beats a member initializer") {
    const std::expected<defaulted_cli, error> got = round_trip<defaulted_cli>(
            defaulted_spec, raw_args{"defaulted", "--port", "9090", "--tag", "debug"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->port == 9090u);
    CLAPP_CHECK(got->tag == "debug");
}

// ---------------------------------------------------------------------------
// skip
// ---------------------------------------------------------------------------

CLAPP_TEST("a skipped field keeps its initializer and never reaches the command line") {
    const std::expected<wide_cli, error> got = round_trip<wide_cli>(wide_spec, wide_minimum());
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->note == "untouched");

    // `--note` is not an argument at all, so the parse fails rather than silently
    // dropping it.
    const raw_args rejected{"wide",
                            "--name",
                            "demo",
                            "--type",
                            "7",
                            "--origin",
                            "1",
                            "2",
                            "3",
                            "--bind",
                            "host",
                            "1",
                            "--color",
                            "never",
                            "--note",
                            "x"};
    const std::expected<arg_matches, error> matches = clapp::parse(wide_spec, rejected);
    CLAPP_CHECK(!matches.has_value());
    CLAPP_CHECK(matches.error().kind() == error_kind::unknown_argument);
}

// ---------------------------------------------------------------------------
// flatten
// ---------------------------------------------------------------------------

CLAPP_TEST("a flattened struct is filled from the same level's matches") {
    const std::expected<tree_cli, error> got = round_trip<tree_cli>(
            tree_spec,
            raw_args{"tree", "--workdir", "/src", "--jobs", "9", "add", "--message", "hi"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->common.workdir == std::optional<std::string>{"/src"});
    CLAPP_CHECK(got->common.jobs == 9u);
}

CLAPP_TEST("a flattened struct keeps its own member initializers when absent") {
    const std::expected<tree_cli, error> got =
            round_trip<tree_cli>(tree_spec, raw_args{"tree", "add", "--message", "hi"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->common.workdir.has_value());
    CLAPP_CHECK(got->common.jobs == 4u);
}

// ---------------------------------------------------------------------------
// Subcommands: the variant dispatch
// ---------------------------------------------------------------------------

CLAPP_TEST("the variant holds the alternative the parse chose") {
    const std::expected<tree_cli, error> got = round_trip<tree_cli>(
            tree_spec, raw_args{"tree", "add", "--message", "initial", "--force"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->command.index() == 0);
    CLAPP_CHECK(std::holds_alternative<cmd_add>(got->command));
    CLAPP_CHECK(std::get<cmd_add>(got->command).message == "initial");
    CLAPP_CHECK(std::get<cmd_add>(got->command).force);
}

CLAPP_TEST("a later alternative is reached too, not only the first") {
    // The `matched` guard: `template for` is an expansion, so without it every later
    // body runs as well and the LAST alternative wins regardless of the name.
    const std::expected<tree_cli, error> got = round_trip<tree_cli>(
            tree_spec, raw_args{"tree", "remove", "--paths", "a", "--paths", "b"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(std::holds_alternative<cmd_remove>(got->command));
    CLAPP_CHECK(std::get<cmd_remove>(got->command).paths.size() == 2);
}

CLAPP_TEST("a from_global field reads the ancestor's value at the subcommand level") {
    const std::expected<tree_cli, error> after = round_trip<tree_cli>(
            tree_spec, raw_args{"tree", "remove", "--paths", "a", "--verbose"});
    CLAPP_CHECK(after.has_value());
    CLAPP_CHECK(after->verbose);
    CLAPP_CHECK(std::get<cmd_remove>(after->command).verbose);

    // Spelled before the subcommand instead: propagation must carry it DOWN.
    const std::expected<tree_cli, error> before = round_trip<tree_cli>(
            tree_spec, raw_args{"tree", "--verbose", "remove", "--paths", "a"});
    CLAPP_CHECK(before.has_value());
    CLAPP_CHECK(before->verbose);
    CLAPP_CHECK(std::get<cmd_remove>(before->command).verbose);

    const std::expected<tree_cli, error> neither =
            round_trip<tree_cli>(tree_spec, raw_args{"tree", "remove", "--paths", "a"});
    CLAPP_CHECK(neither.has_value());
    CLAPP_CHECK(!std::get<cmd_remove>(neither->command).verbose);
}

CLAPP_TEST("an omitted optional subcommand is nullopt, not alternative zero") {
    const std::expected<optional_tree_cli, error> got =
            round_trip<optional_tree_cli>(optional_tree_spec, raw_args{"opt"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->command.has_value());
}

CLAPP_TEST("an optional subcommand engages when one is given") {
    const std::expected<optional_tree_cli, error> got = round_trip<optional_tree_cli>(
            optional_tree_spec, raw_args{"opt", "add", "--message", "x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->command.has_value());
    CLAPP_CHECK(std::holds_alternative<cmd_add>(*got->command));
    CLAPP_CHECK(std::get<cmd_add>(*got->command).message == "x");
}

CLAPP_TEST("a subcommand the variant does not know is reported, not ignored") {
    // Only reachable when the two directions have drifted, which is what makes it worth
    // a case: the command tree offers `help`, the variant does not.
    const std::expected<arg_matches, error> matches =
            clapp::parse(optional_tree_spec, raw_args{"opt", "help"});
    CLAPP_CHECK(!matches.has_value() || matches->subcommand_name() == "help");
    if (matches.has_value()) {
        const std::expected<optional_tree_cli, error> got =
                clapp::from_matches<optional_tree_cli>(matches.value());
        CLAPP_CHECK(!got.has_value());
        CLAPP_CHECK(got.error().kind() == error_kind::invalid_subcommand);
        // The rejected name has to reach the message: "some subcommand was not
        // recognized" is not actionable, and an implementation that reported the wrong
        // one would satisfy the kind check.
        CLAPP_CHECK(got.error().render().contains("help"));
    }
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

CLAPP_TEST("a required scalar missing from the matches is reported") {
    // The parser normally catches this first, so the case is built from matches that
    // never went through validation — which is also how a caller who assembled matches
    // by hand would reach it.
    const arg_matches empty{};
    const std::expected<wide_cli, error> got = clapp::from_matches<wide_cli>(empty);
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(got.error().kind() == error_kind::missing_required_argument);
    // `name` is wide_cli's first required scalar. Naming it is what distinguishes this
    // from an implementation whose arg_id_of() drifted: the kind would still be right.
    CLAPP_CHECK(got.error().render().contains("name"));
}

CLAPP_TEST("a required subcommand missing from the matches is reported") {
    const arg_matches empty{};
    const std::expected<tree_cli, error> got = clapp::from_matches<tree_cli>(empty);
    CLAPP_CHECK(!got.has_value());
    // `verbose` comes first and is a flag, so the subcommand row is what fails.
    CLAPP_CHECK(got.error().kind() == error_kind::missing_subcommand);
}

CLAPP_TEST("the first failure wins and the walk stops writing") {
    // `template for` cannot break, so the guard has to skip the remaining bodies. Without
    // it the LAST failure is reported instead of the first — invisible on any struct
    // whose failures all share one kind, which is why order_cli fails with two.
    const arg_matches empty{};
    const std::expected<order_cli, error> got = clapp::from_matches<order_cli>(empty);
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(got.error().kind() == error_kind::missing_subcommand);

    // The same walk on a struct whose first failure is a scalar, to pin down that the
    // kind above came from the ORDER and not from subcommands being special.
    const std::expected<wide_cli, error> scalar_first = clapp::from_matches<wide_cli>(empty);
    CLAPP_CHECK(!scalar_first.has_value());
    CLAPP_CHECK(scalar_first.error().kind() == error_kind::missing_required_argument);
    CLAPP_CHECK(scalar_first.error().render().contains("name"));
}

// ---------------------------------------------------------------------------
// update_from_matches
// ---------------------------------------------------------------------------

CLAPP_TEST("update leaves a field the matches do not mention alone") {
    defaulted_cli existing{.verbose = false, .port = 1234, .tag = "kept"};
    const std::expected<arg_matches, error> matches =
            clapp::parse(defaulted_spec, raw_args{"defaulted", "--port", "9090"});
    CLAPP_CHECK(matches.has_value());

    const std::expected<void, error> updated =
            clapp::update_from_matches(existing, matches.value());
    CLAPP_CHECK(updated.has_value());
    CLAPP_CHECK(existing.port == 9090u);
    CLAPP_CHECK(existing.tag == "kept");
}

CLAPP_TEST("update never reports a missing required argument") {
    wide_cli existing{};
    existing.name = "previous";
    const arg_matches empty{};
    const std::expected<void, error> updated = clapp::update_from_matches(existing, empty);
    CLAPP_CHECK(updated.has_value());
    CLAPP_CHECK(existing.name == "previous");
}

CLAPP_TEST("update refills the alternative the variant already holds") {
    tree_cli existing{};
    existing.command = cmd_add{.message = "old", .force = true, .author = "me"};

    const std::expected<arg_matches, error> matches =
            clapp::parse(tree_spec, raw_args{"tree", "add", "--message", "new"});
    CLAPP_CHECK(matches.has_value());

    const std::expected<void, error> updated =
            clapp::update_from_matches(existing, matches.value());
    CLAPP_CHECK(updated.has_value());
    // In place: the variant was NOT replaced, so a field the new matches never mention
    // is still there. Constructing a fresh alternative instead loses it silently.
    CLAPP_CHECK(std::holds_alternative<cmd_add>(existing.command));
    CLAPP_CHECK(std::get<cmd_add>(existing.command).message == "new");
    CLAPP_CHECK(std::get<cmd_add>(existing.command).author == std::optional<std::string>{"me"});

    // `force` DOES go back to false, and that is clap parity rather than an oversight:
    // clapp::arg_action::set_true declares a default, so the id is present in the
    // matches with value_source::default_value and clap_derive's `gen_updater` — which
    // guards on `contains_id` alone — assigns it too. The escape hatch is a member
    // initializer, which clapp::detail::wins_over_initializer() protects; `force` has
    // none.
    CLAPP_CHECK(!std::get<cmd_add>(existing.command).force);
}

CLAPP_TEST("update replaces the alternative when the subcommand changed") {
    tree_cli existing{};
    existing.command = cmd_add{.message = "old", .force = true, .author = "me"};

    const std::expected<arg_matches, error> matches =
            clapp::parse(tree_spec, raw_args{"tree", "remove", "--paths", "a"});
    CLAPP_CHECK(matches.has_value());

    const std::expected<void, error> updated =
            clapp::update_from_matches(existing, matches.value());
    CLAPP_CHECK(updated.has_value());
    CLAPP_CHECK(std::holds_alternative<cmd_remove>(existing.command));
    CLAPP_CHECK(std::get<cmd_remove>(existing.command).paths.size() == 1);
}

CLAPP_TEST("update does not touch a skipped field") {
    wide_cli existing{};
    existing.note                                   = "mine";
    const std::expected<arg_matches, error> matches = clapp::parse(wide_spec, wide_minimum());
    CLAPP_CHECK(matches.has_value());

    const std::expected<void, error> updated =
            clapp::update_from_matches(existing, matches.value());
    CLAPP_CHECK(updated.has_value());
    CLAPP_CHECK(existing.note == "mine");
    CLAPP_CHECK(existing.name == "demo");
}
