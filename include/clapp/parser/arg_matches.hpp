/**
 * \file
 * \brief clapp::arg_matches — the parser's output; what most callers read.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/any_value.hpp>
#include <clapp/util/flat_map.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {
    /**
     * \brief What went wrong when reading arg_matches. clap's `MatchesError`.
     *
     * Both variants are programming errors, never user input.
     */
    enum class matches_error_kind : std::uint8_t {
        /** Stored values are not of the requested type. clap: `Downcast`. */
        downcast,
        /**
         * Id is not an argument or group of the originating command.
         * clap: `UnknownArgument`. Only when has_id_validation() is set.
         */
        unknown_argument,
    };

    /**
     * \brief Stable kebab-case name of \p kind.
     * \param kind The kind to name.
     * \return Empty view for a value outside the enumeration.
     */
    [[nodiscard]] constexpr std::string_view name_of(matches_error_kind kind) noexcept {
        switch (kind) {
            case matches_error_kind::downcast:
                return "downcast";
            case matches_error_kind::unknown_argument:
                return "unknown-argument";
        }
        return {};
    }

    /**
     * \brief Violation of arg_matches assumptions. clap's `MatchesError`.
     *
     * \note Carries any_id (not a string) so callers can branch on types;
     *       to_string() is for humans and built on demand.
     * \note Fully `constexpr` (only thing in this header that is): any_id is a
     *       compile-time identity. The container it reports on cannot exist in a
     *       constant expression.
     */
    class matches_error {
    public:
        /**
         * \brief Requested type is not the stored one.
         * \param expected_type Type the caller asked for.
         * \param actual_type   Type actually stored.
         */
        [[nodiscard]] static constexpr matches_error downcast(any_id expected_type,
                                                              any_id actual_type) noexcept {
            matches_error result;
            result.kind_ = matches_error_kind::downcast;
            result.expected_ = expected_type;
            result.actual_ = actual_type;
            return result;
        }

        /** \brief Id names no argument or group of the originating command. */
        [[nodiscard]] static constexpr matches_error unknown_argument() noexcept {
            matches_error result;
            result.kind_ = matches_error_kind::unknown_argument;
            return result;
        }

        /** \brief Which of the two failures this is. */
        [[nodiscard]] constexpr matches_error_kind kind() const noexcept { return kind_; }

        /**
         * \brief Type the caller asked for.
         * \return Default any_id for matches_error_kind::unknown_argument.
         */
        [[nodiscard]] constexpr any_id expected() const noexcept { return expected_; }

        /**
         * \brief Type actually stored.
         * \return Default any_id for matches_error_kind::unknown_argument.
         */
        [[nodiscard]] constexpr any_id actual() const noexcept { return actual_; }

        /**
         * \brief Human-readable one-line rendering.
         * \return Message matching clap's `Display for MatchesError` minus trailing newline.
         *
         * \note Built with `push_back` via detail::append_bytes, never `operator+` or
         *       `std::string{ptr, n}`. Reachable from `static_assert`; under
         *       `-fsanitize=null` GCC 16 will not fold libstdc++ `_M_mutate`. Trap 10.
         */
        [[nodiscard]] constexpr std::string to_string() const {
            std::string out;
            switch (kind_) {
                case matches_error_kind::downcast:
                    detail::append_bytes(out, "could not downcast to ");
                    detail::append_bytes(out, expected_.name());
                    detail::append_bytes(out, ", need to downcast to ");
                    detail::append_bytes(out, actual_.name());
                    return out;
                case matches_error_kind::unknown_argument:
                    detail::append_bytes(out,
                                         "unknown argument or group id -- make sure you are "
                                         "using the argument id and not the short or long flag");
                    return out;
            }
            return out;
        }

        /** \brief Member-wise equality. */
        [[nodiscard]] constexpr bool operator==(const matches_error &other) const noexcept {
            return kind_ == other.kind_ && expected_ == other.expected_ && actual_ == other.actual_;
        }

    private:
        matches_error_kind kind_ = matches_error_kind::unknown_argument;
        any_id expected_{};
        any_id actual_{};
    };

    namespace detail {
        /**
         * \brief Abort after reporting a matches_error against \p id.
         *
         * Counterpart of clap's `MatchesError::unwrap`. Backs every non-`try_` accessor.
         *
         * \param id    Id the caller asked about.
         * \param error The violation.
         *
         * \note Writes with `std::fwrite` (contract already broken; avoid alloc-heavy
         *       formatting). Message assembled first so one `to_string()` is the only
         *       allocation and still gets written if it is the last.
         * \note **`[[noreturn]]` reporter, not `contract_assert`.** clang-p2996 has no
         *       P2900, and `contract_assert` cannot name the two types. See ADR-0008.
         */
        [[noreturn]] inline void report_matches_error(std::string_view id,
                                                      const matches_error &error) {
            const auto put = [](std::string_view text) {
                (void) std::fwrite(text.data(), 1, text.size(), stderr);
            };
            const std::string detail_text = error.to_string();
            put("clapp: mismatch between the definition and the access of `");
            put(id);
            put("`: ");
            put(detail_text);
            put(" -- this is a bug in the calling program, not in its input.\n");
            std::abort();
        }

        /**
         * \brief Abort because an accessor that cannot report absence found none.
         *
         * clap's `get_flag` / `get_count` panic the same way: both actions declare a
         * default, so absent means the argument was never declared with that action.
         *
         * \param id     Id the caller asked about.
         * \param action Action the accessor requires, as the user would write it.
         */
        [[noreturn]] inline void report_matches_absent(std::string_view id,
                                                       std::string_view action) {
            const auto put = [](std::string_view text) {
                (void) std::fwrite(text.data(), 1, text.size(), stderr);
            };
            put("clapp: argument `");
            put(id);
            put("` has no value; its action should be ");
            put(action);
            put(", which always supplies a default.\n");
            std::abort();
        }

        /**
         * \brief Abort because \p name is not a declared subcommand name.
         *
         * Like clap: diagnoses a programmer typo before checking which subcommand ran.
         */
        [[noreturn]] inline void report_invalid_subcommand_query(std::string_view name) {
            const auto put = [](std::string_view text) {
                (void) std::fwrite(text.data(), 1, text.size(), stderr);
            };
            put("clapp: `");
            put(name);
            put("` is not a name of a subcommand; this is a bug in the calling program, not in "
                "its input.\n");
            std::abort();
        }

        struct subcommand_slot;
    } // namespace detail

    /**
     * \brief Everything one parse produced for one command level.
     *
     * \warning **Subcommand matches are owned; every reference into them dies with
     *          the parent.** subcommand() / subcommand_matches() and value-accessor
     *          spans/pointers borrow owned storage. Deep-copy is safe; a reference
     *          into a child is not after the parent dies. Neither toolchain diagnoses
     *          the dangling. Parse builds bottom-up and moves ownership.
     *
     * \note Copyable/movable; copy is deep. clap shares via Arc; clapp does not.
     *
     * \code
     * const bool verbose = matches.get_flag("verbose");
     * const int port = *matches.get_one<int>("port").value();
     * if (const auto sub = matches.subcommand(); sub && sub->first == "build")
     *     sub->second.get_flag("release");
     * \endcode
     */
    class arg_matches {
    public:
        /**
         * \brief Map from id to accumulated state. clap's `FlatMap<Id, MatchedArg>`.
         *
         * \note `std::less<>` so accessors key by `string_view` without building
         *       arg_id — frozen ids live in `.rodata`; call-site strings do not.
         */
        using arg_map = flat_map<arg_id, matched_arg, std::less<> >;

        /** \brief Empty result: no arguments, no subcommand, no id validation. */
        arg_matches() = default;

        /** \brief Deep-copy including the entire subcommand subtree. */
        arg_matches(const arg_matches &other);

        /** \brief Take over \p other's state. */
        arg_matches(arg_matches &&other) noexcept;

        /** \brief Deep-copy assignment. \return `*this`. */
        arg_matches &operator=(const arg_matches &other);

        /** \brief Move assignment. \return `*this`. */
        arg_matches &operator=(arg_matches &&other) noexcept;

        /** \brief Destroy this level and every level below it. */
        ~arg_matches();

        // -------------------------------------------------------------------
        // Typed values — the aborting half
        // -------------------------------------------------------------------

        /**
         * \brief First value stored for \p id, as \p T. clap's `get_one`.
         *
         * \tparam T Type the argument's value_parser produces.
         * \param  id Argument or group id — **not** a short/long flag spelling.
         * \return Pointer to the value, or nullopt when none. Non-null when engaged;
         *         valid until this object is modified or destroyed.
         *
         * \warning Wrong \p T **terminates the process** after printing both type
         *          names (release and debug). Use try_get_one() when the type is a
         *          question. An argument with `default_value` always reports a value;
         *          ask value_source() to tell default from user input.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::optional<const T *> get_one(std::string_view id) const {
            return unwrap(id, try_get_one<T>(id));
        }

        /**
         * \brief Every value for \p id as `T`s, flat, in order. clap's `get_many`.
         *
         * \tparam T Type the value_parser produces.
         * \param  id Argument or group id.
         * \return Lazy sized random-access range of `const T&`, or nullopt if absent.
         *
         * \warning Aborts on wrong \p T; see get_one(). Range does not extend lifetime.
         * \warning Bind the result to a named variable before iterating (dangling views).
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::optional<values_ref<T> > get_many(std::string_view id) const {
            return unwrap(id, try_get_many<T>(id));
        }

        /**
         * \brief Values for \p id grouped by occurrence. clap's `get_occurrences`.
         *
         * `-x a b -x c d` yields `{a,b}` and `{c,d}`; get_many() yields one flat list.
         *
         * \tparam T Type the value_parser produces.
         * \param  id Argument or group id.
         * \return Lazy range of lazy ranges of `const T&`, or nullopt if absent.
         *
         * \warning Aborts on wrong \p T; see get_one(). Range does not extend lifetime.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::optional<occurrences_ref<T> > get_occurrences(std::string_view id) const {
            return unwrap(id, try_get_occurrences<T>(id));
        }

        /**
         * \brief Original bytes every value of \p id was parsed from. clap's `get_raw`.
         *
         * \param id Argument or group id.
         * \return Contiguous span of owning byte strings, or nullopt if absent.
         *
         * \note **os_string, not os_str** — matches outlive the raw_args buffer; a
         *       span of views into freed storage is the failure to prevent.
         *       os_string converts to os_str, so `for (os_str raw : *m.get_raw("x"))`
         *       still compiles.
         *
         * \warning Span does not extend this object's lifetime; see the class \warning.
         */
        [[nodiscard]] std::optional<std::span<const os_string> > get_raw(std::string_view id) const {
            return unwrap(id, try_get_raw(id));
        }

        /**
         * \brief Original bytes for \p id, grouped by occurrence. clap's `get_raw_occurrences`.
         * \param id Argument or group id.
         * \return Lazy range of `span<const os_string>`, or nullopt if absent.
         * \warning Range does not extend this object's lifetime; see the class \warning.
         */
        [[nodiscard]] std::optional<raw_occurrences_ref>
        get_raw_occurrences(std::string_view id) const {
            return unwrap(id, try_get_raw_occurrences(id));
        }

        /**
         * \brief Value of a `set_true` / `set_false` flag. clap's `get_flag`.
         * \param id Argument id.
         * \return Whether the flag is set.
         *
         * \warning **Aborts when \p id collected no value**, as clap panics.
         *          set_true/set_false always declare a default, so absence means the
         *          argument was declared with another action. Use try_get_one<bool>()
         *          when that must not be fatal.
         */
        [[nodiscard]] bool get_flag(std::string_view id) const {
            const std::optional<const bool *> value = get_one<bool>(id);
            if (!value.has_value()) detail::report_matches_absent(id, "`set_true` or `set_false`");
            return *value.value();
        }

        /**
         * \brief Times a `count` flag was seen. clap's `get_count`.
         * \param id Argument id.
         * \return Occurrence count, saturating at 255.
         *
         * \note Returns count_type (`uint8_t`, clap's `u8`); widening would diverge
         *       from the annotation DSL's deduced `uint8_t verbose` field.
         * \warning Aborts when \p id collected no value; see get_flag().
         */
        [[nodiscard]] count_type get_count(std::string_view id) const {
            const std::optional<const count_type *> value = get_one<count_type>(id);
            if (!value.has_value()) detail::report_matches_absent(id, "`count`");
            return *value.value();
        }

        // -------------------------------------------------------------------
        // Typed values — the reporting half
        // -------------------------------------------------------------------

        /**
         * \brief get_one() without the abort. clap's `try_get_one`.
         * \tparam T Expected value type.
         * \param  id Argument or group id.
         * \return Value pointer, nullopt if absent, or matches_error naming both types.
         * \warning Pointer does not extend this object's lifetime; see class \warning.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::expected<std::optional<const T *>, matches_error>
        try_get_one(std::string_view id) const {
            const std::expected<const matched_arg *, matches_error> found = try_find_typed<T>(id);
            if (!found.has_value()) return std::unexpected(found.error());
            if (found.value() == nullptr) return std::optional<const T *>{};
            const std::span<const any_value> values = found.value()->values();
            if (values.empty()) return std::optional<const T *>{};
            return std::optional<const T *>{std::addressof(values.front().get<T>())};
        }

        /**
         * \brief get_many() without the abort. clap's `try_get_many`.
         * \tparam T Expected value type.
         * \param  id Argument or group id.
         * \return Value range, nullopt if absent, or matches_error.
         * \warning Range does not extend this object's lifetime; see class \warning.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::expected<std::optional<values_ref<T> >, matches_error>
        try_get_many(std::string_view id) const {
            const std::expected<const matched_arg *, matches_error> found = try_find_typed<T>(id);
            if (!found.has_value()) return std::unexpected(found.error());
            if (found.value() == nullptr) return std::optional<values_ref<T> >{};
            return std::optional<values_ref<T> >{
                values_ref<T>(found.value()->values(), detail::any_value_cast<T>{})
            };
        }

        /**
         * \brief get_occurrences() without the abort. clap's `try_get_occurrences`.
         * \tparam T Expected value type.
         * \param  id Argument or group id.
         * \return Occurrence range, nullopt if absent, or matches_error.
         * \warning Range does not extend this object's lifetime; see class \warning.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::expected<std::optional<occurrences_ref<T> >, matches_error>
        try_get_occurrences(std::string_view id) const {
            const std::expected<const matched_arg *, matches_error> found = try_find_typed<T>(id);
            if (!found.has_value()) return std::unexpected(found.error());
            if (found.value() == nullptr) return std::optional<occurrences_ref<T> >{};
            return std::optional<occurrences_ref<T> >{
                occurrences_ref<T>(
                    found.value()->occurrences(),
                    detail::typed_group_cast<T>{.all = found.value()->values()})
            };
        }

        /**
         * \brief get_raw() without the abort. clap's `try_get_raw`.
         * \param id Argument or group id.
         * \return Raw values, nullopt if absent, or matches_error.
         * \warning Span does not extend this object's lifetime; see class \warning.
         */
        [[nodiscard]] std::expected<std::optional<std::span<const os_string> >, matches_error>
        try_get_raw(std::string_view id) const {
            const std::expected<const matched_arg *, matches_error> found = try_find(id);
            if (!found.has_value()) return std::unexpected(found.error());
            if (found.value() == nullptr) return std::optional<std::span<const os_string> >{};
            return std::optional<std::span<const os_string> >{found.value()->raw_values()};
        }

        /**
         * \brief get_raw_occurrences() without the abort. clap's `try_get_raw_occurrences`.
         * \param id Argument or group id.
         * \return Grouped raw values, nullopt if absent, or matches_error.
         * \warning Range does not extend this object's lifetime; see class \warning.
         */
        [[nodiscard]] std::expected<std::optional<raw_occurrences_ref>, matches_error>
        try_get_raw_occurrences(std::string_view id) const {
            const std::expected<const matched_arg *, matches_error> found = try_find(id);
            if (!found.has_value()) return std::unexpected(found.error());
            if (found.value() == nullptr) return std::optional<raw_occurrences_ref>{};
            return std::optional<raw_occurrences_ref>{
                raw_occurrences_ref(
                    found.value()->occurrences(),
                    detail::raw_group_cast{.all = found.value()->raw_values()})
            };
        }

        // -------------------------------------------------------------------
        // Typed values — the moving-out half
        // -------------------------------------------------------------------

        /**
         * \brief Move the first value for \p id out as \p T. clap's `remove_one`.
         *
         * Prefer over get_one() for ownership: hands back the value itself, not a
         * pointer that dies with this object.
         *
         * \tparam T Type the value_parser produces.
         * \param  id Argument or group id.
         * \return The value, or nullopt when none. \p id is removed either way.
         *
         * \warning Aborts on wrong \p T; see get_one(). Nothing is removed then —
         *          the entry is put back before abort (as clap's `try_remove_arg_t`).
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::optional<T> remove_one(std::string_view id) {
            return unwrap(id, try_remove_one<T>(id));
        }

        /**
         * \brief Move every value for \p id out as `T`s. clap's `remove_many`.
         * \tparam T Type the value_parser produces.
         * \param  id Argument or group id.
         * \return The values, or nullopt if absent.
         * \note Materialized (not lazy): moving destroys storage a lazy range needs.
         * \warning Aborts on wrong \p T; see remove_one().
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::optional<std::vector<T> > remove_many(std::string_view id) {
            return unwrap(id, try_remove_many<T>(id));
        }

        /**
         * \brief Move values for \p id out, grouped by occurrence. clap's `remove_occurrences`.
         * \tparam T Type the value_parser produces.
         * \param  id Argument or group id.
         * \return One inner vector per occurrence, or nullopt if absent.
         * \warning Aborts on wrong \p T; see remove_one().
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::optional<std::vector<std::vector<T> > >
        remove_occurrences(std::string_view id) {
            return unwrap(id, try_remove_occurrences<T>(id));
        }

        /**
         * \brief remove_one() without the abort. clap's `try_remove_one`.
         * \tparam T Expected value type.
         * \param  id Argument or group id.
         * \return Value, nullopt if absent, or matches_error (nothing removed then).
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::expected<std::optional<T>, matches_error>
        try_remove_one(std::string_view id) {
            std::expected<std::optional<matched_arg>, matches_error> taken = try_take_arg<T>(id);
            if (!taken.has_value()) return std::unexpected(taken.error());
            if (!taken.value().has_value()) return std::optional<T>{};
            std::vector<any_value> values = taken.value().value().release_values();
            if (values.empty()) return std::optional<T>{};
            return values.front().take<T>();
        }

        /**
         * \brief remove_many() without the abort. clap's `try_remove_many`.
         * \tparam T Expected value type.
         * \param  id Argument or group id.
         * \return Values, nullopt if absent, or matches_error (nothing removed then).
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::expected<std::optional<std::vector<T> >, matches_error>
        try_remove_many(std::string_view id) {
            std::expected<std::optional<matched_arg>, matches_error> taken = try_take_arg<T>(id);
            if (!taken.has_value()) return std::unexpected(taken.error());
            if (!taken.value().has_value()) return std::optional<std::vector<T> >{};
            std::vector<any_value> values = taken.value().value().release_values();
            std::vector<T> out;
            out.reserve(values.size());
            for (any_value &value: values) out.push_back(std::move(value.take<T>()).value());
            return std::optional<std::vector<T> >{std::move(out)};
        }

        /**
         * \brief remove_occurrences() without the abort. clap's `try_remove_occurrences`.
         * \tparam T Expected value type.
         * \param  id Argument or group id.
         * \return Grouped values, nullopt if absent, or matches_error (nothing removed then).
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::expected<std::optional<std::vector<std::vector<T> > >, matches_error>
        try_remove_occurrences(std::string_view id) {
            std::expected<std::optional<matched_arg>, matches_error> taken = try_take_arg<T>(id);
            if (!taken.has_value()) return std::unexpected(taken.error());
            if (!taken.value().has_value()) return std::optional<std::vector<std::vector<T> > >{};
            matched_arg &entry_ref = taken.value().value();
            // The boundaries have to be copied out before release_values(), which
            // clears them along with everything else derived from the values.
            const std::vector<value_group> groups(entry_ref.occurrences().begin(),
                                                  entry_ref.occurrences().end());
            std::vector<any_value> values = entry_ref.release_values();
            std::vector<std::vector<T> > out;
            out.reserve(groups.size());
            for (const value_group group: groups) {
                std::vector<T> one;
                one.reserve(group.count);
                for (std::size_t i = group.first; i < group.last(); ++i)
                    one.push_back(std::move(values[i].take<T>()).value());
                out.push_back(std::move(one));
            }
            return std::optional<std::vector<std::vector<T> > >{std::move(out)};
        }

        // -------------------------------------------------------------------
        // Presence, provenance, position
        // -------------------------------------------------------------------

        /**
         * \brief Whether any value is recorded for \p id. clap's `contains_id`.
         * \param id Argument or group id.
         * \return `true` when the parse recorded the id at all.
         *
         * \warning **`true` for an argument that only got its `default_value`.** Ask
         *          value_source() to distinguish; this answers "is there a value", not
         *          "did the user supply one".
         */
        [[nodiscard]] bool contains_id(std::string_view id) const {
            return unwrap(id, try_contains_id(id));
        }

        /**
         * \brief contains_id() without the abort. clap's `try_contains_id`.
         * \param id Argument or group id.
         * \return Whether present, or matches_error if undeclared and validation is on.
         */
        [[nodiscard]] std::expected<bool, matches_error>
        try_contains_id(std::string_view id) const {
            if (!is_valid_id(id)) return std::unexpected(matches_error::unknown_argument());
            return args_.contains(id);
        }

        /**
         * \brief Where the value for \p id came from. clap's `value_source`.
         * \param id Argument or group id.
         * \return Source, or nullopt when absent or unattributed.
         */
        [[nodiscard]] std::optional<clapp::value_source> value_source(std::string_view id) const {
            const matched_arg *found = find_arg(id);
            if (found == nullptr) return std::nullopt;
            return found->source();
        }

        /**
         * \brief Whether anything was supplied other than defaults. clap's `args_present`.
         * \note Says nothing about subcommands; ask has_subcommand().
         */
        [[nodiscard]] bool args_present() const {
            return std::ranges::any_of(args_.values(), [](const matched_arg &entry) {
                return entry.has_source() && clapp::is_explicit(entry.source().value());
            });
        }

        /**
         * \brief First clap-style position a value of \p id occupied. clap's `index_of`.
         * \param id Argument or group id.
         * \return Index, or nullopt when absent or none recorded.
         *
         * \note **Clap indices, not argv indices.** Flag records the switch position;
         *       option records its values' positions; bundled/delimited args are
         *       counted as if written out, so numbering may go past `argv`.
         */
        [[nodiscard]] std::optional<std::size_t> index_of(std::string_view id) const {
            const matched_arg *found = find_arg(id);
            if (found == nullptr) return std::nullopt;
            return found->index_at(0);
        }

        /**
         * \brief Every clap-style position values of \p id occupied. clap's `indices_of`.
         * \param id Argument or group id.
         * \return Indices, or nullopt if absent.
         *
         * \note Needed when interleaved option order cannot be recovered from values
         *       alone. Returns real index count (clap's size_hint uses value count).
         */
        [[nodiscard]] std::optional<std::span<const std::size_t> >
        indices_of(std::string_view id) const {
            const matched_arg *found = find_arg(id);
            if (found == nullptr) return std::nullopt;
            return found->indices();
        }

        /**
         * \brief Every id the parse recorded, in key order. clap's `ids`.
         * \return Lazy view of `const arg_id&`, valid while this object lives.
         *
         * \warning clap uses *insertion* order; flat_map is sorted, so this is **name**
         *          order. Nothing user-visible may depend on it — see flat_map.
         */
        [[nodiscard]] auto ids() const noexcept { return args_.keys(); }

        /** \brief How many ids the parse recorded. */
        [[nodiscard]] std::size_t arg_count() const noexcept { return args_.size(); }

        /**
         * \brief Whether the parse recorded no ids at all.
         * \note Says nothing about subcommands; ask has_subcommand().
         */
        [[nodiscard]] bool empty() const noexcept { return args_.empty(); }

        /**
         * \brief Whole id-to-state map.
         * \return Reference valid while this object lives.
         */
        [[nodiscard]] const arg_map &args() const noexcept { return args_; }

        /**
         * \brief Monotonic ordinal when \p id's current row was inserted.
         *
         * Parser metadata for clap's insertion-ordered diagnostics while public
         * flat_map stays name-ordered.
         *
         * \return Ordinal, or nullopt when \p id has no current row.
         */
        [[nodiscard]] std::optional<std::size_t>
        insertion_ordinal_of(std::string_view id) const noexcept {
            const std::size_t *found = insertion_ordinals_.find_value(id);
            if (found == nullptr) return std::nullopt;
            return *found;
        }

        /**
         * \brief Accumulated state for \p id.
         * \param id Argument or group id.
         * \return Pointer to state, or nullptr if absent. Valid until next modification.
         *
         * \note Pointer (not optional), matching flat_map::find_value(). Trap 10
         *       forbids null-as-absent only in constant expressions; any_value cannot
         *       exist there. contains_id() is the predicate form.
         */
        [[nodiscard]] const matched_arg *find_arg(std::string_view id) const noexcept {
            return args_.find_value(id);
        }

        /** \copydoc find_arg(std::string_view) const */
        [[nodiscard]] matched_arg *find_arg(std::string_view id) noexcept {
            return args_.find_value(id);
        }

        // -------------------------------------------------------------------
        // Subcommands
        // -------------------------------------------------------------------

        /** \brief Whether a subcommand ran at this level. */
        [[nodiscard]] bool has_subcommand() const noexcept;

        /**
         * \brief The name of the subcommand that ran. clap's `subcommand_name`.
         * \return `std::nullopt` when no subcommand ran.
         */
        [[nodiscard]] std::optional<std::string_view> subcommand_name() const noexcept;

        /**
         * \brief Name and matches of the subcommand that ran. clap's `subcommand`.
         * \return The pair, or nullopt when no subcommand ran.
         *
         * \warning Reference half points into this object's storage; dies with parent.
         *          Copying the pair copies the view and the reference, not the child.
         */
        [[nodiscard]] std::optional<std::pair<std::string_view, const arg_matches &> >
        subcommand() const;

        /**
         * \brief Matches of subcommand \p name if that is the one that ran.
         *        clap's `subcommand_matches`.
         * \param name Subcommand name to ask for.
         * \return Child matches, or nullptr when none / a different one ran.
         *
         * \note Aborts when validation is on and \p name is neither empty nor a
         *       declared name. Validation runs before absence is considered.
         * \warning Pointee dies with this object; see class \warning.
         */
        [[nodiscard]] const arg_matches *subcommand_matches(std::string_view name) const;

        /**
         * \brief Detach the subcommand that ran. clap's `remove_subcommand`.
         * \return Name and child matches by value, or nullopt. No subcommand afterwards.
         * \note Keep a child alive past its parent without copying the whole subtree.
         */
        [[nodiscard]] std::optional<std::pair<std::string, arg_matches> > remove_subcommand();

        /**
         * \brief Record that \p name ran with \p matches. Replaces any prior subcommand.
         * \param name    Subcommand name as declared.
         * \param matches Child result, moved in.
         */
        void set_subcommand(std::string name, arg_matches matches);

        // -------------------------------------------------------------------
        // Accumulation — the parser's side of the interface
        // -------------------------------------------------------------------

        /**
         * \brief State for \p id, creating empty if absent. clap's `or_insert_with`.
         * \param id Argument or group id.
         * \return Reference valid until the next insertion.
         * \note Not `[[nodiscard]]`: discarding to ensure the id exists is valid use.
         */
        matched_arg &entry(arg_id id) {
            record_first_insertion(id);
            return args_.or_insert_with(id, [] { return matched_arg{}; });
        }

        /**
         * \brief Store \p value under \p id, replacing anything there.
         * \param id    Argument or group id.
         * \param value State to store, moved in.
         * \return Reference to stored state, valid until next insertion.
         */
        matched_arg &insert_arg(arg_id id, matched_arg value) {
            record_first_insertion(id);
            args_.insert_or_assign(id, std::move(value));
            matched_arg *stored = args_.find_value(id.name());
            return *stored;
        }

        /**
         * \brief Drop everything recorded for \p id. clap's clear without validation.
         * \param id Argument or group id.
         * \return Whether anything was recorded for it.
         */
        bool erase_arg(std::string_view id) {
            const bool erased = args_.erase_entry(id).has_value();
            static_cast<void>(insertion_ordinals_.erase_entry(id));
            return erased;
        }

        /**
         * \brief erase_arg() with id validation. clap's `try_clear_id`.
         * \param id Argument or group id.
         * \return Whether removed, or matches_error if undeclared and validation is on.
         */
        [[nodiscard]] std::expected<bool, matches_error> try_clear_id(std::string_view id) {
            if (!is_valid_id(id)) return std::unexpected(matches_error::unknown_argument());
            return erase_arg(id);
        }

        // -------------------------------------------------------------------
        // Id validation
        // -------------------------------------------------------------------

        /**
         * \brief Declare which ids the originating command has.
         *
         * Turns "you asked for `--verbose` instead of `verbose`" from silent nullopt
         * into matches_error. clap has the same list only under `debug_assertions`.
         *
         * \param ids Every argument and group id, any order.
         *
         * \note **Opt-in, not build-mode dependent.** clap release silently accepts any
         *       id; here the parser sets it once and all presets agree.
         */
        void set_valid_ids(std::vector<arg_id> ids) {
            valid_args_ = std::move(ids);
            validate_ids_ = true;
        }

        /**
         * \brief Declare which subcommand names the originating command has.
         * \param names Every declared subcommand name.
         */
        void set_valid_subcommands(std::vector<std::string> names) {
            valid_subcommands_ = std::move(names);
            validate_subcommands_ = true;
        }

        /**
         * \brief Whether set_valid_ids() has been called.
         *
         * \note Explicit bool, not "list empty" — a command with no args is real;
         *       empty-list-as-off would make it un-validatable. Trap 10 shape.
         */
        [[nodiscard]] bool has_id_validation() const noexcept { return validate_ids_; }

        /** \brief Whether set_valid_subcommands() has been called. */
        [[nodiscard]] bool has_subcommand_validation() const noexcept {
            return validate_subcommands_;
        }

        /**
         * \brief Whether \p id may be asked about.
         * \param id The id to test.
         * \return `true` when validation is off, id is empty (external), or declared.
         */
        [[nodiscard]] bool is_valid_id(std::string_view id) const noexcept {
            if (!validate_ids_) return true;
            if (id.empty()) return true; // clap's Id::EXTERNAL
            return std::ranges::any_of(valid_args_,
                                       [id](const arg_id &known) { return known == id; });
        }

        /**
         * \brief Whether \p name may be asked about. clap's `is_valid_subcommand`.
         * \param name Subcommand name to test.
         */
        [[nodiscard]] bool is_valid_subcommand(std::string_view name) const noexcept {
            if (!validate_subcommands_) return true;
            if (name.empty()) return true;
            return std::ranges::any_of(valid_subcommands_, [name](const std::string &known) {
                return std::string_view{known} == name;
            });
        }

        // -------------------------------------------------------------------
        // Comparison
        // -------------------------------------------------------------------

        /**
         * \brief Equality over recorded arguments and the subcommand subtree.
         * \param other Matches to compare against.
         * \note Valid-id lists are not compared (command metadata, not matches).
         */
        [[nodiscard]] bool operator==(const arg_matches &other) const;

        /** \brief Exchange contents with \p other. */
        void swap(arg_matches &other) noexcept;

        /** \brief ADL swap. */
        friend void swap(arg_matches &a, arg_matches &b) noexcept { a.swap(b); }

    private:
        /** Assign an ordinal once for the current lifetime of \p id's row. */
        void record_first_insertion(const arg_id &id) {
            if (insertion_ordinals_.contains(id.name())) return;
            insertion_ordinals_.insert_or_assign(id, next_insertion_ordinal_++);
        }

        /**
         * \brief Look \p id up after checking it is askable.
         * \return State, nullptr if absent, or matches_error.
         */
        [[nodiscard]] std::expected<const matched_arg *, matches_error>
        try_find(std::string_view id) const {
            if (!is_valid_id(id)) return std::unexpected(matches_error::unknown_argument());
            return args_.find_value(id);
        }

        /**
         * \brief try_find() plus stored-type check. clap's `try_get_arg_t`.
         * \tparam T Type the caller asked for.
         * \note Via infer_type_id(); groups check against members' actual types.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::expected<const matched_arg *, matches_error>
        try_find_typed(std::string_view id) const {
            const std::expected<const matched_arg *, matches_error> found = try_find(id);
            if (!found.has_value() || found.value() == nullptr) return found;
            const any_id expected_type = any_id::of<T>();
            const any_id actual_type = found.value()->infer_type_id(expected_type);
            if (actual_type != expected_type)
                return std::unexpected(matches_error::downcast(expected_type, actual_type));
            return found;
        }

        /**
         * \brief Remove \p id's entry only if its values really are `T`s.
         *
         * clap's `try_remove_arg_t`: on type mismatch the entry is **put back** before
         * the error, so a failed remove leaves matches unchanged. Without that, a
         * wrong-type guess silently loses the values.
         *
         * \tparam T Type the caller asked for.
         * \param  id Argument or group id.
         * \return Detached entry, nullopt if absent, or matches_error.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] std::expected<std::optional<matched_arg>, matches_error>
        try_take_arg(std::string_view id) {
            if (!is_valid_id(id)) return std::unexpected(matches_error::unknown_argument());
            matched_arg *found = args_.find_value(id);
            if (found == nullptr) return std::optional<matched_arg>{};
            const any_id expected_type = any_id::of<T>();
            const any_id actual_type = found->infer_type_id(expected_type);
            if (actual_type != expected_type)
                return std::unexpected(matches_error::downcast(expected_type, actual_type));

            std::optional<arg_map::value_type> entry = args_.erase_entry(id);
            static_cast<void>(insertion_ordinals_.erase_entry(id));
            return std::optional<matched_arg>{std::move(entry.value().second)};
        }

        /**
         * \brief Return \p result's value, or abort reporting its error.
         *
         * clap's `MatchesError::unwrap`. Static so every aborting accessor is one line.
         *
         * \tparam T Success type.
         * \param  id     Id for the message.
         * \param  result What the `try_` accessor returned.
         */
        template<class T>
        [[nodiscard]] static T unwrap(std::string_view id,
                                      std::expected<T, matches_error> &&result) {
            if (result.has_value()) return std::move(result).value();
            detail::report_matches_error(id, result.error());
        }

        arg_map args_{};
        flat_map<arg_id, std::size_t, std::less<> > insertion_ordinals_{};
        std::size_t next_insertion_ordinal_ = 0;
        std::vector<arg_id> valid_args_{};
        std::vector<std::string> valid_subcommands_{};
        std::unique_ptr<detail::subcommand_slot> subcommand_{};
        bool validate_ids_ = false;
        bool validate_subcommands_ = false;
    };

    namespace detail {
        /**
         * \brief Name-plus-matches pair a parent keeps for its child. clap's `SubCommand`.
         *
         * Separate type: recursion needs indirection; one heap block for the pair.
         */
        struct subcommand_slot {
            std::string name; /**< Owned; external names come from argv. */
            arg_matches matches; /**< Child parse result. */
        };
    } // namespace detail

    // Out-of-class definitions: subcommand_slot cannot be complete inside arg_matches.
    // Destructor must live here — unique_ptr<incomplete> needs a complete deleter.


    inline arg_matches::arg_matches(const arg_matches &other)
        : args_(other.args_),
          insertion_ordinals_(other.insertion_ordinals_),
          next_insertion_ordinal_(other.next_insertion_ordinal_),
          valid_args_(other.valid_args_),
          valid_subcommands_(other.valid_subcommands_),
          subcommand_(other.subcommand_
                          ? std::make_unique<detail::subcommand_slot>(*other.subcommand_)
                          : nullptr),
          validate_ids_(other.validate_ids_),
          validate_subcommands_(other.validate_subcommands_) {
    }

    inline arg_matches::arg_matches(arg_matches &&other) noexcept = default;

    inline arg_matches &arg_matches::operator=(const arg_matches &other) {
        arg_matches copy(other);
        swap(copy);
        return *this;
    }

    inline arg_matches &arg_matches::operator=(arg_matches &&other) noexcept = default;

    inline arg_matches::~arg_matches() = default;

    inline void arg_matches::swap(arg_matches &other) noexcept {
        args_.swap(other.args_);
        insertion_ordinals_.swap(other.insertion_ordinals_);
        std::swap(next_insertion_ordinal_, other.next_insertion_ordinal_);
        valid_args_.swap(other.valid_args_);
        valid_subcommands_.swap(other.valid_subcommands_);
        subcommand_.swap(other.subcommand_);
        std::swap(validate_ids_, other.validate_ids_);
        std::swap(validate_subcommands_, other.validate_subcommands_);
    }

    inline bool arg_matches::has_subcommand() const noexcept { return subcommand_ != nullptr; }

    inline std::optional<std::string_view> arg_matches::subcommand_name() const noexcept {
        if (subcommand_ == nullptr) return std::nullopt;
        return std::string_view{subcommand_->name};
    }

    inline std::optional<std::pair<std::string_view, const arg_matches &> >
    arg_matches::subcommand() const {
        if (subcommand_ == nullptr) return std::nullopt;
        return std::pair<std::string_view, const arg_matches &>{
            std::string_view{subcommand_->name},
            subcommand_->matches
        };
    }

    inline const arg_matches *arg_matches::subcommand_matches(std::string_view name) const {
        if (!is_valid_subcommand(name)) detail::report_invalid_subcommand_query(name);
        if (subcommand_ == nullptr) return nullptr;
        if (std::string_view{subcommand_->name} != name) return nullptr;
        return std::addressof(subcommand_->matches);
    }

    inline std::optional<std::pair<std::string, arg_matches> > arg_matches::remove_subcommand() {
        if (subcommand_ == nullptr) return std::nullopt;
        std::unique_ptr<detail::subcommand_slot> taken = std::move(subcommand_);
        return std::pair<std::string, arg_matches>{
            std::move(taken->name),
            std::move(taken->matches)
        };
    }

    inline void arg_matches::set_subcommand(std::string name, arg_matches matches) {
        subcommand_ = std::make_unique<detail::subcommand_slot>(
            detail::subcommand_slot{.name = std::move(name), .matches = std::move(matches)});
    }

    inline bool arg_matches::operator==(const arg_matches &other) const {
        if (args_ != other.args_) return false;
        if (has_subcommand() != other.has_subcommand()) return false;
        if (!has_subcommand()) return true;
        return subcommand_->name == other.subcommand_->name &&
               subcommand_->matches == other.subcommand_->matches;
    }
} // namespace clapp
