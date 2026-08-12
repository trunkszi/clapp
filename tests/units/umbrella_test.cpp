/**
 * \file
 * \brief Guards the umbrella header `<clapp/clapp.hpp>`.
 *
 * Without this target nothing in the build tree includes `clapp/clapp.hpp`, so a
 * broken umbrella header would compile fine in CI and only break downstream users —
 * exactly the people who cannot debug it. The M1 integration stage had to verify the
 * header out-of-band for this reason; this file closes that gap permanently.
 *
 * What it checks:
 *
 * 1. The umbrella header compiles on its own, as the very first include.
 * 2. Including it twice is a no-op (`#pragma once` is present and effective).
 * 3. Every type each module promises to export is actually reachable through it,
 *    so dropping an `#include` from the umbrella breaks the build here rather than
 *    silently shrinking the public surface.
 * 4. A second translation unit (`umbrella_second_tu.cpp`) links against this one,
 *    catching ODR and duplicate-symbol faults that a single-TU syntax check cannot.
 *
 * \note This test lives directly under `tests/units/` because the umbrella spans
 *       every module rather than belonging to one of them.
 */

// Deliberately first, and deliberately alone — this must be self-sufficient.
#include <clapp/clapp.hpp>

// Idempotency: a second include must be inert.
#include <clapp/clapp.hpp>

#include "support/check.hpp"

#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace std::string_view_literals;

// ===========================================================================
// Exported surface — one static_assert per type the umbrella must reach.
//
// These are compile-time reachability checks, not behavior tests; behavior lives
// in the per-module unit tests. If a module's include disappears from the umbrella,
// the corresponding line here stops compiling.
// ===========================================================================

// --- clapp::lex (M1) -------------------------------------------------------
static_assert(std::is_class_v<clapp::os_str>);
static_assert(std::is_class_v<clapp::os_string>);
static_assert(std::is_class_v<clapp::parsed_arg>);
static_assert(std::is_class_v<clapp::short_flags>);
static_assert(std::is_class_v<clapp::raw_args>);
static_assert(std::is_class_v<clapp::arg_cursor>);

// --- clapp::meta (M0) ------------------------------------------------------
static_assert(std::is_class_v<clapp::arg_attr>);
static_assert(std::is_class_v<clapp::command_attr>);
static_assert(std::is_class_v<clapp::value_attr>);
static_assert(std::is_class_v<clapp::cstr<16>>);
static_assert(clapp::action::infer == clapp::action{});

/**
 * The reflection layer is a hard requirement, not an optional extra. If `config.hpp` reports
 * otherwise, the build must stop here rather than silently degrade.
 */
static_assert(CLAPP_HAS_REFLECTION, "clapp requires C++26 static reflection");
static_assert(CLAPP_HAS_ANNOTATIONS, "clapp requires P3394 annotations");
static_assert(CLAPP_HAS_EXPANSION_STATEMENTS, "clapp requires P1306 expansion statements");

// --- clapp::util (M2) ------------------------------------------------------
static_assert(std::is_class_v<clapp::flat_map<int, int>>);
static_assert(std::is_class_v<clapp::flat_set<int>>);
static_assert(std::is_class_v<clapp::digraph<4>>);
static_assert(std::is_class_v<clapp::digraph_view>);
static_assert(std::is_class_v<clapp::arg_id>);
static_assert(std::is_class_v<clapp::id_table<8>>);
static_assert(std::is_class_v<clapp::any_value>);
static_assert(std::is_class_v<clapp::any_id>);
static_assert(std::is_class_v<clapp::word_cursor>);
static_assert(clapp::string_view_range<std::array<std::string_view, 2>>);
static_assert(clapp::help_id.name() == "help"sv);

