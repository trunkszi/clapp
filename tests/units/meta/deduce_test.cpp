#include <clapp/meta/deduce.hpp>

#include "support/check.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>


using namespace std::string_view_literals;

namespace m = clapp::meta;

using clapp::action;
using clapp::arg_attr;
using clapp::deduced_arg;
using clapp::field_shape;
using clapp::store_kind;
using clapp::tri;
using clapp::value_range;

/**
 * \warning **`^^` cannot be applied to a name introduced by a using-declaration.**
 *          The declarations above make `field_shape` such a name, so the unqualified
 *          `^^` on it is rejected outright — GCC 16.1.0 says `'^^' cannot be applied
 *          to a using-declaration`. Every reflection below therefore spells the type
 *          through its own namespace, `^^clapp::field_shape`, while the unqualified
 *          names stay usable in ordinary expressions — which is why they are kept.
 *
 * The same rule bites `std::uint8_t`: libstdc++ introduces it into namespace `std`
 * with `using ::uint8_t;`, so `^^std::uint8_t` is rejected too. An *alias*-
 * declaration is a different thing and reflects fine, hence:
 */
using u8 = std::uint8_t;

// ===========================================================================
// Types under reflection
// ===========================================================================

namespace {

enum class color { always [[maybe_unused]], auto_ [[maybe_unused]], never [[maybe_unused]] };
enum unscoped_mode { fast [[maybe_unused]], slow [[maybe_unused]] };

struct cmd_add {};
struct cmd_commit {};

/**
 * A type *alias* for the variant, so the tests exercise trap 3 in CLAUDE.md:
 * `template_arguments_of` on an alias throws unless `dealias` runs first.
 */
using subcommands = std::variant<cmd_add, cmd_commit>;
using int_list    = std::vector<int>;
using cv_int_list = const std::vector<int>;

struct nested_opts {
    [[maybe_unused]] int inner = 1;
};

/**
 * One field per row of the deduction table. Field *names* are used to look these
 * up, never indices, so inserting a row here cannot silently renumber the others.
 */
struct cli {
    [[maybe_unused]] bool verbose;                  // ROW 1
    [[maybe_unused]] std::optional<bool> colorize;  // ROW 2

    [[= clapp::arg{.act = clapp::action::count}]] [[maybe_unused]] std::uint8_t v;  // ROW 3

    [[maybe_unused]] std::string name;  // ROW 4

    [[= clapp::arg{.default_value = "8080"}]] [[maybe_unused]] unsigned port_from_attr;  // ROW 5

    [[maybe_unused]] unsigned port_from_initializer = 8080;  // ROW 6

    [[maybe_unused]] std::optional<std::string> config;                    // ROW 7
    [[maybe_unused]] std::optional<std::optional<std::string>> tri_state;  // ROW 8
    [[maybe_unused]] std::vector<std::string> includes;                    // ROW 9a
    [[maybe_unused]] std::deque<int> queue;                                // ROW 9b
    [[maybe_unused]] std::optional<std::vector<int>> maybe_list;           // ROW 10
    [[maybe_unused]] std::array<int, 3> coords;                            // ROW 11
    [[maybe_unused]] std::pair<int, char> key_value;                       // ROW 12
    [[maybe_unused]] color mode;                                           // ROW 13

    [[= clapp::subcommand{}]] [[maybe_unused]] subcommands command;  // ROW 14

    [[= clapp::flatten{}]] [[maybe_unused]] nested_opts shared;  // ROW 16

    [[= clapp::skip{}]] [[maybe_unused]] int internal;  // ROW 17

    /** The counter row must not fire on a non-integer, however the annotation reads. */
    [[= clapp::arg{.act = clapp::action::count}]] [[maybe_unused]] std::string not_a_counter;

    /** Explicit overrides, for the precedence checks. */
    [[= clapp::arg{.act = clapp::action::append}]] [[maybe_unused]] std::string appended;

    [[= clapp::arg{.required = clapp::tri::no}]]
    [[maybe_unused]] std::string not_required_after_all;

    [[= clapp::arg{.required = clapp::tri::yes}]] [[maybe_unused]] bool required_flag;

    /**
     * A member initializer whose value equals value-initialization. The point of
     * `has_default_member_initializer` is that this is still distinguishable from
     * `int plain;`, which no comparison of *values* could achieve.
     */
    [[maybe_unused]] int zero_but_initialized = 0;

