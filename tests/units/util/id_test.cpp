#include <clapp/detail/std_meta.hpp>
#include <clapp/util/flat_map.hpp>
#include <clapp/util/flat_set.hpp>
#include <clapp/util/graph.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include "support/check.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

    using clapp::arg_id;
    using clapp::id_table;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Layout and construction
    // ---------------------------------------------------------------------------

    // Pointer plus two 32-bit fields: cheap to copy, cheap to pass by value.
    static_assert(sizeof(arg_id) <= 2 * sizeof(void*));
    static_assert(std::is_trivially_copyable_v<arg_id>);

    constexpr arg_id verbose{"verbose"};
    constexpr arg_id empty{};

    static_assert(verbose.name() == "verbose"sv);
    static_assert(verbose.length == 7);
    static_assert(!verbose.empty());
    static_assert(!verbose.bound());
    static_assert(verbose.slot == arg_id::unbound);

    static_assert(empty.empty());
    static_assert(!empty.bound());
    static_assert(empty.name() == ""sv);
    static_assert(empty.text == nullptr);  // name() still yields a usable empty view

    // The empty id is clap's Id::EXTERNAL, which is likewise the empty string.
    static_assert(clapp::external_id.empty());
    static_assert(empty == clapp::external_id);
    static_assert(clapp::help_id == "help");
    static_assert(clapp::version_id == "version");

    // Constructing from a view plus a slot; the view must already be static storage.
    constexpr arg_id bound{"quiet"sv, 3};
    static_assert(bound.bound());
    static_assert(bound.slot == 3);
    static_assert(bound.name() == "quiet"sv);

    constexpr arg_id from_view{"quiet"sv};
    static_assert(!from_view.bound());
    static_assert(from_view == bound);  // slot is not part of identity

    // ---------------------------------------------------------------------------
    // Comparison — by name, never by slot
    //
    // The slot is an index into one command's table. Comparing slots would make a
    // global argument that reached a subcommand compare equal to whatever unrelated
    // argument occupies the same index there, and would do it silently.
    // ---------------------------------------------------------------------------

    static_assert(arg_id{"x"sv, 3} == arg_id{"x"sv, 7});
    static_assert(arg_id{"x"sv, 3} != arg_id{"y"sv, 3});
    static_assert(verbose == "verbose");
    static_assert(verbose != "version");
    static_assert("verbose" == verbose);  // reversed candidate, C++20

    static_assert((arg_id{"a"} <=> arg_id{"b"}) == std::strong_ordering::less);
    static_assert((arg_id{"b"} <=> arg_id{"a"}) == std::strong_ordering::greater);
    static_assert((arg_id{"a"} <=> arg_id{"a"}) == std::strong_ordering::equal);
    static_assert(arg_id{"a"} < arg_id{"ab"});
    static_assert(arg_id{"a"} < "b"sv);
    static_assert(arg_id{"c"} > "b"sv);

    // Ordering ignores the slot too, so a sorted container stays consistent.
    static_assert((arg_id{"a"sv, 9} <=> arg_id{"a"sv, 0}) == std::strong_ordering::equal);

    // ---------------------------------------------------------------------------
    // Reaching static storage
    //
    // arg_id is a structural type, but that alone does not make it usable as a
    // template argument: a `const char*` aimed at a string *literal* is rejected,
    // because a literal is not a variable. make_static_id() is the fix, and this is
    // the assertion that proves it — `tagged<arg_id{"verbose"}>` would not compile.
    // ---------------------------------------------------------------------------

    template<arg_id Id>
    struct tagged {
        static constexpr std::string_view name = Id.name();
        static constexpr std::uint32_t slot    = Id.slot;
    };

    static_assert(tagged<clapp::make_static_id("verbose", 0)>::name == "verbose"sv);
    static_assert(tagged<clapp::make_static_id("verbose", 0)>::slot == 0);

    consteval std::span<const arg_id> promoted() {
        std::array<arg_id, 3> ids{clapp::make_static_id("add", 0),
                                  clapp::make_static_id("commit", 1),
                                  clapp::make_static_id("push", 2)};
        return std::define_static_array(ids);
    }
    constexpr std::span<const arg_id> subcommands = promoted();

    static_assert(subcommands.size() == 3);
    static_assert(subcommands[0] == "add");
    static_assert(subcommands[2] == "push");
    static_assert(subcommands[2].slot == 2);

    // The name really was copied, not borrowed from the transient argument.
    consteval bool make_static_id_copies() {
        const std::string derived = clapp::rename("output_file", clapp::naming::kebab);
        const arg_id id           = clapp::make_static_id(derived);
        return id.name() == "output-file"sv && !id.bound();
    }
    static_assert(make_static_id_copies());

    // ---------------------------------------------------------------------------
    // id_table — the interner
    // ---------------------------------------------------------------------------

    consteval id_table<8> build_table() {
        id_table<8> t;
        t.intern("verbose");
        t.intern("quiet");
        t.intern("verbose");  // idempotent: no new slot
        t.intern("output-file");
        return t;
    }
    constexpr auto table = build_table();

    static_assert(id_table<8>::capacity == 8);
    static_assert(id_table<8>{}.empty());
    static_assert(id_table<8>{}.size() == 0);

    static_assert(table.size() == 3);
    static_assert(!table.empty());

    // Slots are handed out in insertion order, which is declaration order of the
    // reflected struct — the property clapp::digraph relies on.
    static_assert(table.find("verbose").slot == 0);
    static_assert(table.find("quiet").slot == 1);
    static_assert(table.find("output-file").slot == 2);
    static_assert(table.at(0) == "verbose");
    static_assert(table.at(1) == "quiet");
    static_assert(table.at(2) == "output-file");

    static_assert(table.contains("quiet"));
    static_assert(!table.contains("nonesuch"));
    static_assert(!table.find("nonesuch").bound());
    static_assert(table.at(3).empty());  // past the end
    static_assert(!table.at(3).bound());
    static_assert(table.at(99).empty());

    static_assert(table.entries().size() == 3);
    static_assert(table.entries()[1] == "quiet");
    static_assert(table.end() - table.begin() == 3);

    // intern() returns the *existing* id on a repeat, so a caller can use its result
    // unconditionally.
    consteval bool intern_is_idempotent() {
        id_table<4> t;
        const arg_id first  = t.intern("a");
        const arg_id second = t.intern("a");
        return first == second && first.slot == second.slot && t.size() == 1;
    }
    static_assert(intern_is_idempotent());

    // The empty name is internable — it is how clap spells the external subcommand —
    // and is then distinguishable from "absent" by arg_id::bound().
    consteval bool empty_name_is_internable() {
        id_table<2> t;
        const arg_id ext = t.intern("");
        return ext.bound() && ext.empty() && t.contains("") && t.size() == 1 &&
               !t.find("other").bound();
    }
    static_assert(empty_name_is_internable());

    consteval bool iteration_is_in_slot_order() {
        std::vector<std::string_view> seen;
        for (const arg_id& id : table) seen.push_back(id.name());
        return seen == std::vector<std::string_view>{"verbose", "quiet", "output-file"};
    }
    static_assert(iteration_is_in_slot_order());

    // ---------------------------------------------------------------------------
    // names() feeds the suggestion machinery directly
    // ---------------------------------------------------------------------------

    consteval bool names_view_matches_entries() {
        std::vector<std::string_view> seen;
        for (const std::string_view name : table.names()) seen.push_back(name);
        return seen == std::vector<std::string_view>{"verbose", "quiet", "output-file"};
    }
    static_assert(names_view_matches_entries());

    consteval bool suggests(std::string_view typo, std::string_view want) {
        const auto hit = clapp::best_match(typo, table.names());
        return hit.has_value() && hit.value() == want;
    }
    static_assert(suggests("verbse", "verbose"));
    static_assert(suggests("quite", "quiet"));
    static_assert(suggests("outputfile", "output-file"));
    static_assert(!clapp::best_match("zzzzzzzz", table.names()).has_value());

    // ---------------------------------------------------------------------------
    // Slots line up with graph vertices
    //
    // This is the reason id_table exists rather than a bare set of names: the slot an
    // id gets *is* its clapp::digraph vertex.
    // ---------------------------------------------------------------------------

    consteval bool slots_index_a_requires_graph() {
        // --verbose requires --output-file, --output-file requires --quiet.
        clapp::digraph<3> g;
        g.add_edge(table.find("verbose").slot, table.find("output-file").slot);
        g.add_edge(table.find("output-file").slot, table.find("quiet").slot);
        const auto closed = g.transitive_closure();
        return closed.has_edge(table.find("verbose").slot, table.find("quiet").slot);
    }
    static_assert(slots_index_a_requires_graph());

    // ---------------------------------------------------------------------------
    // arg_id as a key of clapp's own ordered containers
    //
    // The point of giving arg_id a strong_ordering rather than only equality: the
    // parser keys its matches by id, and clapp::flat_map is what it keys them into.
    // Ordering follows the name, so a lookup succeeds regardless of which table's
    // slot the probe happens to carry.
    // ---------------------------------------------------------------------------

    consteval bool usable_as_a_flat_key() {
        clapp::flat_map<arg_id, int> matches;
        matches.insert_or_assign(arg_id{"verbose"sv, 0}, 1);
        matches.insert_or_assign(arg_id{"quiet"sv, 1}, 2);

        clapp::flat_set<arg_id> seen;
        seen.insert(arg_id{"verbose"sv, 0});

        return matches.size() == 2 &&
               matches.contains(arg_id{"verbose"sv, 99})  // differently slotted probe
               && matches.begin()->first == "quiet"       // sorted by name, not by slot
               && seen.contains(arg_id{"verbose"sv, 7});
    }
    static_assert(usable_as_a_flat_key());

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_id compares by name and ignores the slot") {
    CLAPP_CHECK(arg_id("x"sv, 3) == arg_id("x"sv, 7));
    CLAPP_CHECK(arg_id("x"sv, 3) != arg_id("y"sv, 3));
    CLAPP_CHECK(verbose == "verbose");
}