// --- clapp::builder (M2) ---------------------------------------------------
// One line per header, so that a header dropped from the umbrella stops compiling
// here instead of silently shrinking the public surface.
static_assert(clapp::name_of(clapp::arg_action::set_true) == "set-true"sv);  // action
static_assert(clapp::value_range::single().is_fixed());                      // value_range
static_assert(clapp::name_of(clapp::value_hint::any_path) == "any-path"sv);  // value_hint
static_assert(std::is_class_v<clapp::possible_value>);                       // possible_value
static_assert(clapp::parsable<int>);                                         // value_parser
static_assert(std::is_class_v<clapp::parser_vtable>);
static_assert(std::is_class_v<clapp::styles>);  // styling
static_assert(clapp::styles::plain().get(clapp::style_class::header).is_plain());
static_assert(std::is_class_v<clapp::arg_spec>);  // arg
static_assert(std::is_class_v<clapp::arg_builder>);
static_assert(std::is_class_v<clapp::group_spec>);  // arg_group
static_assert(std::is_class_v<clapp::group_builder>);
static_assert(std::is_class_v<clapp::command_spec>);  // command
static_assert(std::is_class_v<clapp::command_builder>);

// The builder/spec split is the whole point of ADR-0005, so pin it here too: only the
// frozen half may live in static storage.
static_assert(std::is_trivially_copyable_v<clapp::command_spec>);
static_assert(std::is_trivially_copyable_v<clapp::arg_spec>);
static_assert(std::is_trivially_copyable_v<clapp::group_spec>);

// --- clapp::output (M3 + M5) -----------------------------------------------
// styled_str.hpp is the renderable-text substrate the error model is built on; the
// four headers that turn it into a help screen landed in M5.
//
// One line per header, for the reason the `arg_matcher` note below spells out:
// help.hpp includes usage.hpp, render.hpp and textwrap.hpp, so dropping any of the
// three from the umbrella would leave the whole build green. These assertions are
// the only place in the tree that would notice.
//
// \note The three that need a *frozen command tree* — render_usage(),
//       long_help_exists() and render_version() — cannot be asserted here: the only
//       `command_spec` in this file is derived in the M4 section further down. They
//       are asserted there instead, under the same "one line per header" rule.
static_assert(std::is_class_v<clapp::styled_span>);  // styled_str
static_assert(std::is_class_v<clapp::styled_str>);

// textwrap.hpp — terminal cells, which are neither bytes nor code points.
static_assert(clapp::char_display_width(U'a') == 1);
static_assert(clapp::char_display_width(U'你') == 2);
static_assert(clapp::char_display_width(U'́') == 0);
static_assert(clapp::display_width("你好!") == 5);
static_assert(clapp::default_terminal_width == 100);
static_assert(clapp::unbounded_width == static_cast<std::size_t>(-1));
static_assert(std::is_class_v<clapp::line_wrapper>);
static_assert(clapp::line_wrapper{20}.hard_width() == 20);
static_assert(clapp::wrap("12345 12345 12345"sv, 11) == "12345 12345\n12345"sv);
static_assert(clapp::indent("a\nb"sv, "* "sv, "  "sv) == "* a\n  b"sv);
static_assert(clapp::trim_end("a  "sv) == "a"sv);
static_assert(clapp::parse_terminal_width("80"sv) == std::optional<std::size_t>{80});
static_assert(clapp::parse_terminal_width("0"sv) == std::nullopt);
static_assert(clapp::resolve_wrap_width(std::nullopt, std::nullopt, std::nullopt) ==
              clapp::default_terminal_width);
static_assert(clapp::resolve_wrap_width(std::optional<std::size_t>{0},
                                        std::nullopt,
                                        std::nullopt) == clapp::unbounded_width);

// render.hpp — clapp::styled_str to bytes, plus the colour policy.
static_assert(clapp::render_style(clapp::style{}.bold()) == "\x1b[1m"sv);
static_assert(clapp::render_style_reset(clapp::style{}).empty());
static_assert(clapp::render_style_reset(clapp::style{}.bold()) == clapp::ansi_reset);
static_assert(clapp::render_plain(clapp::styled_str{}).empty());
static_assert(clapp::render_ansi(clapp::styled_str{}, clapp::styles::styled()).empty());
static_assert(clapp::render(clapp::styled_str{}, clapp::styles::styled(), true).empty());

