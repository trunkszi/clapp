/**
 * \file
 * \brief clapp::flat_map — ordered map on a sorted vector (constexpr-usable).
 */

#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

namespace clapp {
    namespace detail {
        /**
         * \brief Whether \p Compare has nested `is_transparent` (heterogeneous lookup).
         */
        template<class Compare>
        concept transparent_compare = requires { typename Compare::is_transparent; };
    } // namespace detail

    /**
     * \brief Ordered map Key→T on one sorted vector (all members constexpr).
     * \tparam Key Movable, ordered by \p Compare.
     * \tparam T Mapped type.
     * \tparam Compare Strict weak order; `std::less<>` for transparent lookup.
     * \note Transient in consteval — not a constexpr variable (ADR-0005).
     * \warning **Does not preserve insertion order** (clap's FlatMap does). User-visible
     *          lists need an explicit ordinal (`display_order`); membership tests miss this.
     * \warning Keys immutable (#iterator ≡ #const_iterator); only mapped values mutate.
     *          A `Key&` would silently break the sort invariant.
     */
    template<class Key, class T, class Compare = std::less<Key> >
    class flat_map {
    public:
        /** \brief The key type, as passed for \p Key. */
        using key_type = Key;
        /** \brief The mapped type, as passed for \p T. */
        using mapped_type = T;
        /** \brief What iteration yields: a key/value pair. */
        using value_type = std::pair<Key, T>;
        /** \brief The comparator type, as passed for \p Compare. */
        using key_compare = Compare;
        /** \brief Underlying storage type. */
        using container_type = std::vector<value_type>;

        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        /** \brief Read-only entry reference (no mutable entry access). */
        using const_reference = const value_type &;
        using reference = const_reference;

        using const_iterator = container_type::const_iterator;
        /** \brief Same as #const_iterator (keys immutable). */
        using iterator = const_iterator;
        /** \brief Read-only reverse iterator. */
        using const_reverse_iterator = container_type::const_reverse_iterator;
        /** \copydoc iterator */
        using reverse_iterator = const_reverse_iterator;

        // -------------------------------------------------------------------
        // Construction
        // -------------------------------------------------------------------

        /** \brief An empty map with a default-constructed comparator. */
        constexpr flat_map() = default;

        /**
         * \brief An empty map using \p comp for ordering.
         * \param comp The comparator to store.
         */
        constexpr explicit flat_map(key_compare comp) : comp_(std::move(comp)) {
        }

        /**
         * \brief From braced entries (first of duplicate keys wins).
         */
        constexpr flat_map(std::initializer_list<value_type> init, key_compare comp = key_compare{})
            : comp_(std::move(comp)) {
            data_.reserve(init.size());
            // Raw loop rather than a ranges pipeline: each step must consult the
            // partially built map to decide whether to insert, so the elements are
            // not independent and there is nothing to pipeline.
            for (const value_type &entry: init) try_emplace(entry.first, entry.second);
        }

        /**
         * \brief From range of pair-like elements (`std::from_range`; first key wins).
         */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
        constexpr flat_map(std::from_range_t tag, R &&r, key_compare comp = key_compare{})
            : comp_(std::move(comp)) {
            (void) tag;
            if constexpr (std::ranges::sized_range<R>)
                data_.reserve(static_cast<size_type>(std::ranges::size(r)));
            // Raw loop: see the initializer-list constructor.
            for (auto &&entry: r) {
                value_type e(std::forward<decltype(entry)>(entry));
                try_emplace(std::move(e.first), std::move(e.second));
            }
        }

        // -------------------------------------------------------------------
        // Iteration
        // -------------------------------------------------------------------

        /** \brief First entry in key order. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_.begin(); }
        /** \brief One past the last entry. */
        [[nodiscard]] constexpr const_iterator end() const noexcept { return data_.end(); }
        /** \copydoc begin */
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_.begin(); }
        /** \copydoc end */
        [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_.end(); }

        /** \brief Last entry in key order, for reverse iteration. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
            return data_.rbegin();
        }

        /** \brief One before the first entry. */
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

        /** \brief Lazy view of keys in order. */
        [[nodiscard]] constexpr auto keys() const noexcept {
            return data_ | std::views::transform(&value_type::first);
        }

        /** \brief Lazy view of mapped values (const). */
        [[nodiscard]] constexpr auto values() const noexcept {
            return data_ | std::views::transform(&value_type::second);
        }

        /**
         * \brief Lazy mutable view of mapped values only (keys stay read-only).
         */
        [[nodiscard]] constexpr auto values() noexcept {
            return data_ | std::views::transform(&value_type::second);
        }

        // -------------------------------------------------------------------
        // Capacity
        // -------------------------------------------------------------------

        /** \brief Whether the map holds no entries. */
        [[nodiscard]] constexpr bool empty() const noexcept { return data_.empty(); }
        /** \brief Number of entries. */
        [[nodiscard]] constexpr size_type size() const noexcept { return data_.size(); }
        /** \brief Entries that fit before the next reallocation. */
        [[nodiscard]] constexpr size_type capacity() const noexcept { return data_.capacity(); }

