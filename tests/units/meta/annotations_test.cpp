/**
 * \file
 * \brief Unit tests for clapp/meta/annotations.hpp.
 *
 * Most assertions here are `static_assert`: annotation extraction happens entirely at
 * compile time, so whether they hold is settled before the binary exists. The runtime
 * CLAPP_TEST cases exist to report those results and to cover the parts that need
 * runtime containers.
 */

#include <clapp/meta/annotations.hpp>

#include "support/check.hpp"

#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

// This translation unit intentionally declares reflection-only fixtures whose spelling
// is part of the test data, and the test harness registers cases during static
// initialization.  Those patterns conflict with the project-wide clang-tidy policy.
// NOLINTBEGIN

using namespace std::string_view_literals;

namespace {

// ===========================================================================
// Types under reflection
// ===========================================================================

struct cmd_add {};

struct cmd_commit {};

struct cmd_push {};

using subcommands = std::variant<cmd_add, cmd_commit, cmd_push>;

/**
 * \note The annotation must follow the `struct` keyword, not precede it. Writing
 *       `[[= clapp::cmd{...}]] struct sample` compiles but the attribute is silently
 *       dropped — GCC 16 warns
 *       `attribute for 'struct sample' must follow the 'struct' keyword`
 *       and `annotation_of<command_attr>` then returns nullopt. Verified below.
 */
struct[[= clapp::cmd{.name = "demo", .about = "A demo command"}]] sample {
    [[= clapp::arg{.short_ = 'n', .long_ = "name", .help = "Who to greet"}]]
    [[maybe_unused]] int name;

    [[= clapp::arg{.short_ = 'v', .act = clapp::action::count}]] [[maybe_unused]] int verbose;

    [[= clapp::arg{.long_ = "quiet"}]][[= clapp::conflicts_with{"verbose"}]]
                                      [[= clapp::conflicts_with{"debug"}]]
                                      [[maybe_unused]] bool quiet;

    [[= clapp::skip{}]] [[maybe_unused]] int internal;

    [[maybe_unused]] int plain; /**< No annotation at all; everything comes from deduction. */
};

consteval std::meta::info member(std::size_t i) {
    return std::meta::nonstatic_data_members_of(^^sample, std::meta::access_context::current())[i];
}

// ===========================================================================
// cstr: structural, and its payload survives extraction intact
// ===========================================================================

static_assert(clapp::cstr<16>{"abc"}.view() == "abc"sv);
static_assert(clapp::cstr<16>{"abc"}.size() == 3);
static_assert(clapp::cstr<16>::capacity() == 16);
static_assert(clapp::cstr<16>{}.empty());
static_assert(!clapp::cstr<16>{"x"}.empty());
static_assert(clapp::cstr<16>{"abc"} == clapp::cstr<16>{"abc"});
static_assert(clapp::cstr<16>{"abc"} != clapp::cstr<16>{"abd"});

// Usable as a range, so ranges algorithms apply directly.
static_assert(std::ranges::distance(clapp::cstr<16>{"abc"}) == 3);
static_assert(*clapp::cstr<16>{"abc"}.begin() == 'a');

// The implicit literal conversion the whole DSL depends on.
static_assert(std::is_convertible_v<const char (&)[4], clapp::cstr<16>>);

// ===========================================================================
// annotation_of: values come through, unspecified fields keep their defaults
// ===========================================================================

static_assert(clapp::meta::annotation_of<clapp::arg_attr>(member(0)).has_value());
static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(0)).short_ == 'n');
static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(0)).long_.view() == "name"sv);
static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(0)).help.view() ==
              "Who to greet"sv);

/**
 * The distinction that makes the deduction rules work: an unmentioned field must read
 * back as `infer`, not as the value that inference would have produced.
 */
static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(0)).act == clapp::action::infer);
static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(0)).required == clapp::tri::infer);

static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(1)).act == clapp::action::count);
static_assert(clapp::meta::annotation_or<clapp::arg_attr>(member(1)).long_.empty());

// A member with no annotation of that type yields nullopt.
static_assert(!clapp::meta::annotation_of<clapp::arg_attr>(member(4)).has_value());

// ===========================================================================
// has_annotation: empty structural markers
// ===========================================================================

static_assert(clapp::meta::has_annotation<clapp::skip>(member(3)));
static_assert(!clapp::meta::has_annotation<clapp::skip>(member(0)));
static_assert(!clapp::meta::has_annotation<clapp::flatten>(member(0)));

// ===========================================================================
// Type-level annotations
// ===========================================================================

static_assert(clapp::meta::annotation_of<clapp::command_attr>(^^sample).has_value());
static_assert(clapp::meta::annotation_or<clapp::command_attr>(^^sample).name.view() == "demo"sv);
static_assert(clapp::meta::annotation_or<clapp::command_attr>(^^sample).about.view() ==
              "A demo command"sv);
static_assert(!clapp::meta::annotation_of<clapp::command_attr>(^^cmd_add).has_value());

// ===========================================================================
// variant_traits: pack indexing over the alternatives
// ===========================================================================

using vt = clapp::meta::variant_traits<subcommands>;
static_assert(vt::size == 3);
static_assert(std::is_same_v<vt::alternative<0>, cmd_add>);
static_assert(std::is_same_v<vt::alternative<1>, cmd_commit>);
static_assert(std::is_same_v<vt::alternative<2>, cmd_push>);
static_assert(vt::reflections.size() == 3);
static_assert(std::meta::identifier_of(vt::reflections[0]) == "cmd_add"sv);
static_assert(std::meta::identifier_of(vt::reflections[2]) == "cmd_push"sv);

// ===========================================================================
// Stackable relation annotations
//
// The result is std::vector<std::string_view>, which can neither be a constexpr
// variable (non-transient allocation) nor be passed to std::define_static_array
// (its elements must be structural, and string_view's _M_len is private).
// So these checks run wholly inside consteval functions and only carry a bool out.
// ===========================================================================

consteval bool conflicts_collected() {
    auto ids = clapp::meta::relation_ids_of<clapp::conflicts_with>(member(2));
    return ids.size() == 2 && ids[0] == "verbose"sv && ids[1] == "debug"sv;
}

static_assert(conflicts_collected());

consteval bool no_relations_is_empty() {
    return clapp::meta::relation_ids_of<clapp::conflicts_with>(member(0)).empty();
}

static_assert(no_relations_is_empty());

consteval bool relations_do_not_bleed_across_types() {
    return clapp::meta::annotations_all_of<clapp::requires_arg>(member(2)).empty();
}

static_assert(relations_do_not_bleed_across_types());

consteval bool all_of_collects_every_occurrence() {
    return clapp::meta::annotations_all_of<clapp::conflicts_with>(member(2)).size() == 2;
}

static_assert(all_of_collects_every_occurrence());

// ===========================================================================
// Runtime layer: reports what the compile-time layer already settled
// ===========================================================================

CLAPP_TEST("stackable relations are all collected, in order") {
    CLAPP_CHECK(conflicts_collected());
}

CLAPP_TEST("a member without relations yields an empty list") {
    CLAPP_CHECK(no_relations_is_empty());
}

CLAPP_TEST("annotations_all_of does not bleed across annotation types") {
    CLAPP_CHECK(relations_do_not_bleed_across_types());
}

CLAPP_TEST("annotations_all_of collects every occurrence") {
    CLAPP_CHECK(all_of_collects_every_occurrence());
}

} // namespace

// NOLINTEND