/**
 * The environment seam, asserted in **both** directions: a concept satisfied by
 * everything would pass the positive half alone.
 */
static_assert(clapp::env_lookup<decltype([](std::string_view) {
    return std::optional<std::string_view>{};
})>);
static_assert(!clapp::env_lookup<int>);
static_assert(!clapp::resolve_color(clapp::color_choice::never, clapp::color_env{}));
static_assert(clapp::resolve_color(clapp::color_choice::always, clapp::color_env{}));

// help.hpp — the request descriptor. The renderer itself needs a tree; see the M4
// section.
static_assert(std::is_class_v<clapp::help_style>);
static_assert(clapp::help_style::long_form().use_long);
static_assert(!clapp::help_style::short_form().use_long);

// --- clapp::error (M3) -----------------------------------------------------
// One line per header. The `name_of` probes double as a check that the umbrella has
// not accidentally hidden an overload: `name_of` is overloaded across six unrelated
// headers now (arg_action, value_hint, error_kind, context_kind, value_source,
// matches_error_kind, arg_identifier), and each has to resolve on its own enum.
static_assert(clapp::name_of(clapp::error_kind::unknown_argument)  // error_kind
              == "unknown-argument"sv);
static_assert(clapp::all_error_kinds.size() == clapp::error_kind_count);
static_assert(clapp::describe(clapp::error_kind::display_help) == std::nullopt);

static_assert(clapp::name_of(clapp::context_kind::invalid_arg) == "invalid-arg"sv);  // context
static_assert(clapp::all_context_kinds.size() == clapp::context_kind_count);
static_assert(std::is_class_v<clapp::cow_str>);
static_assert(std::is_class_v<clapp::context_value>);

static_assert(std::is_class_v<clapp::error>);  // error
static_assert(clapp::error_format::rich != clapp::error_format::kind_only);

// --- clapp::parser (M3) ----------------------------------------------------
static_assert(clapp::name_of(clapp::value_source::command_line)  // value_source
              == "command-line"sv);
static_assert(clapp::all_value_sources.size() == clapp::value_source_count);

static_assert(std::is_class_v<clapp::value_group>);  // matched_arg
static_assert(std::is_class_v<clapp::matched_arg>);

static_assert(std::is_class_v<clapp::arg_matches>);  // arg_matches
static_assert(std::is_class_v<clapp::matches_error>);
static_assert(clapp::name_of(clapp::matches_error_kind::downcast) == "downcast"sv);

/**
 * \note `arg_matcher` and `validator` live entirely in `clapp::detail`: they are the
 *       parser's accumulator and its post-pass, not API a caller reaches for — the one
 *       public entry point of the whole module is `clapp::parse`. They are asserted
 *       here anyway, because the umbrella's contract is about *headers*, not about
 *       namespaces: if `<clapp/parser/arg_matcher.hpp>` were dropped from the umbrella
 *       these lines are the only thing in the build tree that would notice, since
 *       parse.hpp includes it transitively and would keep compiling.
 */
static_assert(std::is_class_v<clapp::detail::arg_matcher>);  // arg_matcher
static_assert(std::is_class_v<clapp::detail::pending_arg>);
static_assert(clapp::detail::name_of(clapp::detail::arg_identifier::long_) == "long"sv);
static_assert(!clapp::detail::is_named(clapp::detail::arg_identifier::index));

static_assert(std::is_class_v<clapp::detail::validator>);  // validator
static_assert(std::is_class_v<clapp::detail::usage_renderer>);

/**
 * The module's single public entry point, pinned by signature rather than by
 * `is_class_v` — it is a function, and its return type is the whole error model.
 */
static_assert(  // parse
        std::same_as<decltype(clapp::parse(std::declval<const clapp::command_spec&>(),
                                           std::declval<const clapp::raw_args&>())),
                     std::expected<clapp::arg_matches, clapp::error>>);

