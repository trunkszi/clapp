/**
 * \file
 * \brief clapp::arg_builder and frozen clapp::arg_spec (one command-line argument).
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/value_hint.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {
    // -----------------------------------------------------------------------------
    // Settings bitset
    // -----------------------------------------------------------------------------

    /**
     * \brief One boolean knob on an argument (clap ArgSettings).
     * \note Enumerator order is load-bearing (arg_flags bit positions); clap-with-env order.
     */
    enum class arg_setting : std::uint8_t {
        required, /**< The argument must appear. clap: `Required`. */
        global, /**< Propagates to every subcommand. clap: `Global`. */
        hidden, /**< Omitted from help. clap: `Hidden`. */
        next_line_help, /**< Help text starts on its own line. clap: `NextLineHelp`. */
        hide_possible_values, /**< Suppress `[possible values: ...]`. */
        allow_hyphen_values, /**< A value may start with `-`. */
        allow_negative_numbers, /**< A value may be a negative number, but not a flag. */
        require_equals, /**< `--opt=value` only; `--opt value` is an error. */
        last, /**< Positional reachable only after `--`. clap: `Last`. */
        trailing_var_arg, /**< Everything from here on is a value of this argument. */
        hide_default_value, /**< Suppress `[default: ...]`. */
        ignore_case, /**< Match possible values case-insensitively. */
        hide_env, /**< Suppress `[env: NAME]`. */
        hide_env_values, /**< Suppress the environment variable's current value. */
        hidden_short_help, /**< Present in `--help`, absent from `-h`. */
        hidden_long_help, /**< Present in `-h`, absent from `--help`. */
        exclusive, /**< May not be combined with any other argument. */
    };

    /** \brief How many distinct clapp::arg_setting values exist. */
    inline constexpr std::size_t arg_setting_count = 17;

    /** \brief Every clapp::arg_setting, in bit order. Handy for exhaustive tests. */
    inline constexpr std::array<arg_setting, arg_setting_count> all_arg_settings{
        arg_setting::required,
        arg_setting::global,
        arg_setting::hidden,
        arg_setting::next_line_help,
        arg_setting::hide_possible_values,
        arg_setting::allow_hyphen_values,
        arg_setting::allow_negative_numbers,
        arg_setting::require_equals,
        arg_setting::last,
        arg_setting::trailing_var_arg,
        arg_setting::hide_default_value,
        arg_setting::ignore_case,
        arg_setting::hide_env,
        arg_setting::hide_env_values,
        arg_setting::hidden_short_help,
        arg_setting::hidden_long_help,
        arg_setting::exclusive,
    };

    /**
     * \brief The spelling of \p setting, for diagnostics.
     * \param setting The knob to name.
     * \return A view into static storage; equals the enumerator's own spelling.
     */
    [[nodiscard]] constexpr std::string_view name_of(arg_setting setting) noexcept {
        switch (setting) {
            case arg_setting::required:
                return "required";
            case arg_setting::global:
                return "global";
            case arg_setting::hidden:
                return "hidden";
            case arg_setting::next_line_help:
                return "next_line_help";
            case arg_setting::hide_possible_values:
                return "hide_possible_values";
            case arg_setting::allow_hyphen_values:
                return "allow_hyphen_values";
            case arg_setting::allow_negative_numbers:
                return "allow_negative_numbers";
            case arg_setting::require_equals:
                return "require_equals";
            case arg_setting::last:
                return "last";
            case arg_setting::trailing_var_arg:
                return "trailing_var_arg";
            case arg_setting::hide_default_value:
                return "hide_default_value";
            case arg_setting::ignore_case:
                return "ignore_case";
            case arg_setting::hide_env:
                return "hide_env";
            case arg_setting::hide_env_values:
                return "hide_env_values";
            case arg_setting::hidden_short_help:
                return "hidden_short_help";
            case arg_setting::hidden_long_help:
                return "hidden_long_help";
            case arg_setting::exclusive:
                return "exclusive";
        }
        return "unknown";
    }

    /**
     * \brief Seventeen arg_setting knobs in one word (clap ArgFlags; structural).
     * \note Bit n is arg_setting(n). Prefer members over raw #bits.
     */
    struct arg_flags {
        /** \brief Storage. Bit `n` corresponds to `static_cast<arg_setting>(n)`. */
        std::uint32_t bits = 0;

        /** \brief The single-bit mask for \p setting. */
        [[nodiscard]] static constexpr std::uint32_t bit_of(arg_setting setting) noexcept {
            return std::uint32_t{1} << static_cast<unsigned>(setting);
        }

        /** \brief Whether \p setting is on. */
        [[nodiscard]] constexpr bool is_set(arg_setting setting) const noexcept {
            return (bits & bit_of(setting)) != 0;
        }

        /** \brief Turn \p setting on, in place. */
        constexpr arg_flags &set(arg_setting setting) noexcept {
            bits |= bit_of(setting);
            return *this;
        }

        /** \brief Turn \p setting off, in place. */
        constexpr arg_flags &unset(arg_setting setting) noexcept {
            bits &= ~bit_of(setting);
            return *this;
        }

        /** \brief Turn \p setting on or off according to \p enable, in place. */
        constexpr arg_flags &set(arg_setting setting, bool enable) noexcept {
            return enable ? set(setting) : unset(setting);
        }

        /** \brief Union \p other into `*this`, in place. */
        constexpr arg_flags &insert(arg_flags other) noexcept {
            bits |= other.bits;
            return *this;
        }

        /** \brief A copy with \p setting forced to \p enable. */
        [[nodiscard]] constexpr arg_flags with(arg_setting setting, bool enable) const noexcept {
            arg_flags copy = *this;
            copy.set(setting, enable);
            return copy;
        }

        /** \brief How many knobs are on. */
        [[nodiscard]] constexpr std::size_t count() const noexcept {
            return static_cast<std::size_t>(std::popcount(bits));
        }

        /** \brief Whether every knob is off. */
        [[nodiscard]] constexpr bool empty() const noexcept { return bits == 0; }

        /** \brief Bitwise union. */
        [[nodiscard]] friend constexpr arg_flags operator|(arg_flags lhs, arg_flags rhs) noexcept {
            return arg_flags{.bits = lhs.bits | rhs.bits};
        }

        /** \brief Word equality. */
        [[nodiscard]] constexpr bool operator==(const arg_flags &) const noexcept = default;
    };

    // -----------------------------------------------------------------------------
    // Predicates
    // -----------------------------------------------------------------------------

    /**
     * \brief Which question a predicate about another argument asks.
     *        Mirrors clap's `ArgPredicate`.
     */
    enum class predicate_kind : std::uint8_t {
        is_present, /**< "Did that argument appear at all?" */
        equals, /**< "Did that argument take this exact value?" */
    };

    /**
     * \brief Frozen condition on another argument (requires_if / default_value_if).
     * \note #value is static storage; meaningful only for predicate_kind::equals.
     */
    struct arg_predicate {
        /** \brief Which question is asked. */
        predicate_kind kind = predicate_kind::is_present;
        /** \brief The value compared against, when #kind is predicate_kind::equals. */
        arg_id value{};

        /** \brief The "argument appeared" predicate. */
        [[nodiscard]] static constexpr arg_predicate present() noexcept { return {}; }

        /**
         * \brief The "argument equals \p text" predicate, with \p text promoted.
         * \param text The value to compare against; copied into static storage.
         */
        [[nodiscard]] static consteval arg_predicate equal_to(std::string_view text) {
            return {.kind = predicate_kind::equals, .value = make_static_id(text)};
        }

        /** \brief Whether this predicate only asks about presence. */
        [[nodiscard]] constexpr bool is_present_only() const noexcept {
            return kind == predicate_kind::is_present;
        }

        /**
         * \brief Test \p candidate against this predicate (byte-exact for equals).
         * \param candidate One value the other argument received.
         * \return true for is_present; byte-exact match for equals.
         */
        [[nodiscard]] constexpr bool matches(os_str candidate) const noexcept {
            return kind == predicate_kind::is_present || candidate == os_str{value.name()};
        }

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_predicate &other) const noexcept {
            return kind == other.kind &&
                   (kind == predicate_kind::is_present || value.name() == other.value.name());
        }
    };

    /**
     * \brief Builder-side condition (owns its value; counterpart of arg_predicate).
     */
    struct arg_condition {
        /** \brief Which question is asked. */
        predicate_kind kind = predicate_kind::is_present;
        /** \brief The value compared against, when #kind is predicate_kind::equals. */
        std::string value{};

        /** \brief The "argument appeared" condition. */
        [[nodiscard]] static constexpr arg_condition present() { return {}; }

        /** \brief The "argument equals \p text" condition. */
        [[nodiscard]] static constexpr arg_condition equal_to(std::string_view text) {
            return {.kind = predicate_kind::equals, .value = std::string{text}};
        }

        /** \brief Whether this condition only asks about presence. */
        [[nodiscard]] constexpr bool is_present_only() const noexcept {
            return kind == predicate_kind::is_present;
        }

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_condition &) const = default;
    };

    // -----------------------------------------------------------------------------
    // Frozen leaf records
    // -----------------------------------------------------------------------------

    /**
     * \brief One alternative long spelling, and whether help shows it.
     * \note clap stores `(Str, bool)`; naming the members is the only change.
     */
    struct alias_spec {
        /** \brief The alternative spelling, without the leading `--`. */
        arg_id name{};
        /** \brief `true` when help should list it; `false` for a compatibility alias. */
        bool visible = false;

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const alias_spec &other) const noexcept {
            return name.name() == other.name.name() && visible == other.visible;
        }
    };

    /** \brief One alternative short spelling, and whether help shows it. */
    struct short_alias_spec {
        /** \brief The alternative letter, without the leading `-`. */
        char name = '\0';
        /** \brief `true` when help should list it. */
        bool visible = false;

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const short_alias_spec &) const noexcept = default;
    };

    /**
     * \brief "When this argument satisfies #when, `--target` becomes required."
     * \note clap stores `(ArgPredicate, Id)` in `Arg::requires`.
     */
    struct requires_spec {
        /** \brief The condition on the *owning* argument. */
        arg_predicate when{};
        /** \brief The argument that becomes required. */
        arg_id target{};

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const requires_spec &other) const noexcept {
            return when == other.when && target.name() == other.target.name();
        }
    };

    /**
     * \brief "When `#id` equals #value, the owning argument becomes required."
     * \note clap stores `(Id, OsStr)` in `Arg::r_ifs` / `r_ifs_all`.
     */
    struct required_if_spec {
        /** \brief The argument being watched. */
        arg_id id{};
        /** \brief The value that triggers the requirement. */
        arg_id value{};

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const required_if_spec &other) const noexcept {
            return id.name() == other.id.name() && value.name() == other.value.name();
        }
    };

    /**
     * \brief Conditional default: when #id satisfies #when, use these values.
     * \note #values_present distinguishes zero values from clap None (remove default).
     *       Explicit bool, not pointer null — ubsan consteval cannot fold null tests.
     */
    struct default_value_spec {
        /** \brief The argument being watched. */
        arg_id id{};
        /** \brief The condition on that argument. */
        arg_predicate when{};
        /** \brief The replacement values. Meaningful only when #values_present. */
        const arg_id *value_data = nullptr;
        /** \brief How many replacement values there are. */
        std::size_t value_count = 0;
        /**
         * \brief Whether this rule supplies values at all, rather than removing the
         *        default. clap's `Some(_)` versus `None`.
         */
        bool values_present = false;

        /** \brief Whether this rule supplies values rather than removing the default. */
        [[nodiscard]] constexpr bool has_values() const noexcept { return values_present; }

        /** \brief The replacement values. */
        [[nodiscard]] constexpr std::span<const arg_id> values() const noexcept {
            return {value_data, value_count};
        }

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const default_value_spec &other) const noexcept {
            return id.name() == other.id.name() && when == other.when &&
                   has_values() == other.has_values() &&
                   std::ranges::equal(values(), other.values());
        }
    };

    // -----------------------------------------------------------------------------
    // Builder-side leaf records
    // -----------------------------------------------------------------------------

    /** \brief Builder-side counterpart of clapp::alias_spec; owns its name. */
    struct arg_alias {
        std::string name{}; /**< The alternative long spelling. */
        bool visible = false; /**< Whether help lists it. */

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_alias &) const = default;
    };

    /** \brief Builder-side counterpart of clapp::short_alias_spec. */
    struct arg_short_alias {
        char name = '\0'; /**< The alternative letter. */
        bool visible = false; /**< Whether help lists it. */

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_short_alias &) const = default;
    };

    /** \brief Builder-side counterpart of clapp::requires_spec; owns its strings. */
    struct arg_requirement {
        arg_condition when{}; /**< Condition on the owning argument. */
        std::string target{}; /**< The argument that becomes required. */

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_requirement &) const = default;
    };

    /** \brief Builder-side counterpart of clapp::required_if_spec; owns its strings. */
    struct arg_value_requirement {
        std::string id{}; /**< The argument being watched. */
        std::string value{}; /**< The value that triggers the requirement. */

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_value_requirement &) const = default;
    };

    /** \brief Builder-side counterpart of clapp::default_value_spec; owns its strings. */
    struct arg_default_condition {
        std::string id{}; /**< The argument being watched. */
        arg_condition when{}; /**< The condition on that argument. */
        bool has_values = false; /**< `false` removes the default. */
        std::vector<std::string> values{}; /**< The replacement values. */

        /** \brief Equality by content. */
        [[nodiscard]] constexpr bool operator==(const arg_default_condition &) const = default;
    };

    /**
     * \brief One `default_value_ifs` rule as written at a call site.
     *
     * A plain aggregate so the plural setters can take a braced list:
     * \code
     *     .default_value_ifs({{"mode", arg_condition::equal_to("fast"), "8"},
     *                         {"mode", arg_condition::equal_to("slow"), "1"}})
     * \endcode
     */
    struct default_value_rule {
        std::string_view id; /**< The argument being watched. */
        arg_condition when; /**< The condition on that argument. */
        std::optional<std::string_view> value; /**< `std::nullopt` removes the default. */
    };

    namespace detail {
        /** \brief Input range whose elements convert to `char`. */
        template<class R>
        concept char_range = std::ranges::input_range<R> &&
                             std::convertible_to<std::ranges::range_reference_t<R>, char>;

        /** \brief Input range whose elements convert to clapp::default_value_rule. */
        template<class R>
        concept default_value_rule_range =
                std::ranges::input_range<R> &&
                std::convertible_to<std::ranges::range_reference_t<R>, default_value_rule>;

        /** \brief Input range of `(id, value)` string pairs. */
        template<class R>
        concept string_pair_range =
                std::ranges::input_range<R> &&
                std::convertible_to<std::ranges::range_reference_t<R>,
                    std::pair<std::string_view, std::string_view> >;

        /**
         * \brief Promote owning strings into a static arg_id array.
         * \param names May be transient (copied).
         * \return Span into static storage, or empty.
         */
        [[nodiscard]] consteval std::span<const arg_id>
        promote_ids(std::span<const std::string> names) {
            if (names.empty()) return {};
            std::vector<arg_id> ids;
            ids.reserve(names.size());
            for (const std::string &name: names) ids.push_back(make_static_id(name));
            return std::define_static_array(ids);
        }

        /** \brief Promote a builder-side condition into its frozen counterpart. */
        [[nodiscard]] consteval arg_predicate promote_condition(const arg_condition &when) {
            if (when.kind == predicate_kind::is_present) return arg_predicate::present();
            return arg_predicate::equal_to(when.value);
        }

        /** \brief Promote long aliases. See promote_ids() for why the loop is raw. */
        [[nodiscard]] consteval std::span<const alias_spec>
        promote_aliases(std::span<const arg_alias> aliases) {
            if (aliases.empty()) return {};
            std::vector<alias_spec> frozen;
            frozen.reserve(aliases.size());
            for (const arg_alias &entry: aliases) {
                frozen.push_back(
                    alias_spec{.name = make_static_id(entry.name), .visible = entry.visible});
            }
            return std::define_static_array(frozen);
        }

        /** \brief Promote short aliases. No string promotion needed, only a copy. */
        [[nodiscard]] consteval std::span<const short_alias_spec>
        promote_short_aliases(std::span<const arg_short_alias> aliases) {
            if (aliases.empty()) return {};
            std::vector<short_alias_spec> frozen;
            frozen.reserve(aliases.size());
            for (const arg_short_alias &entry: aliases) {
                frozen.push_back(short_alias_spec{.name = entry.name, .visible = entry.visible});
            }
            return std::define_static_array(frozen);
        }

        /** \brief Promote `requires` / `requires_if` rules. */
        [[nodiscard]] consteval std::span<const requires_spec>
        promote_requirements(std::span<const arg_requirement> rules) {
            if (rules.empty()) return {};
            std::vector<requires_spec> frozen;
            frozen.reserve(rules.size());
            for (const arg_requirement &rule: rules) {
                frozen.push_back(requires_spec{
                    .when = promote_condition(rule.when),
                    .target = make_static_id(rule.target)
                });
            }
            return std::define_static_array(frozen);
        }

        /** \brief Promote `required_if_eq` rules. */
        [[nodiscard]] consteval std::span<const required_if_spec>
        promote_value_requirements(std::span<const arg_value_requirement> rules) {
            if (rules.empty()) return {};
            std::vector<required_if_spec> frozen;
            frozen.reserve(rules.size());
            for (const arg_value_requirement &rule: rules) {
                frozen.push_back(required_if_spec{
                    .id = make_static_id(rule.id),
                    .value = make_static_id(rule.value)
                });
            }
            return std::define_static_array(frozen);
        }

        /** \brief Promote `default_value_if` rules, preserving the null / empty split. */
        [[nodiscard]] consteval std::span<const default_value_spec>
        promote_default_conditions(std::span<const arg_default_condition> rules) {
            if (rules.empty()) return {};
            std::vector<default_value_spec> frozen;
            frozen.reserve(rules.size());
            for (const arg_default_condition &rule: rules) {
                const std::span<const arg_id> values =
                        rule.has_values ? promote_ids(rule.values) : std::span<const arg_id>{};
                // A rule that supplies zero values stays distinguishable from one that
                // removes the default through `values_present`, so no dummy array has to
                // be conjured up to keep `value_data` non-null.
                frozen.push_back(default_value_spec{
                    .id = make_static_id(rule.id),
                    .when = promote_condition(rule.when),
                    .value_data = values.data(),
                    .value_count = values.size(),
                    .values_present = rule.has_values,
                });
            }
            return std::define_static_array(frozen);
        }

        /** \brief Element-wise equality of two pointer/count pairs. */
        template<class T>
        [[nodiscard]] constexpr bool spans_equal(std::span<const T> lhs,
                                                 std::span<const T> rhs) noexcept {
            return std::ranges::equal(lhs, rhs);
        }
    } // namespace detail

    // -----------------------------------------------------------------------------
    // arg_spec — the frozen argument
    // -----------------------------------------------------------------------------

    /**
     * \brief One frozen argument for .rodata (from arg_builder::freeze).
     *
     * #act and #num_args are always resolved (never infer). Structural: pointer+count
     * and length/bool sentinels, not span/optional.
     *
     * \note No accessor null-compares a pointer (ubsan consteval); use length sentinels
     *       or #help_heading_present / non-null #parser.
     */
    struct arg_spec {
        /**
         * \name Identity
         * \{
         */
        arg_id id{}; /**< The argument's unique name. */
        char short_ = '\0'; /**< `'\0'`: no short option. */
        /**
         * \brief Whether a section heading was set. \see get_help_heading()
         * \note Lives in the id/short_ alignment hole (keeps arg_spec 408 bytes).
         *       Bool not length: empty heading is present (opts out of next_help_heading);
         *       absent vs present-but-empty differ only here.
         */
        bool help_heading_present = false;
        arg_id long_{}; /**< Empty: no long option. */
        const alias_spec *alias_data = nullptr; /**< Long aliases. */
        std::size_t alias_count = 0; /**< Number of long aliases. */
        const short_alias_spec *short_alias_data = nullptr; /**< Short aliases. */
        std::size_t short_alias_count = 0; /**< Number of short aliases. */
        std::size_t index = 0; /**< 1-based positional slot; 0 = unset. */
        /** \} */

        /**
         * \name Behaviour
         * \{
         */
        arg_action act = arg_action::set; /**< Never `infer`. */
        value_range num_args = value_range::single(); /**< Never `infer()`. */
        /**
         * \brief Value-parser dispatch table; never null (defaults to string parser).
         */
        const parser_vtable *parser = parser_for<std::string>();
        value_hint hint = value_hint::unknown; /**< Completion hint. */
        char delimiter = '\0'; /**< `'\0'`: values are not split. */
        arg_id terminator{}; /**< Empty: no terminator. */
        /** \} */

        /**
         * \name Constraints
         * \{
         */
        const arg_id *conflict_data = nullptr; /**< Arguments that may not co-occur. */
        std::size_t conflict_count = 0; /**< Number of conflicts. */
        const arg_id *override_data = nullptr; /**< Arguments this one silently replaces. */
        std::size_t override_count = 0; /**< Number of overrides. */
        const arg_id *group_data = nullptr; /**< Groups this argument belongs to. */
        std::size_t group_count = 0; /**< Number of groups. */
        const requires_spec *requires_data = nullptr; /**< Conditional requirements. */
        std::size_t requires_count = 0; /**< Number of requirements. */
        const arg_id *required_unless_any_data = nullptr; /**< Any one of these excuses it. */
        std::size_t required_unless_any_count = 0; /**< Number of such arguments. */
        const arg_id *required_unless_all_data = nullptr; /**< All of these excuse it. */
        std::size_t required_unless_all_count = 0; /**< Number of such arguments. */
        const required_if_spec *required_if_any_data = nullptr; /**< Any match requires it. */
        std::size_t required_if_any_count = 0; /**< Number of such rules. */
        const required_if_spec *required_if_all_data = nullptr; /**< All matches require it. */
        std::size_t required_if_all_count = 0; /**< Number of such rules. */
        /** \} */

        /**
         * \name Value sources
         * \{
         */
        const arg_id *default_data = nullptr; /**< Values used when absent. */
        std::size_t default_count = 0; /**< Number of default values. */
        const arg_id *default_missing_data = nullptr; /**< Values used when present but empty. */
        std::size_t default_missing_count = 0; /**< Number of such values. */
        const default_value_spec *default_if_data = nullptr; /**< Conditional defaults. */
        std::size_t default_if_count = 0; /**< Number of such rules. */
        arg_id env{}; /**< Environment variable; empty = none. */
        /** \} */

        /**
         * \name Help presentation
         * \{
         */
        const char *help_text = nullptr; /**< Short help; read only via #help_length. */
        std::size_t help_length = 0; /**< Bytes of #help_text; 0 = no help. */
        const char *long_help_text = nullptr; /**< Long help; read via #long_help_length. */
        std::size_t long_help_length = 0; /**< Bytes of #long_help_text; 0 = none. */
        const char *help_heading_text = nullptr; /**< Section; read via #help_heading_present. */
        std::size_t help_heading_length = 0; /**< Bytes of #help_heading_text. */
        // The presence flag for the two fields above is #help_heading_present, declared
        // up in "Identity" so it can sit in that group's alignment hole. Its semantics —
        // absent versus present-but-empty — are documented there.
        const arg_id *value_name_data = nullptr; /**< Placeholders such as `<FILE>`. */
        std::size_t value_name_count = 0; /**< Number of placeholders. */
        std::size_t display_order = 999; /**< Sort key within a section. */
        /** \} */

        /** \brief The seventeen boolean knobs. */
        arg_flags settings{};

        // -- identity -------------------------------------------------------------

        /** \brief The argument's unique name. */
        [[nodiscard]] constexpr arg_id get_id() const noexcept { return id; }

        /** \brief The short option letter, if any. */
        [[nodiscard]] constexpr std::optional<char> get_short() const noexcept {
            return short_ == '\0' ? std::nullopt : std::optional<char>{short_};
        }

        /** \brief The long option spelling, without `--`, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long() const noexcept {
            if (long_.empty()) return std::nullopt;
            return long_.name();
        }

        /** \brief Every long alias, visible and hidden alike. */
        [[nodiscard]] constexpr std::span<const alias_spec> get_all_aliases() const noexcept {
            return {alias_data, alias_count};
        }

        /** \brief The long aliases help should list, as a lazy range of spellings. */
        [[nodiscard]] constexpr auto get_visible_aliases() const noexcept {
            return get_all_aliases() |
                   std::views::filter([](const alias_spec &a) { return a.visible; }) |
                   std::views::transform([](const alias_spec &a) { return a.name.name(); });
        }

        /** \brief The long aliases help should hide, as a lazy range of spellings. */
        [[nodiscard]] constexpr auto get_aliases() const noexcept {
            return get_all_aliases() |
                   std::views::filter([](const alias_spec &a) { return !a.visible; }) |
                   std::views::transform([](const alias_spec &a) { return a.name.name(); });
        }

        /**
         * \brief The long option plus its visible aliases, for the help renderer.
         * \return An empty vector when there is no long option at all, matching clap's
         *         `Option<Vec<_>>` shape collapsed onto emptiness.
         */
        [[nodiscard]] constexpr std::vector<std::string_view> get_long_and_visible_aliases() const {
            std::vector<std::string_view> names;
            if (long_.empty()) return names;
            names.push_back(long_.name());
            for (std::string_view alias: get_visible_aliases()) names.push_back(alias);
            return names;
        }

        /** \brief Every short alias, visible and hidden alike. */
        [[nodiscard]] constexpr std::span<const short_alias_spec>
        get_all_short_aliases() const noexcept {
            return {short_alias_data, short_alias_count};
        }

        /** \brief The short aliases help should list, as a lazy range of letters. */
        [[nodiscard]] constexpr auto get_visible_short_aliases() const noexcept {
            return get_all_short_aliases() |
                   std::views::filter([](const short_alias_spec &a) { return a.visible; }) |
                   std::views::transform([](const short_alias_spec &a) { return a.name; });
        }

        /** \brief The short option plus its visible aliases, for the help renderer. */
        [[nodiscard]] constexpr std::vector<char> get_short_and_visible_aliases() const {
            std::vector<char> letters;
            if (short_ == '\0') return letters;
            letters.push_back(short_);
            for (char alias: get_visible_short_aliases()) letters.push_back(alias);
            return letters;
        }

        /** \brief The 1-based positional slot, if one was assigned explicitly. */
        [[nodiscard]] constexpr std::optional<std::size_t> get_index() const noexcept {
            return index == 0 ? std::nullopt : std::optional<std::size_t>{index};
        }

        /**
         * \brief Whether this argument is matched by position rather than by name.
         * \note Exactly clap's rule: no short option and no long option.
         */
        [[nodiscard]] constexpr bool is_positional() const noexcept {
            return short_ == '\0' && long_.empty();
        }

        /**
         * \brief Whether \p name is this argument's long option or one of its aliases.
         * \param name The spelling seen on the command line, without `--`.
         * \param include_hidden Whether hidden aliases count. They normally should:
         *        a hidden alias is hidden from *help*, not from the parser.
         */
        [[nodiscard]] constexpr bool matches_long(std::string_view name,
                                                  bool include_hidden = true) const noexcept {
            if (!long_.empty() && long_.name() == name) return true;
            return std::ranges::any_of(get_all_aliases(), [&](const alias_spec &alias) {
                return (include_hidden || alias.visible) && alias.name.name() == name;
            });
        }

        /** \brief Whether \p letter is this argument's short option or one of its aliases. */
        [[nodiscard]] constexpr bool matches_short(char letter,
                                                   bool include_hidden = true) const noexcept {
            if (letter == '\0') return false;
            if (short_ == letter) return true;
            return std::ranges::any_of(get_all_short_aliases(), [&](const short_alias_spec &alias) {
                return (include_hidden || alias.visible) && alias.name == letter;
            });
        }

        // -- behaviour ------------------------------------------------------------

        /** \brief What happens when this argument matches. Never arg_action::infer. */
        [[nodiscard]] constexpr arg_action get_action() const noexcept { return act; }

        /** \brief How many values it takes. Never value_range::infer(). */
        [[nodiscard]] constexpr value_range get_num_args() const noexcept { return num_args; }

        /** \brief Smallest legal value count. */
        [[nodiscard]] constexpr std::size_t get_min_vals() const noexcept {
            return num_args.min_values();
        }

        /** \brief Largest legal value count, possibly value_range::unbounded. */
        [[nodiscard]] constexpr std::size_t get_max_vals() const noexcept {
            return num_args.max_values();
        }

        /** \brief Whether this argument consumes values at all. */
        [[nodiscard]] constexpr bool is_takes_value_set() const noexcept {
            return num_args.takes_values();
        }

        /** \brief Whether it can consume more than one value. */
        [[nodiscard]] constexpr bool is_multiple_values_set() const noexcept {
            return num_args.is_multiple();
        }

        /**
         * \brief Whether the argument may hold more than one value (clap is_multiple).
         * \warning Not the same as is_multiple_values_set(): this includes append across
         *          occurrences; that is per-occurrence arity. freeze() positional rules
         *          need this; value-name / unbounded-positional rules need the other.
         */
        [[nodiscard]] constexpr bool is_multiple() const noexcept {
            return num_args.is_multiple() || act == arg_action::append;
        }

        /** \brief The dispatch table used to turn text into a typed value. */
        [[nodiscard]] constexpr const parser_vtable *get_value_parser() const noexcept {
            return parser;
        }

        /**
         * \brief The accepted values, or an empty span when the type does not enumerate.
         * \note Empty for an argument that takes no values, matching clap, so the help
         *       renderer never prints `[possible values: ...]` for a flag.
         */
        [[nodiscard]] constexpr std::span<const possible_value>
        get_possible_values() const noexcept {
            if (!is_takes_value_set()) return {};
            return parser->possible_values();
        }

        /** \brief The shell-completion hint. */
        [[nodiscard]] constexpr value_hint get_value_hint() const noexcept { return hint; }

        /** \brief The character values are split on, if any. */
        [[nodiscard]] constexpr std::optional<char> get_value_delimiter() const noexcept {
            return delimiter == '\0' ? std::nullopt : std::optional<char>{delimiter};
        }

        /** \brief The sentinel token that stops multi-value collection, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_value_terminator() const noexcept {
            if (terminator.empty()) return std::nullopt;
            return terminator.name();
        }

        // -- constraints ----------------------------------------------------------

        /** \brief Arguments that may not appear alongside this one. */
        [[nodiscard]] constexpr std::span<const arg_id> get_conflicts() const noexcept {
            return {conflict_data, conflict_count};
        }

        /** \brief Arguments this one silently replaces when both appear. */
        [[nodiscard]] constexpr std::span<const arg_id> get_overrides() const noexcept {
            return {override_data, override_count};
        }

        /** \brief The clapp::arg_group ids this argument belongs to. */
        [[nodiscard]] constexpr std::span<const arg_id> get_groups() const noexcept {
            return {group_data, group_count};
        }

        /** \brief Conditional requirements this argument imposes on others. */
        [[nodiscard]] constexpr std::span<const requires_spec> get_requires() const noexcept {
            return {requires_data, requires_count};
        }

        /** \brief Arguments whose presence excuses this one (any one suffices). */
        [[nodiscard]] constexpr std::span<const arg_id>
        get_required_unless_present_any() const noexcept {
            return {required_unless_any_data, required_unless_any_count};
        }

        /** \brief Arguments whose presence excuses this one (all are needed). */
        [[nodiscard]] constexpr std::span<const arg_id>
        get_required_unless_present_all() const noexcept {
            return {required_unless_all_data, required_unless_all_count};
        }

        /** \brief Rules where a single match makes this argument required. */
        [[nodiscard]] constexpr std::span<const required_if_spec>
        get_required_if_eq_any() const noexcept {
            return {required_if_any_data, required_if_any_count};
        }

        /** \brief Rules where every rule must match to make this argument required. */
        [[nodiscard]] constexpr std::span<const required_if_spec>
        get_required_if_eq_all() const noexcept {
            return {required_if_all_data, required_if_all_count};
        }

        // -- value sources --------------------------------------------------------

        /** \brief The values used when the argument never appears. */
        [[nodiscard]] constexpr std::span<const arg_id> get_default_values() const noexcept {
            return {default_data, default_count};
        }

        /** \brief The values used when the argument appears with none of its own. */
        [[nodiscard]] constexpr std::span<const arg_id>
        get_default_missing_values() const noexcept {
            return {default_missing_data, default_missing_count};
        }

        /** \brief The conditional-default rules, in declaration order. */
        [[nodiscard]] constexpr std::span<const default_value_spec>
        get_default_values_ifs() const noexcept {
            return {default_if_data, default_if_count};
        }

        /** \brief The environment variable consulted before the defaults, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_env() const noexcept {
            if (env.empty()) return std::nullopt;
            return env.name();
        }

        // -- help -----------------------------------------------------------------

        /** \brief The one-line help text, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help() const noexcept {
            if (help_length == 0) return std::nullopt;
            return std::string_view{help_text, help_length};
        }

        /** \brief The `--help` (long form) text, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_help() const noexcept {
            if (long_help_length == 0) return std::nullopt;
            return std::string_view{long_help_text, long_help_length};
        }

        /** \brief The help section heading, if the argument overrides the command's. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help_heading() const noexcept {
            if (!help_heading_present) return std::nullopt;
            return std::string_view{help_heading_text, help_heading_length};
        }

        /** \brief The sort key within a help section. Defaults to 999, as in clap. */
        [[nodiscard]] constexpr std::size_t get_display_order() const noexcept {
            return display_order;
        }

        /** \brief The value placeholders, such as `FILE` in `--out <FILE>`. */
        [[nodiscard]] constexpr std::span<const arg_id> get_value_names() const noexcept {
            return {value_name_data, value_name_count};
        }

        // -- settings -------------------------------------------------------------

        /** \brief Whether \p setting is on. */
        [[nodiscard]] constexpr bool is_set(arg_setting setting) const noexcept {
            return settings.is_set(setting);
        }

        /** \brief The whole flag word, for callers that want to test several knobs. */
        [[nodiscard]] constexpr arg_flags get_settings() const noexcept { return settings; }

        /** \brief Reports clapp::arg_builder::required(). */
        [[nodiscard]] constexpr bool is_required_set() const noexcept {
            return is_set(arg_setting::required);
        }

        /** \brief Reports clapp::arg_builder::global(). */
        [[nodiscard]] constexpr bool is_global_set() const noexcept {
            return is_set(arg_setting::global);
        }

        /** \brief Reports clapp::arg_builder::hide(). */
        [[nodiscard]] constexpr bool is_hide_set() const noexcept {
            return is_set(arg_setting::hidden);
        }

        /** \brief Reports clapp::arg_builder::next_line_help(). */
        [[nodiscard]] constexpr bool is_next_line_help_set() const noexcept {
            return is_set(arg_setting::next_line_help);
        }

        /** \brief Reports clapp::arg_builder::hide_possible_values(). */
        [[nodiscard]] constexpr bool is_hide_possible_values_set() const noexcept {
            return is_set(arg_setting::hide_possible_values);
        }

        /** \brief Reports clapp::arg_builder::allow_hyphen_values(). */
        [[nodiscard]] constexpr bool is_allow_hyphen_values_set() const noexcept {
            return is_set(arg_setting::allow_hyphen_values);
        }

        /** \brief Reports clapp::arg_builder::allow_negative_numbers(). */
        [[nodiscard]] constexpr bool is_allow_negative_numbers_set() const noexcept {
            return is_set(arg_setting::allow_negative_numbers);
        }

        /** \brief Reports clapp::arg_builder::require_equals(). */
        [[nodiscard]] constexpr bool is_require_equals_set() const noexcept {
            return is_set(arg_setting::require_equals);
        }

        /** \brief Reports clapp::arg_builder::last(). */
        [[nodiscard]] constexpr bool is_last_set() const noexcept {
            return is_set(arg_setting::last);
        }

        /** \brief Reports clapp::arg_builder::trailing_var_arg(). */
        [[nodiscard]] constexpr bool is_trailing_var_arg_set() const noexcept {
            return is_set(arg_setting::trailing_var_arg);
        }

        /** \brief Reports clapp::arg_builder::hide_default_value(). */
        [[nodiscard]] constexpr bool is_hide_default_value_set() const noexcept {
            return is_set(arg_setting::hide_default_value);
        }

        /** \brief Reports clapp::arg_builder::ignore_case(). */
        [[nodiscard]] constexpr bool is_ignore_case_set() const noexcept {
            return is_set(arg_setting::ignore_case);
        }

        /** \brief Reports clapp::arg_builder::hide_env(). */
        [[nodiscard]] constexpr bool is_hide_env_set() const noexcept {
            return is_set(arg_setting::hide_env);
        }

        /** \brief Reports clapp::arg_builder::hide_env_values(). */
        [[nodiscard]] constexpr bool is_hide_env_values_set() const noexcept {
            return is_set(arg_setting::hide_env_values);
        }

        /** \brief Reports clapp::arg_builder::hide_short_help(). */
        [[nodiscard]] constexpr bool is_hide_short_help_set() const noexcept {
            return is_set(arg_setting::hidden_short_help);
        }

        /** \brief Reports clapp::arg_builder::hide_long_help(). */
        [[nodiscard]] constexpr bool is_hide_long_help_set() const noexcept {
            return is_set(arg_setting::hidden_long_help);
        }

        /** \brief Reports clapp::arg_builder::exclusive(). */
        [[nodiscard]] constexpr bool is_exclusive_set() const noexcept {
            return is_set(arg_setting::exclusive);
        }

        /**
         * \brief Equality by content (not pointer identity of spans).
         * \note #parser compared via type_name() and possible_values() (address compare
         *       is not a consteval under ubsan).
         * \warning Two hand-written parser tables with the same type_name and values
         *          compare equal even if parse differs; distinguish them in type_name().
         */
        [[nodiscard]] constexpr bool operator==(const arg_spec &other) const noexcept {
            return id.name() == other.id.name() && short_ == other.short_ &&
                   long_.name() == other.long_.name() &&
                   detail::spans_equal(get_all_aliases(), other.get_all_aliases()) &&
                   detail::spans_equal(get_all_short_aliases(), other.get_all_short_aliases()) &&
                   index == other.index && act == other.act && num_args == other.num_args &&
                   parser->type_name() == other.parser->type_name() &&
                   detail::spans_equal(parser->possible_values(),
                                       other.parser->possible_values()) &&
                   hint == other.hint && delimiter == other.delimiter &&
                   terminator.name() == other.terminator.name() &&
                   detail::spans_equal(get_conflicts(), other.get_conflicts()) &&
                   detail::spans_equal(get_overrides(), other.get_overrides()) &&
                   detail::spans_equal(get_groups(), other.get_groups()) &&
                   detail::spans_equal(get_requires(), other.get_requires()) &&
                   detail::spans_equal(get_required_unless_present_any(),
                                       other.get_required_unless_present_any()) &&
                   detail::spans_equal(get_required_unless_present_all(),
                                       other.get_required_unless_present_all()) &&
                   detail::spans_equal(get_required_if_eq_any(), other.get_required_if_eq_any()) &&
                   detail::spans_equal(get_required_if_eq_all(), other.get_required_if_eq_all()) &&
                   detail::spans_equal(get_default_values(), other.get_default_values()) &&
                   detail::spans_equal(get_default_missing_values(),
                                       other.get_default_missing_values()) &&
                   detail::spans_equal(get_default_values_ifs(), other.get_default_values_ifs()) &&
                   env.name() == other.env.name() && get_help() == other.get_help() &&
                   get_long_help() == other.get_long_help() &&
                   get_help_heading() == other.get_help_heading() &&
                   detail::spans_equal(get_value_names(), other.get_value_names()) &&
                   display_order == other.display_order && settings == other.settings;
        }
    };

    // -----------------------------------------------------------------------------
    // arg_builder — the builder
    // -----------------------------------------------------------------------------

    /**
     * \brief Chainable description of one argument (clap Arg). Owns strings.
     * \warning Never `static constexpr` (holds vectors). Build in consteval and freeze().
     */
    class arg_builder {
    public:
        /**
         * \brief Create an argument with unique name \p id (positional, one value).
         * \param id Name matches use; copied. constexpr so runtime tests can build too.
         */
        constexpr explicit arg_builder(std::string_view id) : id_(id) {
        }

        /**
         * \name Identity
         * \{
         */

        /** \brief Rename the argument. \param name The new unique name. */
        constexpr arg_builder &&id(std::string_view name) && {
            id_ = std::string{name};
            return std::move(*this);
        }

        /**
         * \brief Set the short option letter, e.g. `'v'` for `-v`.
         * \param letter The letter; `'\0'` removes the short option, which is clapp's
         *        spelling of clap's `IntoResettable::None`.
         */
        constexpr arg_builder &&short_(char letter) && {
            short_name_ = letter == '\0' ? std::nullopt : std::optional<char>{letter};
            return std::move(*this);
        }

        /**
         * \brief Set the long option spelling, e.g. `"verbose"` for `--verbose`.
         * \param name The spelling without `--`; an empty view removes it.
         */
        constexpr arg_builder &&long_(std::string_view name) && {
            long_name_ = name.empty() ? std::nullopt : std::optional<std::string>{name};
            return std::move(*this);
        }

        /** \brief Add a hidden long alias — accepted by the parser, absent from help. */
        constexpr arg_builder &&alias(std::string_view name) && {
            aliases_.push_back(arg_alias{.name = std::string{name}, .visible = false});
            return std::move(*this);
        }

        /** \brief Add a visible long alias — accepted *and* listed in help. */
        constexpr arg_builder &&visible_alias(std::string_view name) && {
            aliases_.push_back(arg_alias{.name = std::string{name}, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several hidden long aliases. */
        template<string_view_range R>
        constexpr arg_builder &&aliases(R &&names) && {
            for (std::string_view name: names)
                aliases_.push_back(arg_alias{.name = std::string{name}, .visible = false});
            return std::move(*this);
        }

        /** \brief Add several hidden long aliases from a braced list. */
        constexpr arg_builder &&aliases(std::initializer_list<std::string_view> names) && {
            return std::move(*this).aliases<std::initializer_list<std::string_view> &>(names);
        }

        /** \brief Add several visible long aliases. */
        template<string_view_range R>
        constexpr arg_builder &&visible_aliases(R &&names) && {
            for (std::string_view name: names)
                aliases_.push_back(arg_alias{.name = std::string{name}, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several visible long aliases from a braced list. */
        constexpr arg_builder &&visible_aliases(std::initializer_list<std::string_view> names) && {
            return std::move(*this).visible_aliases<std::initializer_list<std::string_view> &>(
                names);
        }

        /** \brief Add a hidden short alias. */
        constexpr arg_builder &&short_alias(char letter) && {
            short_aliases_.push_back(arg_short_alias{.name = letter, .visible = false});
            return std::move(*this);
        }

        /** \brief Add a visible short alias. */
        constexpr arg_builder &&visible_short_alias(char letter) && {
            short_aliases_.push_back(arg_short_alias{.name = letter, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several hidden short aliases. */
        template<detail::char_range R>
        constexpr arg_builder &&short_aliases(R &&letters) && {
            for (char letter: letters)
                short_aliases_.push_back(arg_short_alias{.name = letter, .visible = false});
            return std::move(*this);
        }

        /** \brief Add several hidden short aliases from a braced list. */
        constexpr arg_builder &&short_aliases(std::initializer_list<char> letters) && {
            return std::move(*this).short_aliases<std::initializer_list<char> &>(letters);
        }

        /** \brief Add several visible short aliases. */
        template<detail::char_range R>
        constexpr arg_builder &&visible_short_aliases(R &&letters) && {
            for (char letter: letters)
                short_aliases_.push_back(arg_short_alias{.name = letter, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several visible short aliases from a braced list. */
        constexpr arg_builder &&visible_short_aliases(std::initializer_list<char> letters) && {
            return std::move(*this).visible_short_aliases<std::initializer_list<char> &>(letters);
        }

        /**
         * \brief Pin this positional argument to a 1-based slot.
         * \param slot The position; `0` clears an explicit index.
         * \note Only meaningful for positionals. freeze() rejects an index on an
         *       argument that also has a short or long option.
         */
        constexpr arg_builder &&index(std::size_t slot) && {
            index_ = slot == 0 ? std::nullopt : std::optional<std::size_t>{slot};
            return std::move(*this);
        }

        /** \} */

        /**
         * \name Behaviour
         * \{
         */

        /**
         * \brief What happens when this argument matches.
         * \param act One of clapp::arg_action; arg_action::infer restores inference.
         */
        constexpr arg_builder &&action(arg_action act) && {
            action_ = act == arg_action::infer ? std::nullopt : std::optional<arg_action>{act};
            return std::move(*this);
        }

        /**
         * \brief How many values the argument consumes.
         * \param range value_range::infer() restores inference from the action.
         */
        constexpr arg_builder &&num_args(value_range range) && {
            num_vals_ = range.is_infer() ? std::nullopt : std::optional<value_range>{range};
            return std::move(*this);
        }

        /** \brief Consume exactly \p count values. clap's deprecated `number_of_values`. */
        constexpr arg_builder &&number_of_values(std::size_t count) && {
            return std::move(*this).num_args(value_range::exactly(count));
        }

        /**
         * \brief Parse values as \p T.
         *
         * \tparam T Any clapp::erasable_parsable type — every built-in specialization
         *         of clapp::value_parser plus any enum, plus the user's own.
         *
         * \note Also seeds the completion hint with value_hint::any_path when \p T is
         *       `std::filesystem::path`, which is what clap's `get_value_hint()`
         *       derives from the parser's type id. Doing it here rather than at read
         *       time keeps clapp::arg_spec free of type-id comparisons.
         */
        template<erasable_parsable T>
        constexpr arg_builder &&value_parser() && {
            parser_ = parser_for<T>();
            if constexpr (std::same_as<T, std::filesystem::path>) {
                if (!hint_.has_value()) hint_ = clapp::value_hint::any_path;
            }
            return std::move(*this);
        }

        /**
         * \brief Parse with an existing dispatch table (reset via nullptr overload).
         * \param table From parser_for; must not be null at runtime.
         * \warning Runtime null is stored and later dereferenced. No guard: null compare
         *          breaks ubsan consteval. parser_for() never returns null.
         */
        constexpr arg_builder &&value_parser(const parser_vtable *table) && {
            parser_ = table;
            return std::move(*this);
        }

        /**
         * \brief Restore the action-derived default parser. clap's `Resettable::Reset`.
         * \note A separate overload rather than a null check inside the one above; see
         *       the warning there.
         */
        constexpr arg_builder &&value_parser(std::nullptr_t) && {
            parser_.reset();
            return std::move(*this);
        }

        /**
         * \brief Name the single value placeholder shown in help and usage.
         * \note Replaces any previously configured placeholders, as in clap.
         */
        constexpr arg_builder &&value_name(std::string_view name) && {
            val_names_.clear();
            val_names_.emplace_back(name);
            return std::move(*this);
        }

        /**
         * \brief Name several value placeholders.
         * \note More than one placeholder also *implies* `num_args(n)` when the count
         *       was never set explicitly — clap's rule, applied in freeze().
         */
        template<string_view_range R>
        constexpr arg_builder &&value_names(R &&names) && {
            val_names_.clear();
            for (std::string_view name: names) val_names_.emplace_back(name);
            return std::move(*this);
        }

        /** \brief Name several value placeholders from a braced list. */
        constexpr arg_builder &&value_names(std::initializer_list<std::string_view> names) && {
            return std::move(*this).value_names<std::initializer_list<std::string_view> &>(names);
        }

        /** \brief Set the shell-completion hint. */
        constexpr arg_builder &&value_hint(clapp::value_hint kind) && {
            hint_ = kind;
            return std::move(*this);
        }

        /**
         * \brief Stop collecting values at the sentinel token \p token.
         * \param token e.g. `";"`; an empty view removes the terminator.
         */
        constexpr arg_builder &&value_terminator(std::string_view token) && {
            terminator_ = token.empty() ? std::nullopt : std::optional<std::string>{token};
            return std::move(*this);
        }

        /**
         * \brief Consume everything after `--` verbatim.
         *
         * \param yes Whether to enable the bundle.
         * \note Implies `num_args(1..)` (only when the count was never set),
         *       `allow_hyphen_values(yes)` and `last(yes)` — clap's exact composition.
         */
        constexpr arg_builder &&raw(bool yes = true) && {
            if (yes && !num_vals_.has_value()) num_vals_ = value_range::at_least(1);
            return std::move(*this).allow_hyphen_values(yes).last(yes);
        }

        /** \} */

        /**
         * \name Constraints
         * \{
         */

        /** \brief Require the argument to appear. */
        constexpr arg_builder &&required(bool yes = true) && {
            return std::move(*this).setting(arg_setting::required, yes);
        }

        /** \brief When this argument appears, \p target becomes required too. */
        constexpr arg_builder &&requires_(std::string_view target) && {
            requirements_.push_back(arg_requirement{
                .when = arg_condition::present(),
                .target = std::string{target}
            });
            return std::move(*this);
        }

        /** \brief When this argument appears, every id in \p targets becomes required. */
        template<string_view_range R>
        constexpr arg_builder &&requires_all(R &&targets) && {
            for (std::string_view target: targets)
                requirements_.push_back(arg_requirement{
                    .when = arg_condition::present(),
                    .target = std::string{target}
                });
            return std::move(*this);
        }

        /** \brief `requires_all` from a braced list. */
        constexpr arg_builder &&requires_all(std::initializer_list<std::string_view> targets) && {
            return std::move(*this).requires_all<std::initializer_list<std::string_view> &>(targets);
        }

        /**
         * \brief When this argument's value equals \p value, \p target becomes required.
         * \note Argument order follows clap: the value comes first.
         */
        constexpr arg_builder &&requires_if(std::string_view value, std::string_view target) && {
            requirements_.push_back(arg_requirement{
                .when = arg_condition::equal_to(value),
                .target = std::string{target}
            });
            return std::move(*this);
        }

        /** \brief Several `requires_if` rules, each a `(value, target)` pair. */
        template<detail::string_pair_range R>
        constexpr arg_builder &&requires_ifs(R &&rules) && {
            for (std::pair<std::string_view, std::string_view> rule: rules)
                requirements_.push_back(arg_requirement{
                    .when = arg_condition::equal_to(rule.first),
                    .target = std::string{rule.second}
                });
            return std::move(*this);
        }

        /** \brief `requires_ifs` from a braced list. */
        constexpr arg_builder &&requires_ifs(
            std::initializer_list<std::pair<std::string_view, std::string_view> > rules) && {
            return std::move(*this)
                    .requires_ifs<
                        std::initializer_list<std::pair<std::string_view, std::string_view> > &>(
                        rules);
        }

        /** \brief This argument may not appear alongside \p other. */
        constexpr arg_builder &&conflicts_with(std::string_view other) && {
            conflicts_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief This argument may not appear alongside any of \p others. */
        template<string_view_range R>
        constexpr arg_builder &&conflicts_with_all(R &&others) && {
            for (std::string_view other: others) conflicts_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `conflicts_with_all` from a braced list. */
        constexpr arg_builder &&
        conflicts_with_all(std::initializer_list<std::string_view> others) && {
            return std::move(*this).conflicts_with_all<std::initializer_list<std::string_view> &>(
                others);
        }

        /**
         * \brief When both appear, this argument silently replaces \p other.
         * \note `overrides_with(own_id)` is clap's `args_override_self` idiom.
         */
        constexpr arg_builder &&overrides_with(std::string_view other) && {
            overrides_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `overrides_with` for several ids. */
        template<string_view_range R>
        constexpr arg_builder &&overrides_with_all(R &&others) && {
            for (std::string_view other: others) overrides_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `overrides_with_all` from a braced list. */
        constexpr arg_builder &&
        overrides_with_all(std::initializer_list<std::string_view> others) && {
            return std::move(*this).overrides_with_all<std::initializer_list<std::string_view> &>(
                others);
        }

        /**
         * \brief This argument is required *unless* \p other is present.
         * \note Stacking calls means "unless **any** of them is present".
         */
        constexpr arg_builder &&required_unless_present(std::string_view other) && {
            r_unless_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief Required unless **any** of \p others is present. */
        template<string_view_range R>
        constexpr arg_builder &&required_unless_present_any(R &&others) && {
            for (std::string_view other: others) r_unless_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `required_unless_present_any` from a braced list. */
        constexpr arg_builder &&
        required_unless_present_any(std::initializer_list<std::string_view> others) && {
            return std::move(*this)
                    .required_unless_present_any<std::initializer_list<std::string_view> &>(others);
        }

        /** \brief Required unless **all** of \p others are present. */
        template<string_view_range R>
        constexpr arg_builder &&required_unless_present_all(R &&others) && {
            for (std::string_view other: others) r_unless_all_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `required_unless_present_all` from a braced list. */
        constexpr arg_builder &&
        required_unless_present_all(std::initializer_list<std::string_view> others) && {
            return std::move(*this)
                    .required_unless_present_all<std::initializer_list<std::string_view> &>(others);
        }

        /**
         * \brief Required when \p other took the value \p value.
         * \note Stacking calls means "when **any** of them matches".
         */
        constexpr arg_builder &&required_if_eq(std::string_view other, std::string_view value) && {
            r_ifs_.push_back(
                arg_value_requirement{.id = std::string{other}, .value = std::string{value}});
            return std::move(*this);
        }

        /** \brief Required when **any** `(id, value)` rule in \p rules matches. */
        template<detail::string_pair_range R>
        constexpr arg_builder &&required_if_eq_any(R &&rules) && {
            for (std::pair<std::string_view, std::string_view> rule: rules)
                r_ifs_.push_back(arg_value_requirement{
                    .id = std::string{rule.first},
                    .value = std::string{rule.second}
                });
            return std::move(*this);
        }

        /** \brief `required_if_eq_any` from a braced list. */
        constexpr arg_builder &&required_if_eq_any(
            std::initializer_list<std::pair<std::string_view, std::string_view> > rules) && {
            return std::move(*this)
                    .required_if_eq_any<
                        std::initializer_list<std::pair<std::string_view, std::string_view> > &>(
                        rules);
        }

        /** \brief Required when **every** `(id, value)` rule in \p rules matches. */
        template<detail::string_pair_range R>
        constexpr arg_builder &&required_if_eq_all(R &&rules) && {
            for (std::pair<std::string_view, std::string_view> rule: rules)
                r_ifs_all_.push_back(arg_value_requirement{
                    .id = std::string{rule.first},
                    .value = std::string{rule.second}
                });
            return std::move(*this);
        }

        /** \brief `required_if_eq_all` from a braced list. */
        constexpr arg_builder &&required_if_eq_all(
            std::initializer_list<std::pair<std::string_view, std::string_view> > rules) && {
            return std::move(*this)
                    .required_if_eq_all<
                        std::initializer_list<std::pair<std::string_view, std::string_view> > &>(
                        rules);
        }

        /** \brief This argument may not be combined with *any* other argument. */
        constexpr arg_builder &&exclusive(bool yes = true) && {
            return std::move(*this).setting(arg_setting::exclusive, yes);
        }

        /** \brief Join the clapp::arg_group named \p group_id. */
        constexpr arg_builder &&group(std::string_view group_id) && {
            groups_.emplace_back(group_id);
            return std::move(*this);
        }

        /** \brief Join several groups. */
        template<string_view_range R>
        constexpr arg_builder &&groups(R &&group_ids) && {
            for (std::string_view group_id: group_ids) groups_.emplace_back(group_id);
            return std::move(*this);
        }

        /** \brief `groups` from a braced list. */
        constexpr arg_builder &&groups(std::initializer_list<std::string_view> group_ids) && {
            return std::move(*this).groups<std::initializer_list<std::string_view> &>(group_ids);
        }

        /** \} */

        /**
         * \name Value sources
         * \{
         */

        /**
         * \brief The value used when the argument never appears.
         * \note Replaces any previously configured defaults, as in clap.
         */
        constexpr arg_builder &&default_value(std::string_view value) && {
            default_vals_.clear();
            default_vals_.emplace_back(value);
            return std::move(*this);
        }

        /** \brief Several values used when the argument never appears. */
        template<string_view_range R>
        constexpr arg_builder &&default_values(R &&values) && {
            default_vals_.clear();
            for (std::string_view value: values) default_vals_.emplace_back(value);
            return std::move(*this);
        }

        /** \brief `default_values` from a braced list. */
        constexpr arg_builder &&default_values(std::initializer_list<std::string_view> values) && {
            return std::move(*this).default_values<std::initializer_list<std::string_view> &>(
                values);
        }

        /**
         * \brief The value used when the argument appears without one of its own.
         * \note Only reachable when `num_args` allows zero values, e.g.
         *       `num_args(value_range::optional())`.
         */
        constexpr arg_builder &&default_missing_value(std::string_view value) && {
            default_missing_vals_.clear();
            default_missing_vals_.emplace_back(value);
            return std::move(*this);
        }

        /** \brief Several values used when the argument appears without its own. */
        template<string_view_range R>
        constexpr arg_builder &&default_missing_values(R &&values) && {
            default_missing_vals_.clear();
            for (std::string_view value: values) default_missing_vals_.emplace_back(value);
            return std::move(*this);
        }

        /** \brief `default_missing_values` from a braced list. */
        constexpr arg_builder &&
        default_missing_values(std::initializer_list<std::string_view> values) && {
            return std::move(*this)
                    .default_missing_values<std::initializer_list<std::string_view> &>(values);
        }

        /**
         * \brief Use \p value as the default when \p other satisfies \p when.
         *
         * \param other The argument being watched.
         * \param when  The condition; clapp::arg_condition::present() or ::equal_to().
         * \param value The replacement values; `std::nullopt` **removes** the default.
         *
         * \note Rules are evaluated in declaration order and the first match wins,
         *       which is why `std::nullopt` is not the same as an empty list.
         */
        constexpr arg_builder &&default_value_if(std::string_view other,
                                                 arg_condition when,
                                                 std::optional<std::string_view> value) && {
            arg_default_condition rule{
                .id = std::string{other},
                .when = std::move(when),
                .has_values = value.has_value(),
                .values = {}
            };
            if (value.has_value()) rule.values.emplace_back(*value);
            default_vals_ifs_.push_back(std::move(rule));
            return std::move(*this);
        }

        /** \brief Use \p value as the default when \p other equals \p trigger. */
        constexpr arg_builder &&default_value_if(std::string_view other,
                                                 std::string_view trigger,
                                                 std::string_view value) && {
            return std::move(*this).default_value_if(other,
                                                     arg_condition::equal_to(trigger),
                                                     std::optional<std::string_view>{value});
        }

        /** \brief Like default_value_if() but supplying several values. */
        template<string_view_range R>
        constexpr arg_builder &&
        default_values_if(std::string_view other, arg_condition when, R &&values) && {
            arg_default_condition rule{
                .id = std::string{other},
                .when = std::move(when),
                .has_values = true,
                .values = {}
            };
            for (std::string_view value: values) rule.values.emplace_back(value);
            default_vals_ifs_.push_back(std::move(rule));
            return std::move(*this);
        }

        /** \brief `default_values_if` from a braced list. */
        constexpr arg_builder &&
        default_values_if(std::string_view other,
                          arg_condition when,
                          std::initializer_list<std::string_view> values) && {
            return std::move(*this).default_values_if<std::initializer_list<std::string_view> &>(
                other, std::move(when), values);
        }

        /** \brief Add several conditional-default rules at once. */
        template<detail::default_value_rule_range R>
        constexpr arg_builder &&default_value_ifs(R &&rules) && {
            for (const default_value_rule &rule: rules) {
                arg_default_condition entry{
                    .id = std::string{rule.id},
                    .when = rule.when,
                    .has_values = rule.value.has_value(),
                    .values = {}
                };
                if (rule.value.has_value()) entry.values.emplace_back(*rule.value);
                default_vals_ifs_.push_back(std::move(entry));
            }
            return std::move(*this);
        }

        /** \brief `default_value_ifs` from a braced list. */
        constexpr arg_builder &&
        default_value_ifs(std::initializer_list<default_value_rule> rules) && {
            return std::move(*this).default_value_ifs<std::initializer_list<default_value_rule> &>(
                rules);
        }

        /**
         * \brief Read the value from the environment variable \p name when absent.
         * \param name The variable name; an empty view removes it.
         */
        constexpr arg_builder &&env(std::string_view name) && {
            env_ = name.empty() ? std::nullopt : std::optional<std::string>{name};
            return std::move(*this);
        }

        /** \} */

        /**
         * \name Help presentation
         * \{
         */

        /** \brief The one-line description shown beside the argument. */
        constexpr arg_builder &&help(std::string_view text) && {
            help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief The longer description shown by `--help` but not `-h`. */
        constexpr arg_builder &&long_help(std::string_view text) && {
            long_help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief The sort key within a help section. clap's default is 999. */
        constexpr arg_builder &&display_order(std::size_t order) && {
            disp_ord_ = order;
            return std::move(*this);
        }

        /**
         * \brief Override the command help section; empty means no heading (not inherit).
         * \param heading Section title; empty = explicit none. Never calling inherits.
         */
        constexpr arg_builder &&help_heading(std::string_view heading) && {
            help_heading_ = std::string{heading};
            return std::move(*this);
        }

        /** \brief Put the help text on the line below the argument. */
        constexpr arg_builder &&next_line_help(bool yes = true) && {
            return std::move(*this).setting(arg_setting::next_line_help, yes);
        }

        /** \brief Omit the argument from every help output. */
        constexpr arg_builder &&hide(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hidden, yes);
        }

        /** \brief Suppress the `[possible values: ...]` line. */
        constexpr arg_builder &&hide_possible_values(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hide_possible_values, yes);
        }

        /** \brief Suppress the `[default: ...]` line. */
        constexpr arg_builder &&hide_default_value(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hide_default_value, yes);
        }

        /** \brief Suppress the `[env: NAME]` line. */
        constexpr arg_builder &&hide_env(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hide_env, yes);
        }

        /** \brief Show `[env: NAME]` but not the variable's current value. */
        constexpr arg_builder &&hide_env_values(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hide_env_values, yes);
        }

        /** \brief Hide from `-h`, keep in `--help`. */
        constexpr arg_builder &&hide_short_help(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hidden_short_help, yes);
        }

        /** \brief Hide from `--help`, keep in `-h`. */
        constexpr arg_builder &&hide_long_help(bool yes = true) && {
            return std::move(*this).setting(arg_setting::hidden_long_help, yes);
        }

        /** \} */

        /**
         * \name Parsing modifiers
         * \{
         */

        /** \brief Allow a value to begin with `-`. */
        constexpr arg_builder &&allow_hyphen_values(bool yes = true) && {
            return std::move(*this).setting(arg_setting::allow_hyphen_values, yes);
        }

        /** \brief Allow a value that looks like a negative number, but not a flag. */
        constexpr arg_builder &&allow_negative_numbers(bool yes = true) && {
            return std::move(*this).setting(arg_setting::allow_negative_numbers, yes);
        }

        /** \brief Demand `--opt=value`; reject `--opt value`. */
        constexpr arg_builder &&require_equals(bool yes = true) && {
            return std::move(*this).setting(arg_setting::require_equals, yes);
        }

        /**
         * \brief Split values on `,`, or stop splitting.
         * \note clap deprecated this in favour of value_delimiter(); it is kept because
         *       the derive layer's `#[arg_builder(use_value_delimiter)]` still spells it so.
         */
        constexpr arg_builder &&use_value_delimiter(bool yes = true) && {
            if (yes) {
                if (!val_delim_.has_value()) val_delim_ = ',';
            } else {
                val_delim_ = std::nullopt;
            }
            return std::move(*this);
        }

        /** \brief Split each value on \p separator. `'\0'` stops splitting. */
        constexpr arg_builder &&value_delimiter(char separator) && {
            val_delim_ = separator == '\0' ? std::nullopt : std::optional<char>{separator};
            return std::move(*this);
        }

        /** \brief Match possible values without regard to case. */
        constexpr arg_builder &&ignore_case(bool yes = true) && {
            return std::move(*this).setting(arg_setting::ignore_case, yes);
        }

        /** \brief Treat everything from this argument onwards as its values. */
        constexpr arg_builder &&trailing_var_arg(bool yes = true) && {
            return std::move(*this).setting(arg_setting::trailing_var_arg, yes);
        }

        /** \brief Only reachable after a `--` escape. */
        constexpr arg_builder &&last(bool yes = true) && {
            return std::move(*this).setting(arg_setting::last, yes);
        }

        /** \brief Make the argument visible to every subcommand. */
        constexpr arg_builder &&global(bool yes = true) && {
            return std::move(*this).setting(arg_setting::global, yes);
        }

        /** \} */

        /**
         * \name Raw setting access
         * \{
         */

        /** \brief Turn \p setting on. */
        constexpr arg_builder &&setting(arg_setting knob) && {
            settings_.set(knob);
            return std::move(*this);
        }

        /** \brief Turn \p setting on or off. */
        constexpr arg_builder &&setting(arg_setting knob, bool enable) && {
            settings_.set(knob, enable);
            return std::move(*this);
        }

        /** \brief Turn \p setting off. */
        constexpr arg_builder &&unset_setting(arg_setting knob) && {
            settings_.unset(knob);
            return std::move(*this);
        }

        /** \brief Whether \p setting is on. */
        [[nodiscard]] constexpr bool is_set(arg_setting knob) const noexcept {
            return settings_.is_set(knob);
        }

        /** \brief The whole flag word. */
        [[nodiscard]] constexpr arg_flags get_settings() const noexcept { return settings_; }

        /** \} */

        /**
         * \name Reflection
         * The getters clap exposes, adapted where clapp resolves something clap defers.
         * \{
         */

        /** \brief The unique name. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::string_view get_id() const noexcept { return id_; }

        /** \brief The short option letter, if any. */
        [[nodiscard]] constexpr std::optional<char> get_short() const noexcept {
            return short_name_;
        }

        /** \brief The long option spelling, if any. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long() const noexcept {
            if (!long_name_.has_value()) return std::nullopt;
            return std::string_view{*long_name_};
        }

        /** \brief Every long alias. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_alias> get_all_aliases() const noexcept {
            return std::span<const arg_alias>{aliases_};
        }

        /** \brief Every short alias. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_short_alias>
        get_all_short_aliases() const noexcept {
            return std::span<const arg_short_alias>{short_aliases_};
        }

        /** \brief The explicit positional slot, if one was set. */
        [[nodiscard]] constexpr std::optional<std::size_t> get_index() const noexcept {
            return index_;
        }

        /** \brief Whether the argument is matched by position. */
        [[nodiscard]] constexpr bool is_positional() const noexcept {
            return !short_name_.has_value() && !long_name_.has_value();
        }

        /**
         * \brief Resolved action (never infer); matches freeze() inference.
         * \return Never arg_action::infer.
         */
        [[nodiscard]] constexpr arg_action get_action() const noexcept {
            if (action_.has_value()) return *action_;
            const value_range requested = num_vals_.value_or(value_range::infer());
            if (!requested.is_infer() && requested == value_range::empty())
                return arg_action::set_true;
            if (is_positional() && requested.resolve_or(value_range::single()).is_unbounded())
                return arg_action::append;
            return arg_action::set;
        }

        /**
         * \brief The value count, **resolved** against get_action() and value_names().
         * \return Never value_range::infer().
         */
        [[nodiscard]] constexpr value_range get_num_args() const noexcept {
            if (num_vals_.has_value() && !num_vals_->is_infer()) return *num_vals_;
            if (val_names_.size() > 1) return value_range::exactly(val_names_.size());
            return default_num_args(get_action());
        }

        /** \brief The value count exactly as configured, before resolution. */
        [[nodiscard]] constexpr std::optional<value_range>
        get_configured_num_args() const noexcept {
            return num_vals_;
        }

        /**
         * \brief The dispatch table, **resolved**.
         *
         * \return The configured table, or the action's own — `bool` for
         *         arg_action::set_true / set_false, clapp::count_type for
         *         arg_action::count — or `std::string` for everything else.
         */
        [[nodiscard]] constexpr const parser_vtable *get_value_parser() const noexcept {
            if (parser_.has_value()) return *parser_;
            switch (get_action()) {
                case arg_action::set_true:
                case arg_action::set_false:
                    return parser_for<bool>();
                case arg_action::count:
                    return parser_for<count_type>();
                default:
                    return parser_for<std::string>();
            }
        }

        /** \brief The accepted values, or empty when the type does not enumerate. */
        [[nodiscard]] constexpr std::span<const possible_value>
        get_possible_values() const noexcept {
            if (!get_num_args().takes_values()) return {};
            return get_value_parser()->possible_values();
        }

        /** \brief The value placeholders. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_value_names() const noexcept {
            return std::span<const std::string>{val_names_};
        }

        /** \brief The completion hint. */
        [[nodiscard]] constexpr clapp::value_hint get_value_hint() const noexcept {
            return hint_.value_or(clapp::value_hint::unknown);
        }

        /** \brief The value separator, if any. */
        [[nodiscard]] constexpr std::optional<char> get_value_delimiter() const noexcept {
            return val_delim_;
        }

        /** \brief The multi-value terminator, if any. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_value_terminator() const noexcept {
            if (!terminator_.has_value()) return std::nullopt;
            return std::string_view{*terminator_};
        }

        /** \brief The environment variable, if any. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_env() const noexcept {
            if (!env_.has_value()) return std::nullopt;
            return std::string_view{*env_};
        }

        /**
         * \brief The configured default values, *before* the action supplies its own.
         * \warning Borrows `*this`.
         */
        [[nodiscard]] constexpr std::span<const std::string> get_default_values() const noexcept {
            return std::span<const std::string>{default_vals_};
        }

        /** \brief The configured default-missing values. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string>
        get_default_missing_values() const noexcept {
            return std::span<const std::string>{default_missing_vals_};
        }

        /** \brief The conditional-default rules. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_default_condition>
        get_default_values_ifs() const noexcept {
            return std::span<const arg_default_condition>{default_vals_ifs_};
        }

        /** \brief Conflicting argument ids. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_conflicts() const noexcept {
            return std::span<const std::string>{conflicts_};
        }

        /** \brief Overridden argument ids. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_overrides() const noexcept {
            return std::span<const std::string>{overrides_};
        }

        /** \brief Group memberships. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_groups() const noexcept {
            return std::span<const std::string>{groups_};
        }

        /** \brief Conditional requirements. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_requirement> get_requires() const noexcept {
            return std::span<const arg_requirement>{requirements_};
        }

        /** \brief "Required unless any of these" ids. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string>
        get_required_unless_present_any() const noexcept {
            return std::span<const std::string>{r_unless_};
        }

        /** \brief "Required unless all of these" ids. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string>
        get_required_unless_present_all() const noexcept {
            return std::span<const std::string>{r_unless_all_};
        }

        /** \brief "Required if any of these matches" rules. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_value_requirement>
        get_required_if_eq_any() const noexcept {
            return std::span<const arg_value_requirement>{r_ifs_};
        }

        /** \brief "Required if all of these match" rules. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_value_requirement>
        get_required_if_eq_all() const noexcept {
            return std::span<const arg_value_requirement>{r_ifs_all_};
        }

        /** \brief The one-line help text, if any. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help() const noexcept {
            if (!help_.has_value()) return std::nullopt;
            return std::string_view{*help_};
        }

        /** \brief The long help text, if any. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_help() const noexcept {
            if (!long_help_.has_value()) return std::nullopt;
            return std::string_view{*long_help_};
        }

        /** \brief The help section override, if any. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help_heading() const noexcept {
            if (!help_heading_.has_value()) return std::nullopt;
            return std::string_view{*help_heading_};
        }

        /** \brief The sort key within a help section; 999 when unset, as in clap. */
        [[nodiscard]] constexpr std::size_t get_display_order() const noexcept {
            return disp_ord_.value_or(999);
        }

        /**
         * \brief Configured display_order before defaulting to 999.
         * \return nullopt if display_order() was never called (999 may be explicit).
         */
        [[nodiscard]] constexpr std::optional<std::size_t>
        get_configured_display_order() const noexcept {
            return disp_ord_;
        }

        /** \brief Whether the argument consumes values at all, after resolution. */
        [[nodiscard]] constexpr bool is_takes_value_set() const noexcept {
            return get_num_args().takes_values();
        }

        /** \brief Whether it can consume more than one value, after resolution. */
        [[nodiscard]] constexpr bool is_multiple_values_set() const noexcept {
            return get_num_args().is_multiple();
        }

        /**
         * \brief Whether the argument may end up holding more than one value.
         *
         * clap's `Arg::is_multiple`; see clapp::arg_spec::is_multiple() for why it is
         * not the same predicate as #is_multiple_values_set().
         */
        [[nodiscard]] constexpr bool is_multiple() const noexcept {
            return get_num_args().is_multiple() || get_action() == arg_action::append;
        }

        /** \brief Reports required(). */
        [[nodiscard]] constexpr bool is_required_set() const noexcept {
            return is_set(arg_setting::required);
        }

        /** \brief Reports global(). */
        [[nodiscard]] constexpr bool is_global_set() const noexcept {
            return is_set(arg_setting::global);
        }

        /** \brief Reports hide(). */
        [[nodiscard]] constexpr bool is_hide_set() const noexcept {
            return is_set(arg_setting::hidden);
        }

        /** \brief Reports next_line_help(). */
        [[nodiscard]] constexpr bool is_next_line_help_set() const noexcept {
            return is_set(arg_setting::next_line_help);
        }

        /** \brief Reports hide_possible_values(). */
        [[nodiscard]] constexpr bool is_hide_possible_values_set() const noexcept {
            return is_set(arg_setting::hide_possible_values);
        }

        /** \brief Reports allow_hyphen_values(). */
        [[nodiscard]] constexpr bool is_allow_hyphen_values_set() const noexcept {
            return is_set(arg_setting::allow_hyphen_values);
        }

        /** \brief Reports allow_negative_numbers(). */
        [[nodiscard]] constexpr bool is_allow_negative_numbers_set() const noexcept {
            return is_set(arg_setting::allow_negative_numbers);
        }

        /** \brief Reports require_equals(). */
        [[nodiscard]] constexpr bool is_require_equals_set() const noexcept {
            return is_set(arg_setting::require_equals);
        }

        /** \brief Reports last(). */
        [[nodiscard]] constexpr bool is_last_set() const noexcept {
            return is_set(arg_setting::last);
        }

        /** \brief Reports trailing_var_arg(). */
        [[nodiscard]] constexpr bool is_trailing_var_arg_set() const noexcept {
            return is_set(arg_setting::trailing_var_arg);
        }

        /** \brief Reports hide_default_value(). */
        [[nodiscard]] constexpr bool is_hide_default_value_set() const noexcept {
            return is_set(arg_setting::hide_default_value);
        }

        /** \brief Reports ignore_case(). */
        [[nodiscard]] constexpr bool is_ignore_case_set() const noexcept {
            return is_set(arg_setting::ignore_case);
        }

        /** \brief Reports hide_env(). */
        [[nodiscard]] constexpr bool is_hide_env_set() const noexcept {
            return is_set(arg_setting::hide_env);
        }

        /** \brief Reports hide_env_values(). */
        [[nodiscard]] constexpr bool is_hide_env_values_set() const noexcept {
            return is_set(arg_setting::hide_env_values);
        }

        /** \brief Reports hide_short_help(). */
        [[nodiscard]] constexpr bool is_hide_short_help_set() const noexcept {
            return is_set(arg_setting::hidden_short_help);
        }

        /** \brief Reports hide_long_help(). */
        [[nodiscard]] constexpr bool is_hide_long_help_set() const noexcept {
            return is_set(arg_setting::hidden_long_help);
        }

        /** \brief Reports exclusive(). */
        [[nodiscard]] constexpr bool is_exclusive_set() const noexcept {
            return is_set(arg_setting::exclusive);
        }

        /** \} */

        /**
         * \brief Resolve, validate, and promote into a static arg_spec (clap Arg::_build).
         *
         * Resolves action, num_args, action defaults, and parser; rejects empty id,
         * bad index/num_args/hint/value_names/require_equals, and knobs that need values.
         * required+default_value and inert value_delimiter are allowed (as in clap).
         *
         * \return Frozen arg_spec (static strings; id unbound to any id_table).
         * \warning Four checks are also in command_builder::check_arg_shape() (named
         *          diagnostics there): max_num_args, index+option, index on no-value,
         *          and value_name count. Keep both; these local checks lack the arg name.
         */
        [[nodiscard]] consteval arg_spec freeze() const {
            if (id_.empty()) std::abort();
            if (index_.has_value() && !is_positional()) std::abort();

            const arg_action act = get_action();
            const value_range nargs = get_num_args();
            if (!nargs.is_within(max_num_args(act))) std::abort();
            if (act == arg_action::count &&
                get_value_parser()->type_name() != parser_for<count_type>()->type_name())
                std::abort();
            if (index_.has_value() && !nargs.takes_values()) std::abort();
            if (hint_.has_value() && *hint_ != clapp::value_hint::unknown) {
                if (!nargs.takes_values()) std::abort();
                if (*hint_ == clapp::value_hint::command_with_arguments && !nargs.is_multiple())
                    std::abort();
            }
            if (nargs.takes_values() && nargs.max_values() < val_names_.size()) std::abort();
            if (is_set(arg_setting::require_equals) && nargs.min_values() > 1) std::abort();
            check_needs_values(nargs);

            // The action-supplied fallbacks. Copies rather than views because the
            // strings come from a `constexpr` table and must be promoted alongside the
            // author's own values.
            std::vector<std::string> defaults = default_vals_;
            if (defaults.empty()) {
                if (const std::optional<std::string_view> seed = default_value_of(act))
                    defaults.emplace_back(*seed);
            }
            std::vector<std::string> missing = default_missing_vals_;
            if (missing.empty()) {
                if (const std::optional<std::string_view> seed = default_missing_value_of(act))
                    missing.emplace_back(*seed);
            }

            const std::span<const alias_spec> aliases = detail::promote_aliases(aliases_);
            const std::span<const short_alias_spec> short_aliases =
                    detail::promote_short_aliases(short_aliases_);
            const std::span<const arg_id> conflicts = detail::promote_ids(conflicts_);
            const std::span<const arg_id> overrides = detail::promote_ids(overrides_);
            const std::span<const arg_id> groups = detail::promote_ids(groups_);
            const std::span<const requires_spec> requirements =
                    detail::promote_requirements(requirements_);
            const std::span<const arg_id> unless_any = detail::promote_ids(r_unless_);
            const std::span<const arg_id> unless_all = detail::promote_ids(r_unless_all_);
            const std::span<const required_if_spec> if_any =
                    detail::promote_value_requirements(r_ifs_);
            const std::span<const required_if_spec> if_all =
                    detail::promote_value_requirements(r_ifs_all_);
            const std::span<const arg_id> default_ids = detail::promote_ids(defaults);
            const std::span<const arg_id> missing_ids = detail::promote_ids(missing);
            const std::span<const default_value_spec> default_ifs =
                    detail::promote_default_conditions(default_vals_ifs_);
            const std::span<const arg_id> value_names = detail::promote_ids(val_names_);

            return arg_spec{
                .id = make_static_id(id_),
                .short_ = short_name_.value_or('\0'),
                .help_heading_present = help_heading_.has_value(),
                .long_ = long_name_.has_value() ? make_static_id(*long_name_) : arg_id{},
                .alias_data = aliases.data(),
                .alias_count = aliases.size(),
                .short_alias_data = short_aliases.data(),
                .short_alias_count = short_aliases.size(),
                .index = index_.value_or(0),
                .act = act,
                .num_args = nargs,
                .parser = get_value_parser(),
                .hint = get_value_hint(),
                .delimiter = val_delim_.value_or('\0'),
                .terminator = terminator_.has_value() ? make_static_id(*terminator_) : arg_id{},
                .conflict_data = conflicts.data(),
                .conflict_count = conflicts.size(),
                .override_data = overrides.data(),
                .override_count = overrides.size(),
                .group_data = groups.data(),
                .group_count = groups.size(),
                .requires_data = requirements.data(),
                .requires_count = requirements.size(),
                .required_unless_any_data = unless_any.data(),
                .required_unless_any_count = unless_any.size(),
                .required_unless_all_data = unless_all.data(),
                .required_unless_all_count = unless_all.size(),
                .required_if_any_data = if_any.data(),
                .required_if_any_count = if_any.size(),
                .required_if_all_data = if_all.data(),
                .required_if_all_count = if_all.size(),
                .default_data = default_ids.data(),
                .default_count = default_ids.size(),
                .default_missing_data = missing_ids.data(),
                .default_missing_count = missing_ids.size(),
                .default_if_data = default_ifs.data(),
                .default_if_count = default_ifs.size(),
                .env = env_.has_value() ? make_static_id(*env_) : arg_id{},
                .help_text = help_.has_value() ? std::define_static_string(*help_) : nullptr,
                .help_length = help_.has_value() ? help_->size() : 0,
                .long_help_text = long_help_.has_value()
                                      ? std::define_static_string(*long_help_)
                                      : nullptr,
                .long_help_length = long_help_.has_value() ? long_help_->size() : 0,
                .help_heading_text = help_heading_.has_value()
                                         ? std::define_static_string(*help_heading_)
                                         : nullptr,
                .help_heading_length = help_heading_.has_value() ? help_heading_->size() : 0,
                .value_name_data = value_names.data(),
                .value_name_count = value_names.size(),
                .display_order = get_display_order(),
                .settings = settings_,
            };
        }

    private:
        /**
         * clap's `assert_arg_flags`: eight knobs that only mean anything on an
         * argument that consumes values.
         *
         * \param nargs The resolved value count, so the caller does not pay for
         *        get_num_args() a ninth time.
         *
         * \note A table plus a raw loop rather than a `views` pipeline: the message
         *       has to name *which* knob failed, so the loop body is the diagnostic,
         *       not a predicate. `detail::fail()` is unavailable here — it lives in
         *       command.hpp, which includes this file — so each entry carries a
         *       ready-made sentence instead of assembling one.
         */
        consteval void check_needs_values(value_range nargs) const {
            if (nargs.takes_values()) return;
            struct rule {
                arg_setting knob;
                std::string_view message;
            };
            constexpr rule rules[] = {
                {
                    arg_setting::hide_possible_values,
                    "clapp::arg_builder::freeze: hide_possible_values() needs an argument that "
                    "takes values"
                },
                {
                    arg_setting::allow_hyphen_values,
                    "clapp::arg_builder::freeze: allow_hyphen_values() needs an argument that "
                    "takes values"
                },
                {
                    arg_setting::allow_negative_numbers,
                    "clapp::arg_builder::freeze: allow_negative_numbers() needs an argument that "
                    "takes values"
                },
                {
                    arg_setting::require_equals,
                    "clapp::arg_builder::freeze: require_equals() needs an argument that takes "
                    "values"
                },
                {
                    arg_setting::last,
                    "clapp::arg_builder::freeze: last() needs an argument that takes values"
                },
                {
                    arg_setting::hide_default_value,
                    "clapp::arg_builder::freeze: hide_default_value() needs an argument that "
                    "takes values"
                },
                {
                    arg_setting::ignore_case,
                    "clapp::arg_builder::freeze: ignore_case() needs an argument that takes "
                    "values"
                },
            };
            for (const rule &entry: rules)
                if (is_set(entry.knob)) std::abort();
        }

        std::string id_;
        std::optional<std::string> help_;
        std::optional<std::string> long_help_;
        std::optional<arg_action> action_;
        std::optional<const parser_vtable *> parser_;
        std::vector<std::string> conflicts_;
        arg_flags settings_{};
        std::vector<std::string> overrides_;
        std::vector<std::string> groups_;
        std::vector<arg_requirement> requirements_;
        std::vector<arg_value_requirement> r_ifs_;
        std::vector<arg_value_requirement> r_ifs_all_;
        std::vector<std::string> r_unless_;
        std::vector<std::string> r_unless_all_;
        std::optional<char> short_name_;
        std::optional<std::string> long_name_;
        std::vector<arg_alias> aliases_;
        std::vector<arg_short_alias> short_aliases_;
        std::optional<std::size_t> disp_ord_;
        std::vector<std::string> val_names_;
        std::optional<value_range> num_vals_;
        std::optional<char> val_delim_;
        std::vector<std::string> default_vals_;
        std::vector<arg_default_condition> default_vals_ifs_;
        std::vector<std::string> default_missing_vals_;
        std::optional<std::string> env_;
        std::optional<std::string> terminator_;
        std::optional<std::size_t> index_;
        std::optional<std::string> help_heading_;
        std::optional<clapp::value_hint> hint_;
    };

    namespace detail {
        /**
         * \brief Usage placeholders for \p arg (e.g. `<FILE> <FILE>`; clap render_arg_val).
         * \param arg Argument to describe.
         * \param required Whether the caller treats it as required (not only is_required_set).
         * \return Placeholders without the flag name; repeats one name to min_values.
         */
        [[nodiscard]] constexpr std::string render_arg_values(const arg_spec &arg, bool required) {
            const value_range num_vals =
                    arg.get_num_args().resolve_or(default_num_args(arg.get_action()));

            std::vector<std::string_view> names;
            for (const arg_id &one: arg.get_value_names()) names.push_back(one.name());
            if (names.empty()) names.push_back(arg.get_id().name());
            if (names.size() == 1) {
                const std::string_view only = names.front();
                const std::size_t repeats =
                        num_vals.min_values() > 1 ? num_vals.min_values() : std::size_t{1};
                names.assign(repeats, only);
            }

            const std::size_t min_vals = num_vals.min_values();
            const bool positional = arg.is_positional();

            std::string rendered;
            for (std::size_t n = 0; n < names.size(); ++n) {
                const bool optional_val = min_vals == 0;
                const bool past_min = min_vals <= n;
                const bool optional =
                        positional ? (!required || past_min) : (!optional_val && past_min);
                if (n != 0) rendered.push_back(' ');
                rendered.push_back(optional ? '[' : '<');
                append_bytes(rendered, names[n]);
                rendered.push_back(optional ? ']' : '>');
            }

            bool extra_values = names.size() < num_vals.max_values();
            if (positional && arg.get_action() == arg_action::append) extra_values = true;
            if (extra_values) append_bytes(rendered, "...");
            return rendered;
        }

        /**
         * Compile-time contract: everything reachable from a clapp::arg_spec has to be
         * a structural type, or `command_of<T>()` cannot put an array of them in
         * `.rodata` with `std::define_static_array`. Failing here names this file and
         * the offending type; failing inside `define_static_array` names neither.
         */
        template<arg_flags>
        struct arg_flags_probe {
        };

        template<arg_predicate>
        struct arg_predicate_probe {
        };

        template<alias_spec>
        struct alias_spec_probe {
        };

        template<short_alias_spec>
        struct short_alias_spec_probe {
        };

        template<requires_spec>
        struct requires_spec_probe {
        };

        template<required_if_spec>
        struct required_if_spec_probe {
        };

        template<default_value_spec>
        struct default_value_spec_probe {
        };

        template<arg_spec>
        struct arg_spec_probe {
        };

        // ===================================================================
        // The one copy of clap's `long_help_exists_`
        // ===================================================================

        /**
         * \brief What long_help_exists_over() needs of an argument.
         *
         * clapp::arg_builder and clapp::arg_spec both satisfy it, which is the whole
         * point: the predicate has to be answerable before freeze() (to decide what the
         * injected `--help` says) and after it (to decide whether `--help` collapses
         * onto `-h`), and those are two different types.
         */
        template<class Arg>
        concept long_help_contributor = requires(const Arg &candidate)
        {
            { candidate.is_hide_set() } -> std::convertible_to<bool>;
            { candidate.is_hide_long_help_set() } -> std::convertible_to<bool>;
            { candidate.is_hide_short_help_set() } -> std::convertible_to<bool>;
            { candidate.is_hide_possible_values_set() } -> std::convertible_to<bool>;
            { candidate.get_long_help() } -> std::convertible_to<std::optional<std::string_view> >;
            { candidate.get_possible_values() };
        };

        /**
         * \brief Whether \p candidate alone gives `--help` something `-h` lacks.
         *
         * \tparam Arg A clapp::detail::long_help_contributor.
         * \param candidate The argument to inspect.
         * \return Whether it has a long form, hides itself from one of the two screens,
         *         or carries possible values whose help only `--help` prints.
         */
        template<long_help_contributor Arg>
        [[nodiscard]] constexpr bool arg_has_long_form(const Arg &candidate) noexcept {
            if (candidate.is_hide_set()) return false;
            if (candidate.get_long_help().has_value()) return true;
            if (candidate.is_hide_long_help_set() || candidate.is_hide_short_help_set())
                return true;
            if (candidate.is_hide_possible_values_set()) return false;
            return std::ranges::any_of(candidate.get_possible_values(),
                                       &possible_value::should_show_help);
        }

        /**
         * \brief clap long_help_exists_ over builder parts or a frozen command_spec.
         * \tparam Args Range of long_help_contributor.
         * \param long_about Command long_about if any.
         * \param before_long_help before_long_help if any.
         * \param after_long_help after_long_help if any.
         * \param args Arguments.
         * \return Whether --help shows more than -h.
         * \warning Single shared implementation for inject_help_and_version and
         *          long_help_exists(command_spec) — dual copies diverged silently before.
         *          Subcommands not consulted (clap).
         */
        template<std::ranges::input_range Args>
            requires long_help_contributor<std::ranges::range_value_t<Args> >
        [[nodiscard]] constexpr bool
        long_help_exists_over(const std::optional<std::string_view> &long_about,
                              const std::optional<std::string_view> &before_long_help,
                              const std::optional<std::string_view> &after_long_help,
                              Args &&args) noexcept {
            if (long_about.has_value() || before_long_help.has_value() ||
                after_long_help.has_value())
                return true;
            return std::ranges::any_of(
                args, [](const auto &candidate) { return arg_has_long_form(candidate); });
        }

        /** \brief Proof that clapp::arg_flags is a structural type. */
        using arg_flags_is_structural = arg_flags_probe<arg_flags{}>;
        /** \brief Proof that clapp::arg_predicate is a structural type. */
        using arg_predicate_is_structural = arg_predicate_probe<arg_predicate{}>;
        /** \brief Proof that clapp::alias_spec is a structural type. */
        using alias_spec_is_structural = alias_spec_probe<alias_spec{}>;
        /** \brief Proof that clapp::short_alias_spec is a structural type. */
        using short_alias_spec_is_structural = short_alias_spec_probe<short_alias_spec{}>;
        /** \brief Proof that clapp::requires_spec is a structural type. */
        using requires_spec_is_structural = requires_spec_probe<requires_spec{}>;
        /** \brief Proof that clapp::required_if_spec is a structural type. */
        using required_if_spec_is_structural = required_if_spec_probe<required_if_spec{}>;
        /** \brief Proof that clapp::default_value_spec is a structural type. */
        using default_value_spec_is_structural = default_value_spec_probe<default_value_spec{}>;
        /** \brief Proof that clapp::arg_spec is a structural type. */
        using arg_spec_is_structural = arg_spec_probe<arg_spec{}>;

        /**
         * The bit layout is a wire format in all but name: clapp::arg_flags::bits is
         * compared against clap's `ArgFlags` during porting, and a reordered
         * clapp::arg_setting would make that comparison silently wrong.
         */
        static_assert(arg_flags::bit_of(arg_setting::required) == 1u);
        static_assert(arg_flags::bit_of(arg_setting::exclusive) == 65'536U);
        static_assert(all_arg_settings.size() == arg_setting_count);

        /**
         * A default-constructed clapp::arg_spec must already be the frozen form of a
         * bare `arg_builder("x")`: positional, one value, `set`. Anything else means a
         * hand-written spec and a frozen one disagree about what "unset" means.
         */
        static_assert(arg_spec{}.get_action() == arg_action::set);
        static_assert(arg_spec{}.get_num_args() == value_range::single());
        static_assert(arg_spec{}.is_positional());
        static_assert(!arg_spec{}.is_required_set());
    } // namespace detail
} // namespace clapp
