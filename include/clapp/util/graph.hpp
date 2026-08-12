/**
 * \file
 * \brief Directed bit-matrix graph and transitive closure (requires relations).
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace clapp {

    /** \brief digraph vertex: index into the command argument table. */
    using vertex_id = std::size_t;

    namespace detail {

        /** \brief 64-bit words needed to hold \p n bits. */
        [[nodiscard]] constexpr std::size_t bit_words(std::size_t n) noexcept {
            return (n + 63) / 64;
        }

        /** \brief The single-bit mask selecting bit \p i within its word. */
        [[nodiscard]] constexpr std::uint64_t bit_mask(std::size_t i) noexcept {
            return std::uint64_t{1} << (i % 64);
        }

        /** \brief Read bit \p column of the row starting at \p row. */
        [[nodiscard]] constexpr bool test_bit(std::span<const std::uint64_t> row,
                                              std::size_t column) noexcept {
            return (row[column / 64] & bit_mask(column)) != 0;
        }

        /** \brief Population count of a whole row. */
        [[nodiscard]] constexpr std::size_t
        popcount_row(std::span<const std::uint64_t> row) noexcept {
            return std::ranges::fold_left(
                    row, std::size_t{0}, [](std::size_t total, std::uint64_t word) {
                        return total + static_cast<std::size_t>(std::popcount(word));
                    });
        }

    }  // namespace detail

    /**
     * \brief Non-owning view of a row-major adjacency bit matrix.
     * \note Promote closed bits with `define_static_array`; edge query is one load/test.
     */
    class digraph_view {
    public:
        /** \brief An empty view: zero vertices, no edges. */
        constexpr digraph_view() noexcept = default;

        /**
         * \brief Wrap row-major \p bits for \p order vertices.
         * \pre Prefer full size; shorter span → missing rows report false (no OOB).
         */
        constexpr digraph_view(std::span<const std::uint64_t> bits, std::size_t order) noexcept
            : bits_(bits), order_(order) {}

        /** \brief Number of vertices. */
        [[nodiscard]] constexpr std::size_t order() const noexcept { return order_; }

        /** \brief Words per adjacency row. */
        [[nodiscard]] constexpr std::size_t words_per_row() const noexcept {
            return detail::bit_words(order_);
        }

        /** \brief The underlying bits, row-major. */
        [[nodiscard]] constexpr std::span<const std::uint64_t> bits() const noexcept {
            return bits_;
        }

        /**
         * \brief Whether edge `from -> to` exists (`false` if out of range).
         */
        [[nodiscard]] constexpr bool has_edge(vertex_id from, vertex_id to) const noexcept {
            if (from >= order_ || to >= order_) return false;
            const std::size_t width = words_per_row();
            const std::size_t base  = from * width;
            if (base + width > bits_.size()) return false;
            return detail::test_bit(bits_.subspan(base, width), to);
        }

        /** \brief Number of edges leaving \p from. */
        [[nodiscard]] constexpr std::size_t out_degree(vertex_id from) const noexcept {
            if (from >= order_) return 0;
            const std::size_t width = words_per_row();
            const std::size_t base  = from * width;
            if (base + width > bits_.size()) return 0;
            return detail::popcount_row(bits_.subspan(base, width));
        }

        /** \brief Total number of edges. */
        [[nodiscard]] constexpr std::size_t edge_count() const noexcept {
            std::size_t total = 0;
            for (vertex_id v = 0; v < order_; ++v) total += out_degree(v);
            return total;
        }

        /**
         * \brief Out-neighbours of \p from (lazy, ascending).
         * \warning View borrows the bit matrix; invalid when the matrix dies.
         */
        [[nodiscard]] constexpr auto successors(vertex_id from) const noexcept {
            return std::views::iota(vertex_id{0}, order_) |
                   std::views::filter(
                           [self = *this, from](vertex_id to) { return self.has_edge(from, to); });
        }

        /**
         * \brief Self-edge on \p v (cycle marker on a transitive closure).
         */
        [[nodiscard]] constexpr bool self_reachable(vertex_id v) const noexcept {
            return has_edge(v, v);
        }

    private:
        std::span<const std::uint64_t> bits_{};
        std::size_t order_ = 0;
    };

    /**
     * \brief Fixed digraph on vertices `[0, N)` as a packed adjacency bit matrix.
     * \tparam N Vertex count (`0` allowed).
     * \note Not structural — persist `to_array()` / digraph_view, not the graph itself.
     */
    template<std::size_t N>
    class digraph {
    public:
        /** \brief Vertex count, as a compile-time constant. */
        static constexpr std::size_t order = N;

        /** \brief 64-bit words in one adjacency row. */
        static constexpr std::size_t words_per_row = detail::bit_words(N);

        /** \brief Total words of storage. */
        static constexpr std::size_t word_count = N * words_per_row;

        /** \brief The empty graph on `N` vertices. */
        constexpr digraph() noexcept = default;

        /**
         * \brief Build from a list of edges.
         * \param edges `{from, to}` pairs; see add_edge() for the range rule.
         */
        constexpr explicit digraph(std::span<const std::pair<vertex_id, vertex_id>> edges) {
            for (const auto& [from, to] : edges) add_edge(from, to);
        }

        /**
         * \brief Add edge `from -> to` (idempotent).
         * \note Out of range: aborts constant evaluation; no-op at runtime.
         */
        constexpr void add_edge(vertex_id from, vertex_id to) {
            if (from >= N || to >= N) {
                if consteval {
                    std::abort();
                }
                return;
            }
            bits_[from * words_per_row + to / 64] |= detail::bit_mask(to);
        }

        /** \brief Remove the edge `from -> to`, if present. Out-of-range is a no-op. */
        constexpr void remove_edge(vertex_id from, vertex_id to) noexcept {
            if (from >= N || to >= N) return;
            bits_[from * words_per_row + to / 64] &= ~detail::bit_mask(to);
        }

        /** \brief Whether the edge `from -> to` is present. `false` when out of range. */
        [[nodiscard]] constexpr bool has_edge(vertex_id from, vertex_id to) const noexcept {
            if (from >= N || to >= N) return false;
            return (bits_[from * words_per_row + to / 64] & detail::bit_mask(to)) != 0;
        }

        /** \brief Number of edges leaving \p from. */
        [[nodiscard]] constexpr std::size_t out_degree(vertex_id from) const noexcept {
            return view().out_degree(from);
        }

        /** \brief Total number of edges. */
        [[nodiscard]] constexpr std::size_t edge_count() const noexcept {
            return view().edge_count();
        }

        /** \brief Whether the graph has no edges at all. */
        [[nodiscard]] constexpr bool empty() const noexcept {
            return std::ranges::all_of(bits_, [](std::uint64_t w) { return w == 0; });
        }

        /**
         * \brief The vertices reachable from \p from in exactly one edge.
         * \warning See digraph_view::successors() — the view borrows.
         */
        [[nodiscard]] constexpr auto successors(vertex_id from) const noexcept {
            return view().successors(from);
        }

        /**
         * \brief Transitive closure (path length ≥ 1); non-reflexive; O(N³/64).
         * \return New graph; cycles become self-edges (not an error; clap agrees).
         */
        [[nodiscard]] constexpr digraph transitive_closure() const noexcept {
            digraph out = *this;
            // Raw loops: this is Warshall's algorithm, whose correctness depends on the
            // exact `k, i` nesting and on mutating rows in place. A views pipeline would
            // hide both.
            for (vertex_id k = 0; k < N; ++k) {
                for (vertex_id i = 0; i < N; ++i) {
                    if (!out.has_edge(i, k)) continue;
                    for (std::size_t w = 0; w < words_per_row; ++w) {
                        out.bits_[i * words_per_row + w] |= out.bits_[k * words_per_row + w];
                    }
                }
            }
            return out;
        }

        /**
         * \brief Whether \p v lies on a cycle, assuming `*this` is a transitive closure.
         * \note On a raw edge set this reports a literal self-edge and nothing more.
         */
        [[nodiscard]] constexpr bool self_reachable(vertex_id v) const noexcept {
            return has_edge(v, v);
        }

        /**
         * \brief Whether no vertex can reach itself (closes then checks diagonal).
         */
        [[nodiscard]] constexpr bool acyclic() const noexcept {
            const digraph closed = transitive_closure();
            for (vertex_id v = 0; v < N; ++v) {
                if (closed.has_edge(v, v)) return false;
            }
            return true;
        }

        /**
         * \brief Vertices reachable from \p from (path ≥ 1); includes self if cyclic.
         */
        [[nodiscard]] constexpr std::vector<vertex_id> reachable_from(vertex_id from) const {
            std::vector<vertex_id> found;
            if (from >= N) return found;

            std::array<bool, N == 0 ? 1 : N> seen{};
            std::vector<vertex_id> frontier{from};
            while (!frontier.empty()) {
                const vertex_id current = frontier.back();
                frontier.pop_back();
                for (vertex_id to = 0; to < N; ++to) {
                    if (!has_edge(current, to) || seen[to]) continue;
                    seen[to] = true;
                    frontier.push_back(to);
                }
            }
            for (vertex_id v = 0; v < N; ++v) {
                if (seen[v]) found.push_back(v);
            }
            return found;
        }

        /**
         * \brief Row-major adjacency bits.
         * \warning Borrows `*this`. Promote with `define_static_array(to_array())`
         *          before escaping consteval.
         */
        [[nodiscard]] constexpr std::span<const std::uint64_t> bits() const noexcept {
            return bits_;
        }

        /**
         * \brief Owning bit array for `define_static_array` (structural `uint64_t`).
         */
        [[nodiscard]] constexpr std::array<std::uint64_t, word_count> to_array() const noexcept {
            return bits_;
        }

        /**
         * \brief A read-only view of this graph.
         * \warning Borrows `*this`.
         */
        [[nodiscard]] constexpr digraph_view view() const noexcept {
            return digraph_view{std::span<const std::uint64_t>{bits_}, N};
        }

        /** \brief Compare graph dimensions and adjacency bits. */
        [[nodiscard]] constexpr bool operator==(const digraph&) const noexcept = default;

    private:
        std::array<std::uint64_t, word_count> bits_{};
    };

}  // namespace clapp
