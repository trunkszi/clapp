/**
 * \file
 * \brief clapp::flat_set — ordered set on a sorted vector (constexpr-usable).
 */

#pragma once

#include <clapp/util/flat_map.hpp>  // for clapp::detail::transparent_compare

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <ranges>
#include <utility>
#include <vector>

namespace clapp {
    /**
     * \brief Ordered set of \p T on one sorted vector (all members constexpr).
     * \tparam T Movable element type.
     * \tparam Compare Strict weak order; `std::less<>` for transparent lookup.
     * \note Transient in consteval (ADR-0005). Elements immutable (no sort-breaking).
     * \warning **Insertion order is not preserved** (clap's FlatSet does). User-visible
     *          output needs an explicit display key; membership tests miss wrong order.
     */
    template<class T, class Compare = std::less<T> >
    class flat_set {
    public:
        using key_type = T;
        using value_type = T;
        using key_compare = Compare;
        using value_compare = Compare;
        using container_type = std::vector<T>;

        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        using const_reference = const T &;
        using reference = const_reference;

        using const_iterator = container_type::const_iterator;
        /** \brief Same as #const_iterator (elements immutable). */
        using iterator = const_iterator;
        /** \brief Read-only reverse iterator. */
        using const_reverse_iterator = container_type::const_reverse_iterator;
        /** \copydoc iterator */
        using reverse_iterator = const_reverse_iterator;

        // -------------------------------------------------------------------
        // Construction
        // -------------------------------------------------------------------

        /** \brief An empty set with a default-constructed comparator. */
        constexpr flat_set() = default;

        /**
         * \brief An empty set using \p comp for ordering.
         * \param comp The comparator to store.
         */
        constexpr explicit flat_set(key_compare comp) : comp_(std::move(comp)) {
        }

        /**
         * \brief Build from a braced list of elements.
         * \param init Elements to insert, in any order; duplicates are dropped.
         * \param comp The comparator to store.
         */
        constexpr flat_set(std::initializer_list<value_type> init, key_compare comp = key_compare{})
            : comp_(std::move(comp)) {
            data_.reserve(init.size());
            // Raw loop: each insertion consults the partially built set, so the
            // elements are not independent and a pipeline would buy nothing.
            for (const value_type &value: init) insert(value);
        }

        /**
         * \brief From range (`std::from_range`).
         */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
        constexpr flat_set(std::from_range_t tag, R &&r, key_compare comp = key_compare{})
            : comp_(std::move(comp)) {
            (void) tag;
            if constexpr (std::ranges::sized_range<R>)
                data_.reserve(static_cast<size_type>(std::ranges::size(r)));
            insert_range(std::forward<R>(r));
        }

        // -------------------------------------------------------------------
        // Iteration
        // -------------------------------------------------------------------

        /** \brief Smallest element. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_.begin(); }
        /** \brief One past the largest element. */
        [[nodiscard]] constexpr const_iterator end() const noexcept { return data_.end(); }
        /** \copydoc begin */
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_.begin(); }
        /** \copydoc end */
        [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_.end(); }