/**
 * ADR-0005's boundary, stated as an assertion rather than as prose: the frozen tree is
 * a constant, the matches are not and cannot be. `arg_matches` owns clapp::any_value s,
 * whose destructor is not `constexpr`, so `clapp::parse` can never appear in a constant
 * expression. If this pair ever flips, the compile/runtime boundary at `command_of<T>()`
 * has moved.
 */
static_assert(std::is_trivially_copyable_v<clapp::command_spec>);
static_assert(!std::is_trivially_destructible_v<clapp::arg_matches>);

// --- clapp::meta, derive layer (M4) ----------------------------------------
//
// One line per header, with the same caveat the `arg_matcher` note above spells out:
// `parse.hpp` includes `deduce.hpp` and `from_matches.hpp`, so dropping either one from
// the umbrella would leave the whole build green. These assertions are the only place in
// the tree that would notice — which is the entire reason the umbrella's contract is
// stated header by header rather than inherited from whoever happens to include whom.

// deduce.hpp — field deduction represented as data.
static_assert(std::is_class_v<clapp::deduced_arg>);  // deduce
static_assert(clapp::is_argument(clapp::field_shape::flag));
static_assert(!clapp::is_argument(clapp::field_shape::flattened));
static_assert(clapp::enumerates_values(clapp::field_shape::enumeration));
static_assert(clapp::store_for(clapp::field_shape::counter) == clapp::store_kind::count);
static_assert(clapp::name_of(clapp::field_shape::optional_scalar) == "optional-scalar"sv);
static_assert(clapp::name_of(clapp::store_kind::optional_single) == "optional-single"sv);

/**
 * A value-initialized clapp::deduced_arg describes a *skipped* field, not a `bool` flag.
 * That is why clapp::field_shape orders its four structural rows first, and it is the
 * same discipline as `action::infer` sitting at enumerator zero.
 */
static_assert(!clapp::deduced_arg{}.is_argument());
static_assert(clapp::deduced_arg{}.store == clapp::store_kind::none);

/**
 * The acceptance artifact of the whole milestone, reached through `<clapp/clapp.hpp>`
 * and nothing else: a plain annotated struct, and the command tree `consteval` derives
 * from it. No macro, no codegen step, no build plugin.
 */
struct[[= clapp::cmd{.version = "4.0", .about = "Umbrella derive probe"}]] umbrella_probe_cli {
    [[= clapp::arg{.short_ = 'n', .long_ = "name", .help = "Who to greet"}]] std::string name;
    [[= clapp::arg{.short_ = 'v'}]] bool verbose;
    std::optional<unsigned> retries;
    std::vector<std::string> tags;
};

/** A second level, so the subcommand walk is exercised rather than only the flat one. */
struct[[= clapp::cmd{.name = "child"}]] umbrella_probe_child {
    bool force;
};
struct umbrella_probe_tree {
    [[= clapp::subcommand{}]] std::variant<umbrella_probe_child> command;
};

// from_matches.hpp — the reverse direction, matches back into the struct.
static_assert(clapp::derivable_command<umbrella_probe_cli>);  // from_matches
static_assert(!clapp::derivable_command<int>);
static_assert(std::same_as<decltype(clapp::from_matches<umbrella_probe_cli>(
                                   std::declval<const clapp::arg_matches&>())),
                           std::expected<umbrella_probe_cli, clapp::error>>);
static_assert(std::same_as<decltype(clapp::update_from_matches<umbrella_probe_cli>(
                                   std::declval<umbrella_probe_cli&>(),
                                   std::declval<const clapp::arg_matches&>())),
                           std::expected<void, clapp::error>>);
static_assert(clapp::meta::rename_all_of(^^umbrella_probe_cli) == clapp::naming::kebab);
static_assert(clapp::meta::subcommand_name_of(^^umbrella_probe_child) == "child"sv);