        /**
         * \brief Pre-allocate room for \p n entries.
         * \param n Desired capacity.
         */
        constexpr void reserve(size_type n) { data_.reserve(n); }

        /** \brief Drop every entry, keeping the allocated capacity. */
        constexpr void clear() noexcept { data_.clear(); }

        /** \brief The stored comparator. */
        [[nodiscard]] constexpr key_compare key_comp() const { return comp_; }

        // -------------------------------------------------------------------
        // Lookup
        // -------------------------------------------------------------------

        /**
         * \brief Locate the entry for \p key.
         * \param key Key to look for.
         * \return An iterator to the entry, or end() when absent.
         * \note Complexity: O(log n) comparisons.
         */
        [[nodiscard]] constexpr const_iterator find(const key_type &key) const {
            return find_impl(key);
        }

        /**
         * \brief Heterogeneous find (transparent Compare only).
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        [[nodiscard]] constexpr const_iterator find(const K &key) const {
            return find_impl(key);
        }

        /**
         * \brief Whether an entry for \p key exists.
         * \param key Key to look for.
         */
        [[nodiscard]] constexpr bool contains(const key_type &key) const {
            return find_impl(key) != data_.end();
        }

        /**
         * \copydoc contains
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        [[nodiscard]] constexpr bool contains(const K &key) const {
            return find_impl(key) != data_.end();
        }

        /**
         * \brief Pointer to value under \p key, or nullptr (clap `FlatMap::get`).
         */
        [[nodiscard]] constexpr const mapped_type *find_value(const key_type &key) const {
            const const_iterator it = find_impl(key);
            return it == data_.end() ? nullptr : std::addressof(it->second);
        }

        /**
         * \copydoc find_value
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        [[nodiscard]] constexpr const mapped_type *find_value(const K &key) const {
            const const_iterator it = find_impl(key);
            return it == data_.end() ? nullptr : std::addressof(it->second);
        }

        /**
         * \brief Mutable pointer to the value stored under \p key.
         * \param key Key to look for.
         * \return A pointer to the mapped value, or `nullptr` when \p key is absent.
         */
        [[nodiscard]] constexpr mapped_type *find_value(const key_type &key) {
            const const_iterator it = find_impl(key);
            return it == data_.cend() ? nullptr : std::addressof(mutable_at(it).second);
        }

        /**
         * \copydoc find_value
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        [[nodiscard]] constexpr mapped_type *find_value(const K &key) {
            const const_iterator it = find_impl(key);
            return it == data_.cend() ? nullptr : std::addressof(mutable_at(it).second);
        }

        // -------------------------------------------------------------------
        // Modification
        // -------------------------------------------------------------------

        /**
         * \brief Insert unless key present (`std::map::try_emplace` semantics).
         * \return `{iterator, inserted}`.
         */
        template<class... Args>
            requires std::constructible_from<mapped_type, Args...>
        constexpr std::pair<iterator, bool> try_emplace(key_type key, Args &&... args) {
            const const_iterator lb = lower_bound_impl(key);
            const difference_type off = lb - data_.cbegin();
            if (lb != data_.cend() && !comp_(key, lb->first)) return {lb, false};
            data_.emplace(lb,
                          std::piecewise_construct,
                          std::forward_as_tuple(std::move(key)),
                          std::forward_as_tuple(std::forward<Args>(args)...));
            return {data_.cbegin() + off, true};
        }

        /**
         * \brief Insert or overwrite; `{iterator, inserted}` (`false` = overwritten).
         */
        template<class M>
            requires std::assignable_from<mapped_type &, M> &&
                     std::constructible_from<mapped_type, M>
        constexpr std::pair<iterator, bool> insert_or_assign(key_type key, M &&obj) {
            const const_iterator lb = lower_bound_impl(key);
            const difference_type off = lb - data_.cbegin();
            if (lb != data_.cend() && !comp_(key, lb->first)) {
                mutable_at(lb).second = std::forward<M>(obj);
                return {data_.cbegin() + off, false};
            }
            data_.emplace(lb,
                          std::piecewise_construct,
                          std::forward_as_tuple(std::move(key)),
                          std::forward_as_tuple(std::forward<M>(obj)));
            return {data_.cbegin() + off, true};
        }

        /**
         * \brief Value under \p key; value-initializes if absent.
         */
        constexpr mapped_type &operator[](key_type key)
            requires std::default_initializable<mapped_type> {
            const const_iterator lb = lower_bound_impl(key);
            const difference_type off = lb - data_.cbegin();
            if (lb == data_.cend() || comp_(key, lb->first))
                data_.emplace(lb,
                              std::piecewise_construct,
                              std::forward_as_tuple(std::move(key)),
                              std::forward_as_tuple());
            return data_[static_cast<size_type>(off)].second;
        }