    /**
     * An initializer written as an empty brace pair, the spelling that expresses
     * "default to the empty string" — which `.default_value = ""` cannot, because
     * clapp::cstr gives absent and empty the same length of 0.
     */
    [[maybe_unused]] std::string empty_by_initializer{};

    [[maybe_unused]] int no_initializer;
};

/**
 * ROW 15 lives in its own struct: two subcommand members in one command is not a
 * thing a real CLI does, and keeping them apart makes the assertions readable.
 */
struct cli_optional_subcommand {
    [[= clapp::subcommand{}]] [[maybe_unused]] std::optional<subcommands> command;
};

// ---------------------------------------------------------------------------
// Field lookup by name.
//
// Consteval-only, so the GCC 16.1.0 wrong-code note about constexpr functions
// called with string-literal arguments does not apply: this can never run at
// runtime, and every caller is inside a static_assert.
// ---------------------------------------------------------------------------

consteval std::meta::info field(std::meta::info type, std::string_view name) {
    for (const std::meta::info member :
         std::meta::nonstatic_data_members_of(type, std::meta::access_context::current())) {
        if (std::meta::identifier_of(member) == name) return member;
    }
    std::abort();
}

consteval deduced_arg deduce_field(std::string_view name) {
    return m::deduce_member(field(^^cli, name));
}

consteval field_shape shape_field(std::string_view name) { return m::shape_of(field(^^cli, name)); }

// ===========================================================================
// Type-shape detection — the primitives the table rests on
// ===========================================================================

static_assert(m::is_optional(^^std::optional<int>));
static_assert(!m::is_optional(^^int));
static_assert(m::is_vector(^^std::vector<int>));
static_assert(m::is_deque(^^std::deque<int>));
static_assert(m::is_sequence(^^std::vector<int>));
static_assert(m::is_sequence(^^std::deque<int>));
static_assert(!m::is_sequence(^^std::array<int, 2>));
static_assert(m::is_fixed_array(^^std::array<int, 2>));
static_assert(m::is_pair(^^std::pair<int, char>));
static_assert(m::is_variant(^^std::variant<cmd_add>));
static_assert(m::is_boolean(^^bool));
static_assert(!m::is_boolean(^^int));
static_assert(m::is_enumeration(^^color));
static_assert(m::is_enumeration(^^unscoped_mode));
static_assert(!m::is_enumeration(^^int));

// Trap 3: an alias must be canonicalized before its template arguments are read.
// These pass only because canonical_type() runs; mutating it to `return type;`
// fails 7 of the assertions below on GCC and 5 on clang, which is what makes them
// a guard rather than a restatement.
//
// They do *not* discriminate the individual `dealias` calls inside canonical_type()
// — `std::meta::remove_cv` already yields the underlying type of an alias on both
// compilers, so dropping the dealias keeps everything green. Do not read these as
// pinning the dealias down; see the note on canonical_type().
static_assert(m::is_variant(^^subcommands));
static_assert(m::is_vector(^^int_list));
static_assert(m::is_sequence(^^cv_int_list));  // top-level const stripped too
static_assert(m::shape_of_type(^^subcommands) == field_shape::subcommand_set);
static_assert(m::shape_of_type(^^cv_int_list) == field_shape::sequence);

// `bool` is integral but must never be a counter; `std::uint8_t` — which is
// `unsigned char` here, and is clap's own CountType — must be.
static_assert(!m::is_countable(^^bool));
static_assert(m::is_countable(^^u8));
static_assert(m::is_countable(^^int));
static_assert(!m::is_countable(^^std::string));

static_assert(m::sole_type_argument(^^std::optional<int>) == ^^int);
static_assert(m::sole_type_argument(^^std::vector<char>) == ^^char);
static_assert(m::array_extent_of(^^std::array<int, 3>) == 3);
static_assert(m::array_extent_of(^^std::array<char, 0>) == 0);
static_assert(m::pair_types_of (^^std::pair<int, char>)[0] == (^^int));
static_assert(m::pair_types_of (^^std::pair<int, char>)[1] == (^^char));

// `type_of` on a member keeps top-level const; canonical_type must strip it.
static_assert(m::canonical_type(^^cv_int_list) == (^^std::vector<int>));

// ===========================================================================
// One block per field-deduction rule.
// ===========================================================================

// --- ROW 1 · bool -> set_true, 0, not required ---------------------------
static_assert(shape_field("verbose") == field_shape::flag);
static_assert(deduce_field("verbose").act == action::set_true);
static_assert(deduce_field("verbose").num_args == value_range::empty());
static_assert(!deduce_field("verbose").required);
static_assert(deduce_field("verbose").store == store_kind::flag);
static_assert(deduce_field("verbose").is_argument());
static_assert(deduce_field("verbose").is_resolved());

// --- ROW 2 · std::optional<bool> -> set_true, 0, not required ------------
static_assert(shape_field("colorize") == field_shape::optional_flag);
static_assert(deduce_field("colorize").act == action::set_true);
static_assert(deduce_field("colorize").num_args == value_range::empty());
static_assert(!deduce_field("colorize").required);
// The whole point of the row: a distinct store, so "absent" survives as nullopt
// instead of collapsing into "explicitly false".
static_assert(deduce_field("colorize").store == store_kind::optional_flag);
static_assert(deduce_field("colorize").store != deduce_field("verbose").store);

// --- ROW 3 · integer + .act = count -> count, 0, not required ------------
static_assert(shape_field("v") == field_shape::counter);
static_assert(deduce_field("v").act == action::count);
static_assert(deduce_field("v").num_args == value_range::empty());
static_assert(!deduce_field("v").required);
static_assert(deduce_field("v").store == store_kind::count);

// The arity comes from the *action*, not from the integer type: without the
// action-before-type rule in deduce(), an integer would claim `single()` and
// `-vvv` would try to consume three operands.
static_assert(m::deduce(field_shape::scalar, 0, arg_attr{.act = action::count}, false).num_args ==
              value_range::empty());

// A count annotation on a non-integer must not reach the counter row.
static_assert(shape_field("not_a_counter") == field_shape::scalar);
static_assert(deduce_field("not_a_counter").store == store_kind::single);

// --- ROW 4 · T (parsable scalar) -> set, 1, REQUIRED ---------------------
static_assert(shape_field("name") == field_shape::scalar);
static_assert(deduce_field("name").act == action::set);
static_assert(deduce_field("name").num_args == value_range::single());
static_assert(deduce_field("name").required);
static_assert(deduce_field("name").store == store_kind::single);

// --- ROW 5 · T + .default_value -> set, 1, not required ------------------
static_assert(shape_field("port_from_attr") == field_shape::scalar);
static_assert(deduce_field("port_from_attr").act == action::set);
static_assert(deduce_field("port_from_attr").num_args == value_range::single());
static_assert(!deduce_field("port_from_attr").required);

// --- ROW 6 · T + member initializer -> set, 1, not required --------------
//
// This row is the one that could not be delivered if reflection could not see a
// default member initializer. It can: std::meta::has_default_member_initializer is
// implemented by GCC 16.1.0 and by clang-p2996 0.0.0-p2996.5cc3eb319 alike.
static_assert(m::has_member_initializer(field(^^cli, "port_from_initializer")));
static_assert(!m::has_member_initializer(field(^^cli, "no_initializer")));
static_assert(shape_field("port_from_initializer") == field_shape::scalar);
static_assert(deduce_field("port_from_initializer").act == action::set);
static_assert(deduce_field("port_from_initializer").num_args == value_range::single());
static_assert(!deduce_field("port_from_initializer").required);

// The mutation that must fail: `name` and `port_from_initializer` differ *only* in
// having an initializer, so if has_member_initializer() were pinned to `false` this
// pair would stop disagreeing. Same shape, same action, same arity, opposite
// requiredness.
static_assert(shape_field("name") == shape_field("port_from_initializer"));
static_assert(deduce_field("name").required != deduce_field("port_from_initializer").required);

// An initializer whose value equals value-initialization still counts. No
// comparison of *values* could tell these two apart; only the reflection query can.
static_assert(m::has_member_initializer(field(^^cli, "zero_but_initialized")));
static_assert(!deduce_field("zero_but_initialized").required);
static_assert(deduce_field("no_initializer").required);

// `.default_value = ""` cannot express "default to empty" — clapp::cstr gives absent
// and empty the same length — but an empty-brace initializer can, and does.
static_assert(m::has_member_initializer(field(^^cli, "empty_by_initializer")));
static_assert(!deduce_field("empty_by_initializer").required);
static_assert(m::deduce(field_shape::scalar, 0, arg_attr{.default_value = ""}, false).required);

// --- ROW 7 · std::optional<T> -> set, 1, not required --------------------
static_assert(shape_field("config") == field_shape::optional_scalar);
static_assert(deduce_field("config").act == action::set);
static_assert(deduce_field("config").num_args == value_range::single());
static_assert(!deduce_field("config").required);
static_assert(deduce_field("config").store == store_kind::optional_single);

// --- ROW 8 · std::optional<std::optional<T>> -> set, 0..=1, not required -
static_assert(shape_field("tri_state") == field_shape::optional_optional_scalar);
static_assert(deduce_field("tri_state").act == action::set);
static_assert(deduce_field("tri_state").num_args == value_range::optional());
static_assert(deduce_field("tri_state").num_args.min_values() == 0);
static_assert(deduce_field("tri_state").num_args.max_values() == 1);
static_assert(!deduce_field("tri_state").required);
static_assert(deduce_field("tri_state").store == store_kind::optional_optional_single);

// --- ROW 9 · std::vector<T> / std::deque<T> -> append, 1.., not required -
static_assert(shape_field("includes") == field_shape::sequence);
static_assert(deduce_field("includes").act == action::append);
static_assert(deduce_field("includes").num_args == value_range::at_least(1));
static_assert(deduce_field("includes").num_args.is_unbounded());
static_assert(!deduce_field("includes").required);
static_assert(deduce_field("includes").store == store_kind::many);

static_assert(shape_field("queue") == field_shape::sequence);
static_assert(deduce_field("queue") == deduce_field("includes"));

// --- ROW 10 · std::optional<std::vector<T>> -> append, 1.., not required -
static_assert(shape_field("maybe_list") == field_shape::optional_sequence);
static_assert(deduce_field("maybe_list").act == action::append);
static_assert(deduce_field("maybe_list").num_args == value_range::at_least(1));
static_assert(!deduce_field("maybe_list").required);
// Distinct from ROW 9 exactly where it matters: absent must stay separable from
// "present but empty".
static_assert(deduce_field("maybe_list").store == store_kind::optional_many);
static_assert(deduce_field("maybe_list").store != deduce_field("includes").store);

// --- ROW 11 · std::array<T, N> -> set, N, required -----------------------
static_assert(shape_field("coords") == field_shape::fixed_array);
static_assert(deduce_field("coords").act == action::set);
static_assert(deduce_field("coords").num_args == value_range::exactly(3));
static_assert(deduce_field("coords").arity == 3);
static_assert(deduce_field("coords").required);
static_assert(deduce_field("coords").store == store_kind::fixed);

// --- ROW 12 · std::pair<A, B> -> set, 2, required ------------------------
static_assert(shape_field("key_value") == field_shape::pair);
static_assert(deduce_field("key_value").act == action::set);
static_assert(deduce_field("key_value").num_args == value_range::exactly(2));
static_assert(deduce_field("key_value").arity == 2);
static_assert(deduce_field("key_value").required);
static_assert(deduce_field("key_value").store == store_kind::pair);

// --- ROW 13 · enum -> set, 1, required, auto possible-values -------------
static_assert(shape_field("mode") == field_shape::enumeration);
static_assert(deduce_field("mode").act == action::set);
static_assert(deduce_field("mode").num_args == value_range::single());
static_assert(deduce_field("mode").required);
static_assert(deduce_field("mode").store == store_kind::single);
// The "自动 value_enum" half of the row: no ValueEnum opt-in exists to check, so
// the assertion is that the shape itself announces the enumeration.
static_assert(clapp::enumerates_values(shape_field("mode")));
static_assert(!clapp::enumerates_values(shape_field("name")));
static_assert(std::meta::enumerators_of(^^color).size() == 3);

// --- ROW 14 · std::variant<...> + [[= subcommand]] -> required -----------
static_assert(shape_field("command") == field_shape::subcommand_set);
static_assert(deduce_field("command").required);
static_assert(deduce_field("command").store == store_kind::subcommand);
// A subcommand set is not an argument, so both sentinels must stay undischarged.
// value_range::infer() answers takes_values() == false, so a caller that skipped
// is_argument() would silently treat the subcommand set as a flag.
static_assert(!deduce_field("command").is_argument());
static_assert(deduce_field("command").act == clapp::arg_action::infer);
static_assert(deduce_field("command").num_args.is_infer());
static_assert(!deduce_field("command").is_resolved());

// --- ROW 15 · std::optional<std::variant<...>> + [[= subcommand]] --------
static_assert(m::shape_of(field(^^cli_optional_subcommand, "command")) ==
              field_shape::optional_subcommand_set);
static_assert(!m::deduce_member(field(^^cli_optional_subcommand, "command")).required);
static_assert(m::deduce_member(field(^^cli_optional_subcommand, "command")).store ==
              store_kind::optional_subcommand);
static_assert(!m::deduce_member(field(^^cli_optional_subcommand, "command")).is_argument());
// The one bit that separates ROW 14 from ROW 15.
static_assert(deduce_field("command").required !=
              m::deduce_member(field(^^cli_optional_subcommand, "command")).required);

// --- ROW 16 · nested struct + [[= flatten]] ------------------------------
static_assert(shape_field("shared") == field_shape::flattened);
static_assert(deduce_field("shared").store == store_kind::nested);
static_assert(!deduce_field("shared").is_argument());
static_assert(deduce_field("shared").act == clapp::arg_action::infer);
static_assert(deduce_field("shared").num_args.is_infer());
static_assert(!deduce_field("shared").required);

// --- ROW 17 · any + [[= skip]] ------------------------------------------
//
// skip wins over the type: `internal` is an `int`, which would otherwise be a
// required scalar.
static_assert(shape_field("internal") == field_shape::skipped);
static_assert(deduce_field("internal").store == store_kind::none);
static_assert(!deduce_field("internal").is_argument());
static_assert(!deduce_field("internal").required);
static_assert(m::shape_of_type(^^int) == field_shape::scalar);

// ===========================================================================
// Precedence: an explicit annotation always beats deduction
// ===========================================================================

// .act overrides the row's action, and drags num_args to the action's default.
static_assert(deduce_field("appended").act == action::append);
static_assert(deduce_field("appended").num_args == value_range::single());
// ...but never the store-back, which the field's type alone decides.
static_assert(deduce_field("appended").store == store_kind::single);

// .required overrides in both directions, including against the row's rule.
static_assert(!deduce_field("not_required_after_all").required);
static_assert(deduce_field("required_flag").required);
static_assert(shape_field("required_flag") == field_shape::flag);  // a flag, yet required

// The arity override, which today only reaches deduce() and not the annotation DSL
// (clapp::arg_attr has no num_args member — see the warning on deduce()).
static_assert(m::deduce(field_shape::scalar, 0, arg_attr{}, false, value_range::exactly(4))
                      .num_args == value_range::exactly(4));
static_assert(m::deduce(field_shape::flag, 0, arg_attr{}, false, value_range::single()).num_args ==
              value_range::single());
// An explicit arity wins over an explicit action, too.
static_assert(m::deduce(field_shape::scalar,
                        0,
                        arg_attr{.act = action::count},
                        false,
                        value_range::exactly(2))
                      .num_args == value_range::exactly(2));

// tri::infer / action::infer / value_range::infer() must all mean "said nothing":
// an arg_attr that is entirely default must deduce identically to no annotation.
static_assert(m::deduce(field_shape::scalar, 0, arg_attr{}, false) ==
              m::deduce_member(field(^^cli, "name")));

// The clap rule `.required(required && action.takes_values())`: an action that
// consumes nothing cannot leave a required argument behind.
static_assert(!m::deduce(field_shape::scalar, 0, arg_attr{.act = action::help}, false).required);
static_assert(!m::deduce(field_shape::scalar, 0, arg_attr{.act = action::version}, false).required);
// Nor can an explicitly empty arity.
static_assert(!m::deduce(field_shape::scalar, 0, arg_attr{}, false, value_range::empty()).required);

// A subcommand set honours clap's `subcommand_required` through the same field.
static_assert(
        !m::deduce(field_shape::subcommand_set, 0, arg_attr{.required = tri::no}, false).required);
static_assert(
        m::deduce(field_shape::optional_subcommand_set, 0, arg_attr{.required = tri::yes}, false)
                .required);

// ===========================================================================
// value_type_of: what a value_parser specialization must exist for
// ===========================================================================

// value_type_of canonicalizes, so the right-hand side has to be canonicalized too:
// `^^std::string` is an alias for `std::__cxx11::basic_string<char>` on libstdc++ and
// for `std::__1::basic_string<...>` on libc++, and neither spelling is portable.
static_assert(m::value_type_of(^^bool) == (^^bool));
static_assert(m::value_type_of(^^std::string) == m::canonical_type(^^std::string));
static_assert(m::value_type_of(^^color) == (^^color));
static_assert(m::value_type_of(^^std::optional<bool>) == (^^bool));
static_assert(m::value_type_of(^^std::optional<int>) == (^^int));
static_assert(m::value_type_of(^^std::optional<std::optional<int>>) == (^^int));
static_assert(m::value_type_of(^^std::vector<char>) == (^^char));
static_assert(m::value_type_of(^^std::deque<int>) == (^^int));
static_assert(m::value_type_of(^^std::optional<std::vector<long>>) == (^^long));
static_assert(m::value_type_of(^^std::array<int, 3>) == (^^int));
static_assert(m::value_type_of(^^int_list) == (^^int));  // through an alias

// ===========================================================================
// Naming, for compile-time diagnostics
// ===========================================================================

static_assert(clapp::name_of(field_shape::flag) == "flag"sv);
static_assert(clapp::name_of(field_shape::optional_optional_scalar) ==
              "optional-optional-scalar"sv);
static_assert(clapp::name_of(store_kind::optional_many) == "optional-many"sv);
static_assert(clapp::name_of(clapp::arg_action::set_true) == "set-true"sv);  // the overload set

// ===========================================================================
// Structural invariants
// ===========================================================================

static_assert(std::meta::is_structural_type(^^clapp::deduced_arg));
static_assert(deduced_arg{}.shape == field_shape::skipped);
static_assert(deduced_arg{}.store == store_kind::none);
static_assert(!deduced_arg{}.is_argument());

// store_for() must agree with what deduce() actually emits, for every row.
consteval bool store_matches_shape_everywhere() {
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^clapp::field_shape)) {
        const auto shape = std::meta::extract<field_shape>(enumerator);
        if (m::deduce(shape, 2, arg_attr{}, false).store != clapp::store_for(shape)) return false;
    }
    return true;
}
static_assert(store_matches_shape_everywhere());