// parse.hpp — the concept, the forward direction, and the entry points.
static_assert(clapp::parsable_command<umbrella_probe_cli>);  // parse
static_assert(clapp::parsable_command<umbrella_probe_tree>);
/**
 * The negative half. Without it a concept answering `true` unconditionally would
 * satisfy the two lines above, and the concept is documented as never hard-erroring —
 * so it must be assertable in *both* directions, here as well as in the unit test.
 */
static_assert(!clapp::parsable_command<int>);

/**
 * The derived tree, frozen into static storage exactly as ADR-0005 requires. If this
 * declaration stops compiling, the derive layer no longer reaches the umbrella,
 * whatever else still does.
 */
static constexpr clapp::command_spec umbrella_derived = clapp::command_of<umbrella_probe_cli>();

static_assert(umbrella_derived.get_version() == "4.0"sv);
static_assert(umbrella_derived.get_about() == "Umbrella derive probe"sv);
static_assert(std::is_trivially_copyable_v<decltype(umbrella_derived)>);

/**
 * \warning **Existence is asserted by dereferencing, never by `!= nullptr`.** Under the
 *          `ubsan` preset GCC 16.1.0 refuses to fold *any* pointer comparison inside a
 *          constant expression, so `find_arg("name") != nullptr` is rejected with
 *          "is not a constant expression" and a page of the frozen tree's initializer —
 *          measured here, on this file, before these lines were rewritten. A `->` costs
 *          nothing and proves strictly more: a null pointer cannot be dereferenced in a
 *          constant expression either, so reaching the value at all is the existence
 *          check. Same trap as the pointer-identity probe at the bottom of this file,
 *          which is a runtime `CLAPP_CHECK` for exactly this reason.
 */
static_assert(umbrella_derived.find_arg("name")->is_required_set());
static_assert(umbrella_derived.find_arg("name")->get_short() == 'n');
static_assert(umbrella_derived.find_arg("verbose")->get_action() == clapp::arg_action::set_true);
static_assert(umbrella_derived.find_arg("tags")->get_action() == clapp::arg_action::append);
static_assert(!umbrella_derived.find_arg("retries")->is_required_set());

/**
 * The injected `--help` / `--version`, which the derive layer inherits from
 * clapp::command_builder::freeze() rather than from any field the user wrote.
 */
static_assert(umbrella_derived.find_arg("help")->get_action() == clapp::action::help);
static_assert(umbrella_derived.find_arg("version")->get_action() == clapp::action::version);

/**
 * --- clapp::output, the half that needs a frozen tree (M5) ----------------
 *
 * Deliberately down here rather than with the rest of the output block: these three
 * take a `command_spec`, and `umbrella_derived` is the first one this file has. Each
 * line names a different header, so a header dropped from the umbrella stops
 * compiling at exactly one of them.
 *
 * \note The `Usage:` line is the byte-for-byte contract the scenario e2e suite also
 *       pins; it is repeated here so a broken umbrella fails before ten example
 *       binaries do.
 */
static_assert(clapp::render_usage(umbrella_derived)->to_string()  // usage
              == "Usage: umbrella-probe-cli [OPTIONS] --name <name>"sv);
static_assert(clapp::render_usage_body(umbrella_derived)->to_string() ==
              "umbrella-probe-cli [OPTIONS] --name <name>"sv);

static_assert(clapp::render_version(umbrella_derived, false).to_string()  // help
              == "umbrella-probe-cli 4.0\n"sv);
/**
 * Nothing in this struct carries `long_about`, `long_help` or a `hide_*` switch, so
 * `--help` and `-h` are the same page and `Print help` stays unqualified.
 */
static_assert(!clapp::long_help_exists(umbrella_derived));
static_assert(clapp::render_help(umbrella_derived).to_string() ==
              "Umbrella derive probe\n"
              "\n"
              "Usage: umbrella-probe-cli [OPTIONS] --name <name>\n"
              "\n"
              "Options:\n"
              "  -n, --name <name>        Who to greet\n"
              "  -v, --verbose            \n"
              "      --retries <retries>  \n"
              "      --tags <tags>...     \n"
              "  -h, --help               Print help\n"
              "  -V, --version            Print version\n"sv);

