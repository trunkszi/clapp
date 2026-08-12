/**
 * \file
 * \brief clapp::matched_arg — accumulated state for one argument.
 */

#pragma once

#include <clapp/lex/os_str.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/any_value.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace clapp {

    /**
     * \brief One occurrence's slice into a matched_arg's flat value storage.
     *
     * `-x a b -x c d` yields `{0, 2}` and `{2, 2}` over four flat values.
     * Zero-value occurrences are legitimate (`set_true`); do not treat
     * `count == 0` as "no such occurrence".
     */
    struct value_group {
        std::size_t first = 0; /**< First value index in flat storage. */
        std::size_t count = 0; /**< Values this occurrence contributed. */

        /** \brief Whether this occurrence contributed no values. */
        [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }

        /** \brief One past this occurrence's last value. */
        [[nodiscard]] constexpr std::size_t last() const noexcept { return first + count; }

        /** \brief Member-wise equality. */
        [[nodiscard]] constexpr bool operator==(const value_group&) const noexcept = default;
    };

    namespace detail {

        /**
         * \brief Read a clapp::any_value as \p T, aborting on mismatch.
         *
         * Projection behind clapp::values_ref. By the time a value view exists the
         * stored type has already been checked once; a mismatch here is an invariant
         * break, not a question.
         *
         * \tparam T The element type to project to.
         */
        template<class T>
            requires any_storable<T>
        struct any_value_cast {
            /**
             * \brief Project one stored value.
             * \param value The type-erased value.
             * \return A reference into \p value 's storage.
             */
            [[nodiscard]] const T& operator()(const any_value& value) const {
                return value.get<T>();
            }
        };

        /**
         * \brief Project one value_group onto a typed value range.
         * \tparam T The element type.
         */
        template<class T>
            requires any_storable<T>
        struct typed_group_cast {
            std::span<const any_value> all{}; /**< Flat values; sliced per occurrence. */

            /**
             * \brief Slice out one occurrence.
             * \param group The occurrence boundary.
             * \return The values that occurrence contributed.
             */
            [[nodiscard]] std::ranges::transform_view<std::span<const any_value>, any_value_cast<T>>
            operator()(value_group group) const {
                return std::ranges::transform_view<std::span<const any_value>, any_value_cast<T>>(
                        all.subspan(group.first, group.count), any_value_cast<T>{});
            }
        };

        /** \brief Project one value_group onto the raw bytes it covers. */
        struct raw_group_cast {
            std::span<const os_string> all{}; /**< Flat raw values. */

            /**
             * \brief Slice out one occurrence.
             * \param group The occurrence boundary.
             * \return The raw values that occurrence contributed.
             */
            [[nodiscard]] std::span<const os_string> operator()(value_group group) const {
                return all.subspan(group.first, group.count);
            }
        };

    }  // namespace detail

    /**
     * \brief Lazy sized random-access range of `const T&` over an argument's values.
     *
     * clap's `ValuesRef`; what arg_matches::get_many() returns. Named alias for
     * transform_view — a hand-written range would only risk getting category, size,
     * or reference type wrong.
     *
     * \tparam T The element type.
     *
     * \note Projection is a named struct, never a lambda: a lambda in an alias
     *       template is a distinct closure per TU (ODR violation, no diagnostic).
     */
    template<class T>
        requires any_storable<T>
    using values_ref =
            std::ranges::transform_view<std::span<const any_value>, detail::any_value_cast<T>>;

    /**
     * \brief Lazy range of per-occurrence values_ref. clap's `OccurrencesRef`.
     * \tparam T The element type.
     */
    template<class T>
        requires any_storable<T>
    using occurrences_ref =
            std::ranges::transform_view<std::span<const value_group>, detail::typed_group_cast<T>>;

    /**
     * \brief Lazy range of per-occurrence raw byte spans. clap's `RawOccurrences`.
     */
    using raw_occurrences_ref =
            std::ranges::transform_view<std::span<const value_group>, detail::raw_group_cast>;

    namespace detail {

        /**
         * Compile-time contracts on the three view aliases:
         * - reference type (not by-value — would copy on every traversal);
         * - random-access and sized (clap's ExactSizeIterator / DoubleEndedIterator).
         */
        static_assert(std::ranges::random_access_range<values_ref<int>>);
        static_assert(std::ranges::sized_range<values_ref<int>>);
        static_assert(std::same_as<std::ranges::range_reference_t<values_ref<int>>, const int&>);
        static_assert(std::same_as<std::ranges::range_value_t<values_ref<int>>, int>);

        static_assert(std::ranges::random_access_range<occurrences_ref<int>>);
        static_assert(std::ranges::sized_range<occurrences_ref<int>>);
        static_assert(
                std::same_as<std::ranges::range_value_t<occurrences_ref<int>>, values_ref<int>>);

        static_assert(std::ranges::random_access_range<raw_occurrences_ref>);
        static_assert(std::ranges::sized_range<raw_occurrences_ref>);
        static_assert(std::same_as<std::ranges::range_value_t<raw_occurrences_ref>,
                                   std::span<const os_string>>);

    }  // namespace detail

    /**
     * \brief Accumulated state of one argument or group id.
     *
     * Built by the parser, read through arg_matches. Holds source, indices, type_id,
     * values, raw_values, ignore_case — parallel to clap's `MatchedArg`.
     *
     * \note values() and raw_values() are parallel; append_value() is the only writer.
     *
     * \warning Every span points into this object's vectors and is invalidated by
     *          the next append_value(), push_index(), or start_occurrence(). A live
     *          span across a `remove_*` is the caller's bug.
     *
     * \code
     * clapp::matched_arg m{clapp::any_id::of<int>()};
     * m.start_occurrence();
     * m.append_value(clapp::any_value(std::in_place_type<int>, 22), clapp::os_string{"22"});
     * m.set_source(clapp::value_source::command_line);
     * \endcode
     */
    class matched_arg {
    public:
        /**
         * \brief Empty matched_arg with no declared value type.
         *
         * clap's `MatchedArg::new_group`: a group has no value parser, so its type is
         * whatever members hold. infer_type_id() resolves that later.
         */
        matched_arg() = default;

        /**
         * \brief Empty matched_arg that will hold values of type \p expected.
         *
         * clap's `new_arg` / `new_external` reduced to "the parser's `T`".
         *
         * \param expected Type the value parser produces; default-constructed any_id
         *                 means "unknown" (what for_group() uses).
         */
        explicit matched_arg(any_id expected) noexcept : type_id_(expected) {}

        /**
         * \brief Empty matched_arg for an arg_group id. clap's `new_group`.
         * \return A matched_arg whose has_type_id() is `false`.
         */
        [[nodiscard]] static matched_arg for_group() noexcept { return matched_arg{}; }

        // -------------------------------------------------------------------
        // Accumulation — the parser's side of the interface
        // -------------------------------------------------------------------

        /**
         * \brief Begin a new occurrence. clap's `new_val_group`.
         *
         * Call once per sighting, before appending that sighting's values.
         * Zero-value occurrences are kept (`set_true`).
         */
        void start_occurrence() {
            groups_.push_back(value_group{.first = values_.size(), .count = 0});
        }

        /**
         * \brief Append one parsed value with the bytes it came from. clap's `append_val`.
         *
         * \param value Parsed value from the value_parser.
         * \param raw   Original command-line bytes.
         *
         * \note Opens an occurrence if none was started (clap panics instead).
         * \note \p raw is owning (os_string): argv may not outlive the matches.
         */
        void append_value(any_value value, os_string raw) {
            if (groups_.empty()) start_occurrence();
            values_.push_back(std::move(value));
            raw_values_.push_back(std::move(raw));
            ++groups_.back().count;
        }

        /**
         * \brief Record that a value occupied argv position \p index.
         * \param index Clap-style index; see arg_matches::index_of().
         */
        void push_index(std::size_t index) { indices_.push_back(index); }

        /**
         * \brief Take parsed values out, leaving this empty. clap's `into_vals`.
         *
         * Backs arg_matches::remove_one() and relatives.
         *
         * \return The values, in flat order.
         *
         * \warning Everything derived from the values goes with them — raw_values(),
         *          occurrences(), indices(), and source() are reset so values() and
         *          raw_values() stay parallel. Only type_id() survives. Read what you
         *          need before calling.
         */
        [[nodiscard]] std::vector<any_value> release_values() {
            std::vector<any_value> out = std::move(values_);
            values_.clear();
            raw_values_.clear();
            groups_.clear();
            indices_.clear();
            source_.reset();
            return out;
        }

        /**
         * \brief Merge \p source in, keeping the stronger. clap's `set_source`.
         * \param source The source to merge in.
         * \see clapp::strongest()
         */
        void set_source(clapp::value_source source) noexcept {
            source_ = source_.has_value() ? strongest(*source_, source) : source;
        }

        /**
         * \brief Set whether has_raw_value() folds ASCII case.
         * \param on `true` for an argument declared `ignore_case`.
         */
        void set_ignore_case(bool on) noexcept { ignore_case_ = on; }

        /**
         * \brief Declare the type this argument's value parser produces.
         * \param expected Type id, or default any_id for "unknown, infer it".
         */
        void set_type_id(any_id expected) noexcept { type_id_ = expected; }

        // -------------------------------------------------------------------
        // Source
        // -------------------------------------------------------------------

        /** \brief Whether any source has been recorded yet. */
        [[nodiscard]] bool has_source() const noexcept { return source_.has_value(); }

        /**
         * \brief Where the value came from. clap's `source`.
         * \return `std::nullopt` when no source has been attributed yet.
         */
        [[nodiscard]] std::optional<clapp::value_source> source() const noexcept { return source_; }

        /**
         * \brief Whether the recorded source counts as user-supplied.
         *
         * First half of clap's `check_explicit`: no recorded source counts as
         * explicit (parser has not attributed one yet, not "default supplied it").
         *
         * \return `false` only when a source is recorded and it is default_value.
         */
        [[nodiscard]] bool is_explicit() const noexcept {
            return !source_.has_value() || clapp::is_explicit(*source_);
        }

        /** \brief Whether ASCII case is folded when comparing raw values. */
        [[nodiscard]] bool ignore_case() const noexcept { return ignore_case_; }

        // -------------------------------------------------------------------
        // Type identity
        // -------------------------------------------------------------------

        /**
         * \brief Whether a value type was declared up front.
         *
         * `false` for a group id (no value parser). Predicate form of clap's
         * `Option<AnyValueId>`; any_id already has an empty state with this meaning.
         */
        [[nodiscard]] bool has_type_id() const noexcept { return type_id_.has_value(); }

        /**
         * \brief The declared value type. clap's `type_id`.
         * \return Default-constructed any_id when none was declared.
         */
        [[nodiscard]] any_id type_id() const noexcept { return type_id_; }

        /**
         * \brief Type a caller asking for \p expected should be checked against.
         *
         * Port of clap's `infer_type_id`:
         * 1. declared type, when present;
         * 2. else first stored value whose type is not \p expected;
         * 3. else \p expected (empty group accepts any `T`).
         *
         * \param expected The type the caller asked for.
         * \return The type to compare \p expected against.
         *
         * \note Step 2 looks for a *mismatch*, not the first value's type, so a
         *       heterogeneous group reports the non-`T` member even when the first
         *       member happens to be a `T`.
         */
        [[nodiscard]] any_id infer_type_id(any_id expected) const noexcept {
            if (type_id_.has_value()) return type_id_;
            const auto mismatched = std::ranges::find_if(
                    values_, [expected](const any_value& v) { return v.type() != expected; });
            return mismatched == values_.end() ? expected : mismatched->type();
        }

        // -------------------------------------------------------------------
        // Values
        // -------------------------------------------------------------------

        /**
         * \brief Every parsed value, flat, in occurrence order.
         * \return Span valid until the next mutation; see the class \warning.
         */
        [[nodiscard]] std::span<const any_value> values() const noexcept { return values_; }

        /**
         * \brief Every original byte string, flat and parallel to values().
         * \return Span valid until the next mutation; see the class \warning.
         */
        [[nodiscard]] std::span<const os_string> raw_values() const noexcept { return raw_values_; }

        /**
         * \brief Occurrence boundaries, in the order the argument was seen.
         * \return Span valid until the next mutation; see the class \warning.
         */
        [[nodiscard]] std::span<const value_group> occurrences() const noexcept { return groups_; }

        /** \brief How many values were collected in total. clap's `num_vals`. */
        [[nodiscard]] std::size_t value_count() const noexcept { return values_.size(); }

        /** \brief How many times the argument was seen. */
        [[nodiscard]] std::size_t occurrence_count() const noexcept { return groups_.size(); }

        /**
         * \brief Values in the most recent occurrence. clap's `num_vals_last_group`.
         * \return `0` when no occurrence has been started.
         */
        [[nodiscard]] std::size_t value_count_in_last_occurrence() const noexcept {
            return groups_.empty() ? 0 : groups_.back().count;
        }

        /**
         * \brief Whether no values have been collected.
         *
         * Not the same as occurrence_count() == 0: `set_true` has one occurrence, no values.
         */
        [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

        /**
         * \brief Whether any raw value equals \p wanted.
         *
         * Second half of clap's `check_explicit` / `ArgPredicate::Equals`.
         *
         * \param wanted The bytes to look for.
         * \return `true` when some raw value compares equal, folding ASCII case when
         *         ignore_case() is set.
         *
         * \note Byte comparison (raw need not be valid UTF-8); case fold is ASCII-only.
         */
        [[nodiscard]] bool has_raw_value(os_str wanted) const {
            const bool fold = ignore_case_;
            return std::ranges::any_of(raw_values_, [wanted, fold](const os_string& value) {
                if (!fold) return value.view() == wanted;
                return detail::equals_ignore_ascii_case(value.chars(), wanted.chars());
            });
        }

        // -------------------------------------------------------------------
        // Indices
        // -------------------------------------------------------------------

        /**
         * \brief Every argv position the argument's values occupied.
         * \return Span valid until the next mutation; see the class \warning.
         */
        [[nodiscard]] std::span<const std::size_t> indices() const noexcept { return indices_; }

        /**
         * \brief The \p nth recorded index. clap's `get_index`.
         * \param nth Zero-based position within indices().
         * \return `std::nullopt` when fewer than `nth + 1` indices were recorded.
         */
        [[nodiscard]] std::optional<std::size_t> index_at(std::size_t nth) const noexcept {
            if (nth >= indices_.size()) return std::nullopt;
            return indices_[nth];
        }

        // -------------------------------------------------------------------
        // Comparison
        // -------------------------------------------------------------------

        /**
         * \brief Equality over everything except the parsed values themselves.
         * \param other The matched_arg to compare against.
         *
         * \warning **values() is not compared** — any_value has no `operator==`.
         *          raw_values() is compared (parallel to values()), so two entries
         *          differing only in parsed values would need different parsers from
         *          identical bytes. clap makes the same trade (`vals: _` in PartialEq).
         */
        [[nodiscard]] bool operator==(const matched_arg& other) const {
            return source_ == other.source_ && indices_ == other.indices_ &&
                   type_id_ == other.type_id_ && groups_ == other.groups_ &&
                   ignore_case_ == other.ignore_case_ &&
                   std::ranges::equal(raw_values_,
                                      other.raw_values_,
                                      [](const os_string& a, const os_string& b) {
                                          return a.view() == b.view();
                                      });
        }

    private:
        std::optional<clapp::value_source> source_{};
        std::vector<std::size_t> indices_{};
        any_id type_id_{};
        std::vector<any_value> values_{};
        std::vector<os_string> raw_values_{};
        std::vector<value_group> groups_{};
        bool ignore_case_ = false;
    };

}  // namespace clapp