        /**
         * \brief Value under \p key; call \p make only if inserting (clap or_insert_with).
         */
        template<std::invocable F>
            requires std::constructible_from<mapped_type, std::invoke_result_t<F> >
        constexpr mapped_type &or_insert_with(key_type key, F &&make) {
            const const_iterator lb = lower_bound_impl(key);
            const difference_type off = lb - data_.cbegin();
            if (lb == data_.cend() || comp_(key, lb->first))
                data_.emplace(lb,
                              std::piecewise_construct,
                              std::forward_as_tuple(std::move(key)),
                              std::forward_as_tuple(std::invoke(std::forward<F>(make))));
            return data_[static_cast<size_type>(off)].second;
        }

        /**
         * \brief Insert new keys from \p r (existing keys kept).
         */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
        constexpr void insert_range(R &&r) {
            // Raw loop: every step depends on the map as modified by the previous one.
            for (auto &&entry: r) {
                value_type e(std::forward<decltype(entry)>(entry));
                try_emplace(std::move(e.first), std::move(e.second));
            }
        }

        /**
         * \brief Remove \p key; return mapped value or nullopt (clap `remove`).
         */
        constexpr std::optional<mapped_type> erase(const key_type &key) {
            std::optional<value_type> entry = erase_entry(key);
            if (!entry.has_value()) return std::nullopt;
            return std::optional<mapped_type>(std::move(entry.value().second));
        }

        /**
         * \copydoc erase(const key_type&)
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        constexpr std::optional<mapped_type> erase(const K &key) {
            std::optional<value_type> entry = erase_entry(key);
            if (!entry.has_value()) return std::nullopt;
            return std::optional<mapped_type>(std::move(entry.value().second));
        }

        /**
         * \brief Remove \p key; return key+value pair or nullopt.
         */
        constexpr std::optional<value_type> erase_entry(const key_type &key) {
            return erase_entry_impl(key);
        }

        /**
         * \copydoc erase_entry(const key_type&)
         * \tparam K Any type \p Compare can compare against #key_type.
         */
        template<class K>
            requires detail::transparent_compare<Compare>
        constexpr std::optional<value_type> erase_entry(const K &key) {
            return erase_entry_impl(key);
        }

        /**
         * \brief Remove the entry \p pos refers to.
         * \param pos A dereferenceable iterator into this map.
         * \return An iterator to the entry that followed \p pos.
         */
        constexpr iterator erase(const_iterator pos) { return data_.erase(pos); }

        /**
         * \brief Erase entries matching \p pred; return count removed.
         */
        template<class Pred>
            requires std::predicate<Pred &, const_reference>
        friend constexpr size_type erase_if(flat_map &m, Pred pred) {
            const size_type before = m.data_.size();
            const auto gone = std::ranges::remove_if(m.data_, pred);
            m.data_.erase(gone.begin(), gone.end());
            return before - m.data_.size();
        }

        /**
         * \brief Swap contents with \p other in constant time.
         * \param other The map to swap with.
         */
        constexpr void swap(flat_map &other) noexcept {
            data_.swap(other.data_);
            std::ranges::swap(comp_, other.comp_);
        }

        /**
         * \brief ADL swap.
         * \param a First map.
         * \param b Second map.
         */
        friend constexpr void swap(flat_map &a, flat_map &b) noexcept { a.swap(b); }

        /**
         * \brief Entry-wise equality. Comparators are not compared.
         * \param other The map to compare against.
         */
        [[nodiscard]] constexpr bool operator==(const flat_map &other) const {
            return std::ranges::equal(data_, other.data_);
        }

    private:
        /** \brief First entry whose key does not compare before \p key. */
        template<class K>
        [[nodiscard]] constexpr const_iterator lower_bound_impl(const K &key) const {
            return std::ranges::lower_bound(data_, key, comp_, &value_type::first);
        }

        /**
         * \brief lower_bound_impl() plus the "is it actually equal" check.
         *
         * Equality is expressed as `!comp(a, b) && !comp(b, a)`; the first half is
         * implied by lower_bound, so only the second is tested here.
         */
        template<class K>
        [[nodiscard]] constexpr const_iterator find_impl(const K &key) const {
            const const_iterator it = lower_bound_impl(key);
            if (it != data_.end() && !comp_(key, it->first)) return it;
            return data_.end();
        }

        /** \brief Mutable entry under const_iterator (only `.second` is written). */
        [[nodiscard]] constexpr value_type &mutable_at(const_iterator it) {
            return data_[static_cast<size_type>(it - data_.cbegin())];
        }

        /** \brief Shared body of the two erase_entry() overloads. */
        template<class K>
        constexpr std::optional<value_type> erase_entry_impl(const K &key) {
            const const_iterator it = find_impl(key);
            if (it == data_.cend()) return std::nullopt;
            std::optional<value_type> out(std::move(mutable_at(it)));
            data_.erase(it);
            return out;
        }

        container_type data_{};
        key_compare comp_{};
    };
} // namespace clapp
