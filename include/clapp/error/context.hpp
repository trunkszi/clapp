/**
 * \file
 * \brief Error context payloads: clapp::context_kind, clapp::context_value, clapp::cow_str.
 */

#pragma once

#include <clapp/output/styled_str.hpp>
#include <clapp/util/str.hpp>

#include <array>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace clapp {

    /**
     * \brief What one piece of error information means. clap's `ContextKind`.
     *
     * Seventeen enumerators in clap's declaration order.
     *
     * \note Spellings follow clap, including singular names that hold lists
     *       (valid_value, valid_subcommand): the kind names the element's role, not
     *       the container shape.
     */
    enum class context_kind : std::uint8_t {
        /** The subcommand that caused the error. */
        invalid_subcommand,
        /** The argument that caused the error. */
        invalid_arg,
        /** Arguments already present that the invalid one collides with. */
        prior_arg,
        /** The subcommands that would have been accepted. */
        valid_subcommand,
        /** The values that would have been accepted. */
        valid_value,
        /** The value that was rejected. */
        invalid_value,
        /** How many values were actually supplied. */
        actual_num_values,
        /** How many values were required. */
        expected_num_values,
        /** The lower bound on the number of values. */
        min_values,
        /** A command the user probably meant. */
        suggested_command,
        /** A subcommand the user probably meant. */
        suggested_subcommand,
        /** An argument the user probably meant. */
        suggested_arg,
        /** A value the user probably meant. */
        suggested_value,
        /** A value that should have followed `--`. */
        trailing_arg,
        /** Free-form advice, already styled — clap's `Suggested`, rendered as `tip:`. */
        suggested,
        /** The usage line, already rendered by `clapp::output`. */
        usage,
        /** An opaque message from the application. */
        custom,
    };

    /** \brief How many clapp::context_kind values there are. */
    inline constexpr std::size_t context_kind_count = 17;

    /** \brief Every clapp::context_kind, in declaration order. */
    inline constexpr std::array<context_kind, context_kind_count> all_context_kinds{
            context_kind::invalid_subcommand,
            context_kind::invalid_arg,
            context_kind::prior_arg,
            context_kind::valid_subcommand,
            context_kind::valid_value,
            context_kind::invalid_value,
            context_kind::actual_num_values,
            context_kind::expected_num_values,
            context_kind::min_values,
            context_kind::suggested_command,
            context_kind::suggested_subcommand,
            context_kind::suggested_arg,
            context_kind::suggested_value,
            context_kind::trailing_arg,
            context_kind::suggested,
            context_kind::usage,
            context_kind::custom,
    };

    /**
     * \brief Human-readable label for \p kind. clap's `ContextKind::as_str`.
     * \param kind The kind to label.
     * \return Title-cased English, or `nullopt` for usage and custom (payload is already a message).
     */
    [[nodiscard]] constexpr std::optional<std::string_view> describe(context_kind kind) noexcept {
        switch (kind) {
        case context_kind::invalid_subcommand:
            return "Invalid Subcommand";
        case context_kind::invalid_arg:
            return "Invalid Argument";
        case context_kind::prior_arg:
            return "Prior Argument";
        case context_kind::valid_subcommand:
            return "Valid Subcommand";
        case context_kind::valid_value:
            return "Valid Value";
        case context_kind::invalid_value:
            return "Invalid Value";
        case context_kind::actual_num_values:
            return "Actual Number of Values";
        case context_kind::expected_num_values:
            return "Expected Number of Values";
        case context_kind::min_values:
            return "Minimum Number of Values";
        case context_kind::suggested_command:
            return "Suggested Command";
        case context_kind::suggested_subcommand:
            return "Suggested Subcommand";
        case context_kind::suggested_arg:
            return "Suggested Argument";
        case context_kind::suggested_value:
            return "Suggested Value";
        case context_kind::trailing_arg:
            return "Trailing Argument";
        case context_kind::suggested:
            return "Suggested";
        case context_kind::usage:
        case context_kind::custom:
            return std::nullopt;
        }
        return std::nullopt;
    }

    /**
     * \brief Kebab-cased spelling of \p kind (diagnostics and tests).
     * \param kind The kind to spell.
     * \return View into a string literal, valid for the program lifetime.
     */
    [[nodiscard]] constexpr std::string_view name_of(context_kind kind) noexcept {
        switch (kind) {
        case context_kind::invalid_subcommand:
            return "invalid-subcommand";
        case context_kind::invalid_arg:
            return "invalid-arg";
        case context_kind::prior_arg:
            return "prior-arg";
        case context_kind::valid_subcommand:
            return "valid-subcommand";
        case context_kind::valid_value:
            return "valid-value";
        case context_kind::invalid_value:
            return "invalid-value";
        case context_kind::actual_num_values:
            return "actual-num-values";
        case context_kind::expected_num_values:
            return "expected-num-values";
        case context_kind::min_values:
            return "min-values";
        case context_kind::suggested_command:
            return "suggested-command";
        case context_kind::suggested_subcommand:
            return "suggested-subcommand";
        case context_kind::suggested_arg:
            return "suggested-arg";
        case context_kind::suggested_value:
            return "suggested-value";
        case context_kind::trailing_arg:
            return "trailing-arg";
        case context_kind::suggested:
            return "suggested";
        case context_kind::usage:
            return "usage";
        case context_kind::custom:
            return "custom";
        }
        return {};
    }

    /**
     * \brief A string that is either borrowed from static storage or owned.
     *
     * Rust's `Cow<'static, str>`. Use borrowed() for `.rodata` / frozen specs;
     * owned() when the bytes may be transient.
     *
     * \code
     *     auto arg  = clapp::cow_str::borrowed(spec.get_long());
     *     auto seen = clapp::cow_str::owned(token);
     * \endcode
     *
     * \warning borrowed() takes a promise it cannot verify: the bytes must outlive
     *          every copy of the error. `.rodata` — string literals and anything
     *          `std::define_static_string` produced — always qualifies. A
     *          `std::string` local, an `os_string` owned by a `raw_args` about to
     *          go out of scope, and a `std::format` result never do. When in doubt
     *          use owned(): one allocation on a path that is about to print and exit.
     *
     * \note No implicit conversion from `string_view` or `const char*` — an implicit
     *       borrow is the mistake this type exists to make visible. `std::string`
     *       converts implicitly because moving one in can only be owning.
     */
    class cow_str {
    public:
        /**
         * \brief An empty, owning string.
         * \note Owning so a value-initialized cow_str never claims to borrow nothing.
         */
        constexpr cow_str() = default;

        /**
         * \brief Take ownership of \p value.
         * \param value The string to move in.
         * \note Implicit: owning construction has no lifetime hazard to flag.
         */
        constexpr cow_str(std::string value)  // NOLINT(google-explicit-constructor)
            : owned_(std::move(value)), owns_(true) {}

        /**
         * \brief Borrow \p text, which must outlive every copy of this object.
         * \param text Bytes in static storage; see the class \warning.
         * \return A borrowing cow_str.
         */
        [[nodiscard]] static constexpr cow_str borrowed(std::string_view text) noexcept {
            cow_str result;
            result.borrowed_ = text;
            result.owns_     = false;
            return result;
        }

        /**
         * \brief Copy \p text into an owning cow_str.
         * \param text Bytes to copy; may be transient.
         * \return An owning cow_str.
         * \note Copies via clapp::detail::append_bytes (`std::string{text}` is not a
         *       constant expression under `-fsanitize=null`).
         */
        [[nodiscard]] static constexpr cow_str owned(std::string_view text) {
            std::string copy;
            detail::append_bytes(copy, text);
            return cow_str{std::move(copy)};
        }

        /**
         * \brief Whether the bytes are owned rather than borrowed.
         * \note A `bool` rather than a null test on #borrowed_ (trap 10); public so
         *       callers do not compare the pointer.
         */
        [[nodiscard]] constexpr bool is_owned() const noexcept { return owns_; }

        /**
         * \brief The bytes in either state.
         * \return A view valid while this object lives and, when borrowed, its target.
         */
        [[nodiscard]] constexpr std::string_view view() const noexcept {
            return owns_ ? std::string_view{owned_} : borrowed_;
        }

        /** \brief Whether there are no bytes. */
        [[nodiscard]] constexpr bool empty() const noexcept { return view().empty(); }

        /** \brief How many bytes there are. */
        [[nodiscard]] constexpr std::size_t size() const noexcept { return view().size(); }

        /** \brief Equality by content (borrowed and owned of the same bytes match). */
        [[nodiscard]] constexpr bool operator==(const cow_str& other) const noexcept {
            return view() == other.view();
        }

        /** \brief Equality against a plain view, by content. */
        [[nodiscard]] constexpr bool operator==(std::string_view other) const noexcept {
            return view() == other;
        }

    private:
        std::string owned_{};
        std::string_view borrowed_{};
        // Defaults to owning: see the default constructor. borrowed() clears it.
        bool owns_ = true;
    };

    /**
     * \brief Which alternative a clapp::context_value holds.
     *
     * clap models this as the `ContextValue` enum; C++ needs a separate discriminant
     * so callers can switch without `std::visit`.
     */
    enum class context_value_kind : std::uint8_t {
        /** clap's `ContextValue::None` — the kind is self-sufficient. */
        none,
        /** clap's `ContextValue::Bool`. */
        boolean,
        /** clap's `ContextValue::String`. */
        string,
        /** clap's `ContextValue::Strings`. */
        strings,
        /** clap's `ContextValue::StyledStr`. */
        styled,
        /** clap's `ContextValue::StyledStrs`. */
        styled_list,
        /** clap's `ContextValue::Number`. */
        number,
    };

    /**
     * \brief One piece of error information. clap's `ContextValue`.
     *
     * Seven shapes; which a given context_kind carries is not fixed by the kind
     * (InvalidArg may be String or Strings). Accessors report failure; use kind().
     *
     * \note Named factories only — `context_value{true}` vs `{1}` would be one
     *       overload accident apart (Bool vs Number).
     */
    class context_value {
    public:
        /** \brief The self-sufficient value: the kind says everything. */
        constexpr context_value() = default;

        /** \brief clap's `ContextValue::None`. */
        [[nodiscard]] static constexpr context_value none() { return context_value{}; }

        /**
         * \brief A boolean.
         * \param value The flag.
         */
        [[nodiscard]] static constexpr context_value boolean(bool value) {
            context_value result;
            result.storage_ = value;
            return result;
        }

        /**
         * \brief A single string.
         * \param value The text; borrowed or owned, see clapp::cow_str.
         */
        [[nodiscard]] static constexpr context_value string(cow_str value) {
            context_value result;
            result.storage_ = std::move(value);
            return result;
        }

        /**
         * \brief A list of strings.
         * \param values The texts, in render order.
         */
        [[nodiscard]] static constexpr context_value strings(std::vector<cow_str> values) {
            context_value result;
            result.storage_ = std::move(values);
            return result;
        }

        /**
         * \brief An already-styled message (e.g. a usage line).
         * \param value The message.
         */
        [[nodiscard]] static constexpr context_value styled(styled_str value) {
            context_value result;
            result.storage_ = std::move(value);
            return result;
        }

        /**
         * \brief A list of already-styled messages (e.g. `tip:` lines).
         * \param values The messages, in render order.
         */
        [[nodiscard]] static constexpr context_value styled_list(std::vector<styled_str> values) {
            context_value result;
            result.storage_ = std::move(values);
            return result;
        }

        /**
         * \brief A number (e.g. a value count).
         * \param value Signed, matching clap's `isize`.
         */
        [[nodiscard]] static constexpr context_value number(std::ptrdiff_t value) {
            context_value result;
            result.storage_ = value;
            return result;
        }

        /** \brief Which alternative is held. */
        [[nodiscard]] constexpr context_value_kind kind() const noexcept {
            return static_cast<context_value_kind>(storage_.index());
        }

        /** \brief The boolean, when kind() is context_value_kind::boolean. */
        [[nodiscard]] constexpr std::optional<bool> as_bool() const {
            if (!std::holds_alternative<bool>(storage_)) return std::nullopt;
            return std::get<bool>(storage_);
        }

        /** \brief The number, when kind() is context_value_kind::number. */
        [[nodiscard]] constexpr std::optional<std::ptrdiff_t> as_number() const {
            if (!std::holds_alternative<std::ptrdiff_t>(storage_)) return std::nullopt;
            return std::get<std::ptrdiff_t>(storage_);
        }

        /**
         * \brief The single string, when kind() is context_value_kind::string.
         * \return A view borrowing `*this`; `nullopt` for other alternatives
         *         (including strings holding exactly one element).
         *
         * \warning Borrows `*this` and does not extend its lifetime, so
         *          `err.context(k)->as_string()` dangles — clapp::error::context()
         *          returns a *copy* that dies at the end of the full-expression. Bind
         *          the copy first, or call clapp::error::context_ref().
         */
        [[nodiscard]] constexpr std::optional<std::string_view> as_string() const {
            if (!std::holds_alternative<cow_str>(storage_)) return std::nullopt;
            return std::get<cow_str>(storage_).view();
        }

        /**
         * \brief The string list, when kind() is context_value_kind::strings.
         * \return A view borrowing `*this`; empty span for other alternatives
         *         (indistinguishable from empty strings — test kind() when that matters).
         *
         * \warning Borrows `*this` and does not extend its lifetime; see as_string()
         *          for the `err.context(k)->as_strings()` trap.
         */
        [[nodiscard]] constexpr std::span<const cow_str> as_strings() const {
            if (!std::holds_alternative<std::vector<cow_str>>(storage_)) return {};
            return std::get<std::vector<cow_str>>(storage_);
        }

        /**
         * \brief The styled message, when kind() is context_value_kind::styled.
         * \return A copy; `nullopt` for every other alternative.
         */
        [[nodiscard]] constexpr std::optional<styled_str> as_styled() const {
            if (!std::holds_alternative<styled_str>(storage_)) return std::nullopt;
            return std::get<styled_str>(storage_);
        }

        /**
         * \brief The styled list, when kind() is context_value_kind::styled_list.
         * \return A view borrowing `*this`; empty span otherwise.
         *
         * \warning Borrows `*this` and does not extend its lifetime; see as_string()
         *          for the `err.context(k)->as_styled_list()` trap.
         */
        [[nodiscard]] constexpr std::span<const styled_str> as_styled_list() const {
            if (!std::holds_alternative<std::vector<styled_str>>(storage_)) return {};
            return std::get<std::vector<styled_str>>(storage_);
        }

        /**
         * \brief The value as plain text. clap's `Display for ContextValue`.
         * \return Empty for none; `"true"`/`"false"`; decimal; string/styled text;
         *         `", "`-joined list elements. For logging/tests — not the full message
         *         (that is `error::render()`).
         */
        [[nodiscard]] constexpr std::string to_string() const {
            std::string out;
            switch (kind()) {
            case context_value_kind::none:
                break;
            case context_value_kind::boolean:
                detail::append_bytes(out, std::get<bool>(storage_) ? "true" : "false");
                break;
            case context_value_kind::string:
                detail::append_bytes(out, std::get<cow_str>(storage_).view());
                break;
            case context_value_kind::strings: {
                bool first = true;
                for (const cow_str& value : std::get<std::vector<cow_str>>(storage_)) {
                    if (!first) detail::append_bytes(out, ", ");
                    first = false;
                    detail::append_bytes(out, value.view());
                }
                break;
            }
            case context_value_kind::styled:
                detail::append_bytes(out, std::get<styled_str>(storage_).to_string());
                break;
            case context_value_kind::styled_list: {
                bool first = true;
                for (const styled_str& value : std::get<std::vector<styled_str>>(storage_)) {
                    if (!first) detail::append_bytes(out, ", ");
                    first = false;
                    detail::append_bytes(out, value.to_string());
                }
                break;
            }
            case context_value_kind::number:
                detail::append_decimal(out, std::get<std::ptrdiff_t>(storage_));
                break;
            }
            return out;
        }

        /** \brief Equality by alternative and content. */
        [[nodiscard]] constexpr bool operator==(const context_value&) const = default;

    private:
        // Alternative order *is* context_value_kind's order; monostate first = none.
        std::variant<std::monostate,
                     bool,
                     cow_str,
                     std::vector<cow_str>,
                     styled_str,
                     std::vector<styled_str>,
                     std::ptrdiff_t>
                storage_{};
    };

    namespace detail {

        /**
         * \brief Sentinel for an absent context lookup (for context_ref()).
         *
         * A `const context_value*` would invite `!= nullptr`, which constant evaluation
         * refuses under `-fsanitize=null` (trap 10).
         */
        inline constexpr context_value absent_context_value{};

        static_assert(all_context_kinds.size() == context_kind_count);

        /** \brief Verify that the context-kind table is exhaustive and duplicate-free. */
        consteval bool all_context_kinds_is_a_set() {
            for (std::size_t value = 0; value < context_kind_count; ++value) {
                std::size_t seen = 0;
                for (const context_kind kind : all_context_kinds) {
                    if (static_cast<std::size_t>(kind) == value) ++seen;
                }
                if (seen != 1) return false;
            }
            for (const context_kind kind : all_context_kinds) {
                if (name_of(kind).empty()) return false;
            }
            return true;
        }

        static_assert(all_context_kinds_is_a_set(),
                      "clapp: all_context_kinds must list every clapp::context_kind "
                      "exactly once, with contiguous underlying values.");

        /** \brief Verify that exactly the self-describing context kinds omit a label. */
        consteval bool describe_context_covers_every_kind() {
            for (const context_kind kind : all_context_kinds) {
                const bool has_label = describe(kind).has_value();
                const bool wants_label =
                        kind != context_kind::usage && kind != context_kind::custom;
                if (has_label != wants_label) return false;
            }
            return true;
        }

        static_assert(describe_context_covers_every_kind());

        static_assert(context_value{}.kind() == context_value_kind::none);
        static_assert(context_value::boolean(true).kind() == context_value_kind::boolean);
        static_assert(context_value::string(cow_str::borrowed("x")).kind() ==
                      context_value_kind::string);
        static_assert(context_value::strings({}).kind() == context_value_kind::strings);
        static_assert(context_value::styled(styled_str{"x"}).kind() == context_value_kind::styled);
        static_assert(context_value::styled_list({}).kind() == context_value_kind::styled_list);
        static_assert(context_value::number(1).kind() == context_value_kind::number);

        static_assert(cow_str{}.is_owned());
        static_assert(!cow_str::borrowed("--verbose").is_owned());
        static_assert(cow_str::owned("--verbose").is_owned());
        static_assert(cow_str::borrowed("--verbose") == cow_str::owned("--verbose"));
        static_assert(cow_str::borrowed("--verbose").view() == std::string_view{"--verbose"});
        static_assert(cow_str::owned("--verbose").size() == 9);

    }  // namespace detail

}  // namespace clapp