// Exactly four rows are structural (produce no argument); the rest are arguments.
consteval std::size_t structural_row_count() {
    std::size_t count = 0;
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^clapp::field_shape)) {
        if (!clapp::is_argument(std::meta::extract<field_shape>(enumerator))) ++count;
    }
    return count;
}
static_assert(structural_row_count() == 4);

// Every field of `cli` deduces without throwing, and every argument row comes out
// fully resolved. This is the check that would catch a new row leaking an `infer`.
consteval bool every_field_of_cli_resolves() {
    for (const std::meta::info member :
         std::meta::nonstatic_data_members_of(^^cli, std::meta::access_context::current())) {
        const deduced_arg deduced = m::deduce_member(member);
        if (deduced.is_argument() != deduced.is_resolved()) return false;
        if (deduced.store != clapp::store_for(deduced.shape)) return false;
    }
    return true;
}
static_assert(every_field_of_cli_resolves());

// ===========================================================================
// Runtime layer: reports what the compile-time layer already settled
// ===========================================================================

CLAPP_TEST("every clapp::field_shape maps to a store_kind that deduce() emits") {
    CLAPP_CHECK(store_matches_shape_everywhere());
}

CLAPP_TEST("exactly four deduction rows produce no argument") {
    CLAPP_CHECK(structural_row_count() == 4);
}

CLAPP_TEST("every field of the sample CLI resolves both sentinels or neither") {
    CLAPP_CHECK(every_field_of_cli_resolves());
}

CLAPP_TEST("a member initializer makes an otherwise-required scalar optional") {
    CLAPP_CHECK(deduce_field("name").required);
    CLAPP_CHECK(!deduce_field("port_from_initializer").required);
    CLAPP_CHECK(shape_field("name") == shape_field("port_from_initializer"));
}

CLAPP_TEST("optional wrappers keep 'absent' distinguishable from 'empty'") {
    CLAPP_CHECK(deduce_field("colorize").store == store_kind::optional_flag);
    CLAPP_CHECK(deduce_field("maybe_list").store == store_kind::optional_many);
    CLAPP_CHECK(deduce_field("includes").store == store_kind::many);
}

} // namespace

// NOLINTEND
