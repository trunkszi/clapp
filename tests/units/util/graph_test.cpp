#include <clapp/util/graph.hpp>

#include "support/check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

    using clapp::digraph;
    using clapp::digraph_view;
    using clapp::vertex_id;

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // `chain` is the motivating case from the docs: --a requires --b, --b requires
    // --c. `cycle` is the case clap tolerates rather than rejects.
    // ---------------------------------------------------------------------------

    consteval digraph<4> make_chain() {
        digraph<4> g;
        g.add_edge(0, 1);
        g.add_edge(1, 2);
        g.add_edge(2, 3);
        return g;
    }

    consteval digraph<3> make_cycle() {
        digraph<3> g;
        g.add_edge(0, 1);
        g.add_edge(1, 2);
        g.add_edge(2, 0);
        return g;
    }

    consteval digraph<5> make_forked() {
        digraph<5> g;
        g.add_edge(0, 1);
        g.add_edge(0, 2);
        g.add_edge(1, 3);
        g.add_edge(2, 3);
        g.add_edge(3, 4);
        return g;
    }

    constexpr auto chain        = make_chain();
    constexpr auto chain_closed = chain.transitive_closure();
    constexpr auto cycle        = make_cycle();
    constexpr auto cycle_closed = cycle.transitive_closure();
    constexpr auto forked       = make_forked();
    constexpr auto fork_closed  = forked.transitive_closure();

    // ---------------------------------------------------------------------------
    // Shape and edge bookkeeping
    // ---------------------------------------------------------------------------

    static_assert(digraph<4>::order == 4);
    static_assert(digraph<4>::words_per_row == 1);
    static_assert(digraph<64>::words_per_row == 1);
    static_assert(digraph<65>::words_per_row == 2);
    static_assert(digraph<130>::words_per_row == 3);
    static_assert(digraph<130>::word_count == 130 * 3);

    static_assert(digraph<4>{}.empty());
    static_assert(digraph<4>{}.edge_count() == 0);
    static_assert(!chain.empty());
    static_assert(chain.edge_count() == 3);
    static_assert(chain.out_degree(0) == 1);
    static_assert(chain.out_degree(3) == 0);

    static_assert(chain.has_edge(0, 1));
    static_assert(chain.has_edge(1, 2));
    static_assert(!chain.has_edge(0, 2));
    static_assert(!chain.has_edge(1, 0));

    // add_edge is idempotent; remove_edge undoes exactly one edge.
    consteval bool edge_bookkeeping() {
        digraph<3> g;
        g.add_edge(0, 1);
        g.add_edge(0, 1);
        const bool once = g.edge_count() == 1;
        g.add_edge(0, 2);
        g.remove_edge(0, 1);
        return once && g.edge_count() == 1 && !g.has_edge(0, 1) && g.has_edge(0, 2);
    }
    static_assert(edge_bookkeeping());

    // Out-of-range queries answer "no relation" instead of reading past the row.
    static_assert(!chain.has_edge(4, 0));
    static_assert(!chain.has_edge(0, 4));
    static_assert(chain.out_degree(99) == 0);

    // Construction from an edge list.
    consteval bool built_from_pairs() {
        constexpr std::array<std::pair<vertex_id, vertex_id>, 2> edges{{{0, 1}, {1, 2}}};
        const digraph<3> g{edges};
        return g.edge_count() == 2 && g.has_edge(0, 1) && g.has_edge(1, 2);
    }
    static_assert(built_from_pairs());

    // ---------------------------------------------------------------------------
    // Transitive closure — "A requires B, B requires C, therefore A requires C"
    // ---------------------------------------------------------------------------

    static_assert(chain_closed.has_edge(0, 1));
    static_assert(chain_closed.has_edge(0, 2));
    static_assert(chain_closed.has_edge(0, 3));
    static_assert(chain_closed.has_edge(1, 3));
    static_assert(chain_closed.edge_count() == 3 + 2 + 1);

    // Direction is preserved: closure never invents a back edge.
    static_assert(!chain_closed.has_edge(3, 0));
    static_assert(!chain_closed.has_edge(2, 1));

    // Non-reflexive: a vertex off every cycle gains no self-edge, because `requires`
    // names *other* arguments.
    static_assert(!chain_closed.has_edge(0, 0));
    static_assert(!chain_closed.has_edge(3, 3));

    // Idempotent, as a closure must be.
    static_assert(chain_closed.transitive_closure() == chain_closed);
    static_assert(digraph<4>{}.transitive_closure() == digraph<4>{});

    // A diamond closes to the union of both paths, without double-counting.
    static_assert(fork_closed.has_edge(0, 3));
    static_assert(fork_closed.has_edge(0, 4));
    static_assert(fork_closed.has_edge(1, 4));
    static_assert(fork_closed.out_degree(0) == 4);
    static_assert(fork_closed.acyclic());

    // ---------------------------------------------------------------------------
    // Cycles: permitted, not diagnosed away
    //
    // clap's unroll_arg_requires terminates on a cycle thanks to its `processed`
    // list, and still emits the starting argument because something on the cycle
    // requires it. The closure says the same thing with a self-edge.
    // ---------------------------------------------------------------------------

    static_assert(!cycle.acyclic());
    static_assert(cycle_closed.has_edge(0, 0));
    static_assert(cycle_closed.has_edge(1, 1));
    static_assert(cycle_closed.has_edge(2, 2));
    static_assert(cycle_closed.edge_count() == 9);  // complete: every vertex reaches every vertex
    static_assert(cycle_closed.self_reachable(0));

    // A self-edge written directly is a one-vertex cycle.
    consteval bool self_edge_is_a_cycle() {
        digraph<2> g;
        g.add_edge(1, 1);
        return !g.acyclic() && g.transitive_closure().has_edge(1, 1);
    }
    static_assert(self_edge_is_a_cycle());

    static_assert(chain.acyclic());
    static_assert(digraph<4>{}.acyclic());
    static_assert(digraph<0>{}.acyclic());

    // ---------------------------------------------------------------------------
    // Multi-word rows — the packing is where an off-by-one would hide
    // ---------------------------------------------------------------------------

    consteval bool wide_chain_closes() {
        constexpr std::size_t n = 130;  // three 64-bit words per row
        digraph<n> g;
        for (vertex_id i = 0; i + 1 < n; ++i) g.add_edge(i, i + 1);
        const auto closed = g.transitive_closure();
        return closed.has_edge(0, n - 1) && closed.has_edge(64, 65) && closed.has_edge(63, 128) &&
               !closed.has_edge(n - 1, 0) && closed.out_degree(0) == n - 1 &&
               closed.edge_count() == n * (n - 1) / 2;
    }
    static_assert(wide_chain_closes());

    // Bit 63 and bit 64 land in different words; make sure neither leaks.
    consteval bool word_boundary_is_clean() {
        digraph<70> g;
        g.add_edge(0, 63);
        g.add_edge(0, 64);
        return g.has_edge(0, 63) && g.has_edge(0, 64) && !g.has_edge(0, 62) && !g.has_edge(0, 65) &&
               g.out_degree(0) == 2 && g.edge_count() == 2;
    }
    static_assert(word_boundary_is_clean());

    // ---------------------------------------------------------------------------
    // successors()
    // ---------------------------------------------------------------------------

    consteval bool successors_are_ascending() {
        std::vector<vertex_id> seen;
        for (const vertex_id v : fork_closed.successors(0)) seen.push_back(v);
        return seen == std::vector<vertex_id>{1, 2, 3, 4};
    }
    static_assert(successors_are_ascending());

    consteval bool successors_of_a_sink_are_empty() {
        std::size_t n = 0;
        for ([[maybe_unused]] const vertex_id v : chain.successors(3)) ++n;
        return n == 0;
    }
    static_assert(successors_of_a_sink_are_empty());

    // ---------------------------------------------------------------------------
    // reachable_from() — one closure row, without closing the whole graph
    // ---------------------------------------------------------------------------

    consteval bool reachable_matches_closure(vertex_id from) {
        const std::vector<vertex_id> got = forked.reachable_from(from);
        std::vector<vertex_id> want;
        for (vertex_id v = 0; v < digraph<5>::order; ++v) {
            if (fork_closed.has_edge(from, v)) want.push_back(v);
        }
        return got == want;
    }
    static_assert(reachable_matches_closure(0));
    static_assert(reachable_matches_closure(1));
    static_assert(reachable_matches_closure(3));
    static_assert(reachable_matches_closure(4));

    consteval bool reachable_includes_self_only_on_a_cycle() {
        const std::vector<vertex_id> in_cycle = cycle.reachable_from(0);
        const std::vector<vertex_id> in_chain = chain.reachable_from(0);
        return in_cycle == std::vector<vertex_id>{0, 1, 2} &&
               in_chain == std::vector<vertex_id>{1, 2, 3};
    }
    static_assert(reachable_includes_self_only_on_a_cycle());

    consteval bool reachable_from_out_of_range_is_empty() {
        return chain.reachable_from(99).empty();
    }
    static_assert(reachable_from_out_of_range_is_empty());

    // ---------------------------------------------------------------------------
    // digraph_view — the runtime half
    //
    // This is the shape a closure takes once it has been promoted into .rodata: a
    // span of std::uint64_t (a structural type, unlike digraph itself) plus a count.
    // ---------------------------------------------------------------------------

    constexpr auto chain_words = chain_closed.to_array();
    constexpr digraph_view chain_view{chain_words, 4};

    static_assert(chain_view.order() == 4);
    static_assert(chain_view.words_per_row() == 1);
    static_assert(chain_view.bits().size() == 4);
    static_assert(chain_view.has_edge(0, 3));
    static_assert(!chain_view.has_edge(3, 0));
    static_assert(chain_view.edge_count() == chain_closed.edge_count());
    static_assert(chain_view.out_degree(0) == 3);
    static_assert(!chain_view.self_reachable(0));

    // Out-of-range is inert here too.
    static_assert(!chain_view.has_edge(4, 0));
    static_assert(!chain_view.has_edge(0, 4));
    static_assert(digraph_view{}.order() == 0);
    static_assert(digraph_view{}.edge_count() == 0);
    static_assert(!digraph_view{}.has_edge(0, 0));

    // A truncated span reports "no edge" rather than reading past its end.
    constexpr std::span<const std::uint64_t> truncated{chain_words.data(), 2};
    constexpr digraph_view short_view{truncated, 4};
    static_assert(short_view.has_edge(0, 1));
    static_assert(!short_view.has_edge(3, 0));
    static_assert(short_view.out_degree(3) == 0);

    consteval bool view_and_graph_agree() {
        for (vertex_id i = 0; i < 4; ++i) {
            for (vertex_id j = 0; j < 4; ++j) {
                if (chain_view.has_edge(i, j) != chain_closed.has_edge(i, j)) return false;
            }
        }
        return true;
    }
    static_assert(view_and_graph_agree());

    // ---------------------------------------------------------------------------
    // Degenerate graph
    // ---------------------------------------------------------------------------

    static_assert(digraph<0>::order == 0);
    static_assert(digraph<0>::words_per_row == 0);
    static_assert(digraph<0>::word_count == 0);
    static_assert(digraph<0>{}.edge_count() == 0);
    static_assert(digraph<0>{}.empty());
    static_assert(!digraph<0>{}.has_edge(0, 0));
    static_assert(digraph<0>{}.transitive_closure() == digraph<0>{});

    static_assert(digraph<1>{}.acyclic());
    static_assert(digraph<1>{}.edge_count() == 0);

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
// ---------------------------------------------------------------------------