        /** \brief Largest element, for reverse iteration. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
            return data_.rbegin();
        }

        /** \brief One before the smallest element. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
            return data_.rend();
        }

        /** \copydoc rbegin */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
            return data_.rbegin();
        }

        /** \copydoc rend */
        [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
            return data_.rend();
        }

        /** \brief Contiguous element block (`vector::data` rules when empty). */
        [[nodiscard]] constexpr const value_type *data() const noexcept { return data_.data(); }

        // -------------------------------------------------------------------
        // Capacity
        // -------------------------------------------------------------------

        /** \brief Whether the set holds no elements. */
        [[nodiscard]] constexpr bool empty() const noexcept { return data_.empty(); }
        /** \brief Number of elements. */
        [[nodiscard]] constexpr size_type size() const noexcept { return data_.size(); }
        /** \brief Elements that fit before the next reallocation. */
        [[nodiscard]] constexpr size_type capacity() const noexcept { return data_.capacity(); }

        /**
         * \brief Pre-allocate room for \p n elements.
         * \param n Desired capacity.
         */
        constexpr void reserve(size_type n) { data_.reserve(n); }

        /** \brief Drop every element, keeping the allocated capacity. */
        constexpr void clear() noexcept { data_.clear(); }

        /** \brief The stored comparator. */
        [[nodiscard]] constexpr key_compare key_comp() const { return comp_; }

        // -------------------------------------------------------------------
        // Lookup
        // -------------------------------------------------------------------

        /**
         * \brief Locate \p value.
         * \param value Element to look for.
         * \return An iterator to the element, or end() when absent.
         * \note Complexity: O(log n) comparisons.
         */
        [[nodiscard]] constexpr const_iterator find(const key_type &value) const {
            return find_impl(value);
        }

        /**
         * \brief Heterogeneous find (transparent Compare only).
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        [[nodiscard]] constexpr const_iterator find(const K &value) const {
            return find_impl(value);
        }

        /**
         * \brief Whether \p value is a member.
         * \param value Element to look for.
         */
        [[nodiscard]] constexpr bool contains(const key_type &value) const {
            return find_impl(value) != data_.end();
        }

        /**
         * \copydoc contains
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        [[nodiscard]] constexpr bool contains(const K &value) const {
            return find_impl(value) != data_.end();
        }

        // -------------------------------------------------------------------
        // Modification
        // -------------------------------------------------------------------

        /**
         * \brief Insert unless present; `{iterator, inserted}`.
         */
        constexpr std::pair<iterator, bool> insert(value_type value) {
            const const_iterator lb = lower_bound_impl(value);
            const difference_type off = lb - data_.cbegin();
            if (lb != data_.cend() && !comp_(value, *lb)) return {lb, false};
            data_.insert(lb, std::move(value));
            return {data_.cbegin() + off, true};
        }

        /**
         * \brief Insert new elements from \p r.
         */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
        constexpr void insert_range(R &&r) {
            // Raw loop: every step depends on the set as modified by the previous one.
            for (auto &&value: r) insert(value_type(std::forward<decltype(value)>(value)));
        }

        /**
         * \brief Remove \p value if present.
         * \param value The element to remove.
         * \return `true` when an element was removed.
         */
        constexpr bool erase(const key_type &value) { return erase_impl(value); }

        /**
         * \copydoc erase(const key_type&)
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        constexpr bool erase(const K &value) {
            return erase_impl(value);
        }

        /**
         * \brief Remove the element \p pos refers to.
         * \param pos A dereferenceable iterator into this set.
         * \return An iterator to the element that followed \p pos.
         */
        constexpr iterator erase(const_iterator pos) { return data_.erase(pos); }

        /**
         * \brief Erase elements matching \p pred; return count removed.
         */
        template<class Pred>
            requires std::predicate<Pred &, const_reference>
        friend constexpr size_type erase_if(flat_set &s, Pred pred) {
            const size_type before = s.data_.size();
            const auto gone = std::ranges::remove_if(s.data_, pred);
            s.data_.erase(gone.begin(), gone.end());
            return before - s.data_.size();
        }

        /**
         * \brief Take sorted storage; rvalue-qualified (`std::move(s).into_vector()`).
         */
        [[nodiscard]] constexpr container_type into_vector() && { return std::move(data_); }

        /**
         * \brief Swap contents with \p other in constant time.
         * \param other The set to swap with.
         */
        constexpr void swap(flat_set &other) noexcept {
            data_.swap(other.data_);
            std::ranges::swap(comp_, other.comp_);
        }

        /**
         * \brief ADL swap.
         * \param a First set.
         * \param b Second set.
         */
        friend constexpr void swap(flat_set &a, flat_set &b) noexcept { a.swap(b); }

        /**
         * \brief Element-wise equality. Comparators are not compared.
         * \param other The set to compare against.
         */
        [[nodiscard]] constexpr bool operator==(const flat_set &other) const {
            return std::ranges::equal(data_, other.data_);
        }

    private:
        /** \brief First element that does not compare before \p value. */
        template<class K>
        [[nodiscard]] constexpr const_iterator lower_bound_impl(const K &value) const {
            return std::ranges::lower_bound(data_, value, comp_);
        }

        /** \brief lower_bound_impl() plus the "is it actually equivalent" check. */
        template<class K>
        [[nodiscard]] constexpr const_iterator find_impl(const K &value) const {
            const const_iterator it = lower_bound_impl(value);
            if (it != data_.end() && !comp_(value, *it)) return it;
            return data_.end();
        }

        /** \brief Shared body of the two erase(key) overloads. */
        template<class K>
        constexpr bool erase_impl(const K &value) {
            const const_iterator it = find_impl(value);
            if (it == data_.cend()) return false;
            data_.erase(it);
            return true;
        }

        container_type data_{};
        key_compare comp_{};
    };
} // namespace clapp