CLAPP_TEST("arg_id works as a std::map and std::set key") {
    std::map<arg_id, int> by_id;
    by_id[arg_id("verbose"sv, 0)] = 1;
    by_id[arg_id("quiet"sv, 1)]   = 2;
    // Looking up with a differently-slotted id must still hit.
    CLAPP_CHECK(by_id.at(arg_id("verbose"sv, 99)) == 1);
    CLAPP_CHECK(by_id.size() == 2);

    // Ordered lexicographically, not by slot.
    const std::set<arg_id> sorted{arg_id("verbose"sv, 0), arg_id("quiet"sv, 1)};
    CLAPP_CHECK(sorted.begin()->name() == "quiet");
}

CLAPP_TEST("std::hash agrees with operator== ") {
    // Not constexpr, so this can only be checked here.
    const std::hash<arg_id> h;
    CLAPP_CHECK(h(arg_id("verbose"sv, 0)) == h(arg_id("verbose"sv, 41)));
    CLAPP_CHECK(h(arg_id("verbose"sv, 0)) == std::hash<std::string_view>{}("verbose"));

    std::unordered_map<arg_id, int> by_id;
    by_id[arg_id("verbose"sv, 0)] = 7;
    CLAPP_CHECK(by_id.at(arg_id("verbose"sv, 99)) == 7);
}