/** The subcommand walk, one level down. */
static constexpr clapp::command_spec umbrella_derived_tree =
        clapp::command_of<umbrella_probe_tree>();
static_assert(umbrella_derived_tree.find_subcommand("child")->find_arg("force")->get_action() ==
              clapp::arg_action::set_true);

/**
 * command_for_update() is a different tree from command_of(), not a synonym: an
 * incremental parse requires nothing, because the caller already holds a `T`. One
 * assertion, on the one setting that differs for this struct.
 */
static constexpr clapp::command_spec umbrella_derived_update =
        clapp::command_for_update<umbrella_probe_cli>();
static_assert(!umbrella_derived_update.find_arg("name")->is_required_set());

/**
 * The name collision M4 introduced, pinned in a TU that includes the umbrella:
 * `clapp::parse` now spells both M3's `parse(spec, raw)` and the derive layer's
 * `parse<T>(argc, argv)`. `decltype` is unevaluated, so this is overload resolution
 * alone — nothing is parsed and no command line is needed.
 */
static_assert(
        std::same_as<decltype(clapp::parse<umbrella_probe_cli>(0, static_cast<char**>(nullptr))),
                     umbrella_probe_cli>);
static_assert(std::same_as<
              decltype(clapp::try_parse<umbrella_probe_cli>(0, static_cast<char**>(nullptr))),
              std::expected<umbrella_probe_cli, clapp::error>>);

// --- Version macros --------------------------------------------------------
static_assert(CLAPP_VERSION_MAJOR >= 0);
static_assert(sizeof(CLAPP_VERSION_STRING) > 1);

// ===========================================================================
// ADR-0005: the command tree is data, and that data is shared between TUs
//
// `freeze()` promotes every string and array through std::define_static_string /
// std::define_static_array. Those are specified to hand back the *same* object for
// equal inputs, which is what stops a header-only library from paying for one private
// copy of every command name in every translation unit that includes the header.
//
// That guarantee is verified below rather than assumed. Measured on GCC 16.1.0 with
// this file linked against umbrella_second_tu.cpp, which freezes an identical builder
// chain of its own: the promoted name, the promoted argument array and the promoted
// subcommand array all compare pointer-equal across the two TUs.
// ===========================================================================

/** Frozen independently in `umbrella_second_tu.cpp` from an identical builder chain. */
const clapp::command_spec& umbrella_second_tu_command_spec();

/**
 * \note `static` (internal linkage) so the twin definition in the second TU raises no
 *       ODR question at all. Only the builder *inputs* need to match for the promoted
 *       storage to be shared; the functions need not be the same function.
 */
static consteval clapp::command_spec make_storage_probe() {
    clapp::command_builder root("umbrella-probe");
    std::move(root)
            .about("Cross-TU storage probe")
            .version("1.0")
            .arg(clapp::arg_builder("input").long_("input").required())
            .subcommand(clapp::command_builder("child").about("Nested"));
    return root.freeze();
}

/**
 * The acceptance test for the whole milestone: a `static constexpr` command tree.
 *
 * If this declaration ever stops compiling, `freeze()` has failed, regardless of what
 * else in the builder still works.
 */
static constexpr clapp::command_spec storage_probe = make_storage_probe();

static_assert(storage_probe.get_name() == "umbrella-probe"sv);
static_assert(storage_probe.get_version() == "1.0"sv);
static_assert(storage_probe.find_arg("input")->is_required_set());
static_assert(storage_probe.find_subcommand("child")->get_about() == "Nested"sv);

// ===========================================================================
// Cross-module usability
//
// The umbrella's job is not just to make names visible but to make them work
// together. This exercises the lex types through the umbrella alone.
// ===========================================================================