CLAPP_TEST("transitive closure propagates requires relations") {
    CLAPP_CHECK(!chain.has_edge(0, 2));
    CLAPP_CHECK(chain_closed.has_edge(0, 2));
    CLAPP_CHECK(chain_closed.has_edge(0, 3));
    CLAPP_CHECK(!chain_closed.has_edge(3, 0));
}

CLAPP_TEST("a cycle closes to self-edges instead of failing") {
    CLAPP_CHECK(!cycle.acyclic());
    CLAPP_CHECK(cycle_closed.has_edge(0, 0));
    CLAPP_CHECK(cycle_closed.edge_count() == 9);
}

CLAPP_TEST("reachable_from returns the closure row for that vertex") {
    const std::vector<vertex_id> reachable = forked.reachable_from(0);
    CLAPP_CHECK(reachable == std::vector<vertex_id>({1, 2, 3, 4}));
    CLAPP_CHECK(forked.reachable_from(4).empty());
}

CLAPP_TEST("out-of-range add_edge is a no-op at runtime") {
    // The consteval branch throws instead, which is a compile error and therefore
    // cannot be exercised from here; this covers the other half of that `if
    // consteval`. A runtime graph only ever arises from a hand-written command
    // tree, and a bad relation there must not abort the process.
    digraph<2> g;
    g.add_edge(5, 0);
    g.add_edge(0, 5);
    CLAPP_CHECK(g.empty());
    CLAPP_CHECK(g.edge_count() == 0);
}

CLAPP_TEST("a promoted bit matrix reads back identically through digraph_view") {
    CLAPP_CHECK(chain_view.edge_count() == chain_closed.edge_count());
    CLAPP_CHECK(chain_view.has_edge(0, 3));
    CLAPP_CHECK(chain_view.order() == 4);
}