CLAPP_TEST("id_table hands out slots in insertion order") {
    CLAPP_CHECK(table.size() == 3);
    CLAPP_CHECK(table.find("verbose").slot == 0);
    CLAPP_CHECK(table.find("output-file").slot == 2);
    CLAPP_CHECK(!table.find("nonesuch").bound());
}

CLAPP_TEST("id_table overflow returns an unbound id at runtime") {
    // The consteval branch throws, which is a compile error and cannot be reached
    // from a translation unit that compiles; this covers the other half. Runtime
    // tables only arise from hand-written command trees, where losing one id is
    // preferable to aborting the process.
    id_table<1> t;
    CLAPP_CHECK(t.intern("a").bound());
    const arg_id overflowed = t.intern("b");
    CLAPP_CHECK(!overflowed.bound());
    CLAPP_CHECK(t.size() == 1);
    CLAPP_CHECK(!t.contains("b"));
}

CLAPP_TEST("arg_id keys a clapp::flat_map by name") {
    clapp::flat_map<arg_id, int> matches;
    matches.insert_or_assign(arg_id("verbose"sv, 0), 1);
    matches.insert_or_assign(arg_id("quiet"sv, 1), 2);
    CLAPP_CHECK(matches.size() == 2);
    CLAPP_CHECK(matches.contains(arg_id("verbose"sv, 99)));
    CLAPP_CHECK(matches.begin()->first == "quiet");
}

CLAPP_TEST("names() is accepted by best_match unchanged") {
    const auto hit = clapp::best_match("verbse", table.names());
    CLAPP_CHECK(hit.has_value());
    CLAPP_CHECK(hit.value() == "verbose");
}