/** Defined in umbrella_second_tu.cpp; linking proves the header is ODR-clean. */
int umbrella_second_tu_probe();

CLAPP_TEST("umbrella header exposes a working lexer") {
    const clapp::os_string argv0{"prog"};
    const clapp::os_string a1{"--name=value"};
    const clapp::os_string a2{"-abc"};
    const clapp::os_string a3{"--"};

    const clapp::os_str items[] = {argv0, a1, a2, a3};
    const clapp::raw_args raw{std::span<const clapp::os_str>{items}};

    auto cur = raw.cursor();
    CLAPP_CHECK(!raw.is_end(cur));

    const auto bin = raw.next(cur);
    CLAPP_CHECK(bin.has_value());

    const auto longopt = raw.next(cur);
    CLAPP_CHECK(longopt.has_value());
    CLAPP_CHECK(longopt->is_long());

    const auto shortopt = raw.next(cur);
    CLAPP_CHECK(shortopt.has_value());
    CLAPP_CHECK(shortopt->is_short());

    const auto escape = raw.next(cur);
    CLAPP_CHECK(escape.has_value());
    CLAPP_CHECK(escape->is_escape());

    CLAPP_CHECK(raw.is_end(cur));
}

CLAPP_TEST("umbrella header is ODR-clean across translation units") {
    CLAPP_CHECK(umbrella_second_tu_probe() == 42);
}

/**
 * \note Runtime, not `static_assert`, on purpose. Comparing two pointers inside a
 *       constant expression is rejected under the `ubsan` preset on GCC 16.1.0
 *       ("... is not a constant expression"), which would break `-fsanitize=undefined`
 *       for every translation unit that includes this file.
 *
 * \warning **This test probes an unspecified property, deliberately.** P3491 does not
 *          require two `std::define_static_string` / `define_static_array` calls with
 *          equal contents to return the same pointer — `any_value.hpp` says so in as
 *          many words, which is why clapp::any_id compares characters and never
 *          addresses. A conforming implementation that does not intern equal contents
 *          fails the pointer half below with no clapp defect whatsoever. Measured on
 *          this machine: GCC 16.1.0 and clang-p2996 0.0.0-p2996.5cc3eb319 both dedupe
 *          in all five shapes probed (literal vs literal, literal vs `std::string`,
 *          literal vs a char-by-char-built string, and `define_static_array` from a
 *          `std::array` vs from an equal `std::vector`). The assertions stay because
 *          the property is worth *knowing*: it is what keeps a frozen tree's `.rodata`
 *          from growing linearly in the number of translation units. Triage a failure
 *          here as a toolchain change first, not as a clapp regression — the content
 *          checks below are the ones that would indict clapp.
 */
CLAPP_TEST("freeze() shares promoted storage across translation units (unspecified)") {
    const clapp::command_spec& other = umbrella_second_tu_command_spec();

    // Each TU owns its own `static constexpr` object...
    CLAPP_CHECK(&storage_probe != &other);

    // ...and the contents agree. This half *is* guaranteed, and it is what a clapp
    // defect would break.
    CLAPP_CHECK(storage_probe.get_name() == other.get_name());
    CLAPP_CHECK(storage_probe.get_about() == other.get_about());
    CLAPP_CHECK(storage_probe.get_arguments().size() == other.get_arguments().size());
    CLAPP_CHECK(storage_probe.get_subcommands().size() == other.get_subcommands().size());
    CLAPP_CHECK(storage_probe == other);

    // The unspecified half: everything they point at is the *one* promoted copy.
    CLAPP_CHECK(storage_probe.get_name().data() == other.get_name().data());
    CLAPP_CHECK(storage_probe.get_about()->data() == other.get_about()->data());
    CLAPP_CHECK(storage_probe.get_arguments().data() == other.get_arguments().data());
    CLAPP_CHECK(storage_probe.get_subcommands().data() == other.get_subcommands().data());
}
