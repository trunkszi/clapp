/**
 * \file
 * \brief Compile-time field-shape and storage deduction.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/detail/std_meta.hpp>
#include <clapp/meta/annotations.hpp>

// `template_of(t) == ^^std::vector` needs the template to be *declared*, so the
// shape classifier has to include every container it recognizes. There is no
// forward-declaration shortcut: the standard forbids declaring these templates
// outside their own headers, and matching on `identifier_of(template_of(t))`
// instead would collapse a user's `mylib::deque` onto `std::deque`.
#include <array>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace clapp {

    // =========================================================================
    // The rows of the deduction table
    // =========================================================================

    /**
     * \brief Which deduction rule a field matches.
     * \note Non-argument rows first so `deduced_arg{}` is inert (skipped), not a flag.
     */
    enum class field_shape : unsigned char {
        /** Any type carrying `[[= clapp::skip{}]]`. Not parsed; value-initialized. */
        skipped,
        /** A nested struct carrying `[[= clapp::flatten{}]]`. Its fields are spliced in. */
        flattened,
        /** `std::variant<...>` — the command's subcommand set. Required: one must be chosen. */
        subcommand_set,
        /** `std::optional<std::variant<...>>` — the subcommand may be omitted. */
        optional_subcommand_set,

        /** `bool` — a flag. `set_true`, 0 values, not required. */
        flag,
        /** `std::optional<bool>` — a flag that can tell "absent" from "explicitly false". */
        optional_flag,
        /** An integer field whose annotation says `.act = clapp::action::count`; `-vvv` is 3. */
        counter,
        /** A plain parsable scalar. `set`, 1 value, **required** unless it has a default. */
        scalar,
        /** `std::optional<T>` — `set`, 1 value, never required; absent leaves `nullopt`. */
        optional_scalar,
        /**
         * `std::optional<std::optional<T>>` — the three-state form: absent / present
         * without a value / present with one. `set`, `0..=1`, not required.
         */
        optional_optional_scalar,
        /** `std::vector<T>` or `std::deque<T>` — `append`, `1..`, not required. */
        sequence,
        /**
         * `std::optional<std::vector<T>>` — as field_shape::sequence, but absent is distinguishable
         * from an empty container.
         */
        optional_sequence,
        /** `std::array<T, N>` — `set`, exactly `N` values, required unless defaulted. */
        fixed_array,
        /** `std::pair<A, B>` — `set`, exactly 2 values, required unless defaulted. */
        pair,
        /**
         * An enumeration. `set`, 1 value, required unless defaulted, and its
         * possible values come from `enumerators_of` with no opt-in — see
         * enumerates_values().
         */
        enumeration,
    };

    /**
     * \brief How a parsed value is written back into the field.
     *
     * Coarser than `field_shape` (scalar and enum both store as `single`).
     * \note Function of the field *type* alone; `.act`/`.num_args` cannot change it.
     *       `.act = count` is a shape (`counter`), not a store override.
     */
    enum class store_kind : unsigned char {
        /** Not written from the matches at all (clapp::field_shape::skipped). */
        none,
        /** Recurse into the nested struct's own fields (clapp::field_shape::flattened). */
        nested,
        /** Construct the chosen `std::variant` alternative. */
        subcommand,
        /** As store_kind::subcommand, but leave `nullopt` when no subcommand was given. */
        optional_subcommand,
        /** Assign a `bool`. */
        flag,
        /** Assign a `std::optional<bool>`; absent stays `nullopt`. */
        optional_flag,
        /** Assign the occurrence count to an integer field. */
        count,
        /** Parse one value and assign it. */
        single,
        /** Parse one value into `std::optional<T>`; absent stays `nullopt`. */
        optional_single,
        /** Parse zero or one value into `std::optional<std::optional<T>>`. */
        optional_optional_single,
        /** Parse every value into a `std::vector` / `std::deque`. */
        many,
        /** As store_kind::many, into `std::optional<std::vector<T>>`; absent stays `nullopt`. */
        optional_many,
        /** Parse exactly `deduced_arg::arity` values into a `std::array`. */
        fixed,
        /** Parse exactly two values, one per `std::pair` element type. */
        pair,
    };

    /**
     * \brief Whether \p shape produces a command-line argument.
     * \return `false` for structural rows (skipped, flattened, subcommands).
     * \note Structural rows leave `act`/`num_args` at `infer` (static_assert below).
     */
    [[nodiscard]] constexpr bool is_argument(field_shape shape) noexcept {
        switch (shape) {
        case field_shape::skipped:
        case field_shape::flattened:
        case field_shape::subcommand_set:
        case field_shape::optional_subcommand_set:
            return false;
        case field_shape::flag:
        case field_shape::optional_flag:
        case field_shape::counter:
        case field_shape::scalar:
        case field_shape::optional_scalar:
        case field_shape::optional_optional_scalar:
        case field_shape::sequence:
        case field_shape::optional_sequence:
        case field_shape::fixed_array:
        case field_shape::pair:
        case field_shape::enumeration:
            return true;
        }
        return false;
    }

    /**
     * \brief Whether \p shape supplies its own possible-value list.
     * \return `true` only for `field_shape::enumeration`.
     * \note Replaces clap's `ValueEnum` opt-in via `enumerators_of`.
     */
    [[nodiscard]] constexpr bool enumerates_values(field_shape shape) noexcept {
        return shape == field_shape::enumeration;
    }

    /**
     * \brief The store-back strategy implied by \p shape.
     *
     * \return The clapp::store_kind for that row; store_kind::none only for
     *         clapp::field_shape::skipped.
     */
    [[nodiscard]] constexpr store_kind store_for(field_shape shape) noexcept {
        switch (shape) {
        case field_shape::skipped:
            return store_kind::none;
        case field_shape::flattened:
            return store_kind::nested;
        case field_shape::subcommand_set:
            return store_kind::subcommand;
        case field_shape::optional_subcommand_set:
            return store_kind::optional_subcommand;
        case field_shape::flag:
            return store_kind::flag;
        case field_shape::optional_flag:
            return store_kind::optional_flag;
        case field_shape::counter:
            return store_kind::count;
        case field_shape::scalar:
        case field_shape::enumeration:
            return store_kind::single;
        case field_shape::optional_scalar:
            return store_kind::optional_single;
        case field_shape::optional_optional_scalar:
            return store_kind::optional_optional_single;
        case field_shape::sequence:
            return store_kind::many;
        case field_shape::optional_sequence:
            return store_kind::optional_many;
        case field_shape::fixed_array:
            return store_kind::fixed;
        case field_shape::pair:
            return store_kind::pair;
        }
        return store_kind::none;
    }

    /**
     * \brief The kebab-cased spelling of \p shape, for compile-time diagnostics.
     * \return A view into a string literal, valid for the lifetime of the program;
     *         empty only if an enumerator was added without extending this switch.
     */
    [[nodiscard]] constexpr std::string_view name_of(field_shape shape) noexcept {
        switch (shape) {
        case field_shape::skipped:
            return "skipped";
        case field_shape::flattened:
            return "flattened";
        case field_shape::subcommand_set:
            return "subcommand-set";
        case field_shape::optional_subcommand_set:
            return "optional-subcommand-set";
        case field_shape::flag:
            return "flag";
        case field_shape::optional_flag:
            return "optional-flag";
        case field_shape::counter:
            return "counter";
        case field_shape::scalar:
            return "scalar";
        case field_shape::optional_scalar:
            return "optional-scalar";
        case field_shape::optional_optional_scalar:
            return "optional-optional-scalar";
        case field_shape::sequence:
            return "sequence";
        case field_shape::optional_sequence:
            return "optional-sequence";
        case field_shape::fixed_array:
            return "fixed-array";
        case field_shape::pair:
            return "pair";
        case field_shape::enumeration:
            return "enumeration";
        }
        return {};
    }

    /**
     * \brief The kebab-cased spelling of \p store, for compile-time diagnostics.
     * \return A view into a string literal; empty only if an enumerator was added
     *         without extending this switch.
     */
    [[nodiscard]] constexpr std::string_view name_of(store_kind store) noexcept {
        switch (store) {
        case store_kind::none:
            return "none";
        case store_kind::nested:
            return "nested";
        case store_kind::subcommand:
            return "subcommand";
        case store_kind::optional_subcommand:
            return "optional-subcommand";
        case store_kind::flag:
            return "flag";
        case store_kind::optional_flag:
            return "optional-flag";
        case store_kind::count:
            return "count";
        case store_kind::single:
            return "single";
        case store_kind::optional_single:
            return "optional-single";
        case store_kind::optional_optional_single:
            return "optional-optional-single";
        case store_kind::many:
            return "many";
        case store_kind::optional_many:
            return "optional-many";
        case store_kind::fixed:
            return "fixed";
        case store_kind::pair:
            return "pair";
        }
        return {};
    }

    // =========================================================================
    // The deduction result
    // =========================================================================

    /**
     * \brief Everything the deduction table decides about one field.
     *
     * Structural type (public members) for `define_static_array` / annotations.
     * \note Member order packs to 32 bytes; reordering breaks designated initializers.
     * \warning When `is_argument()` is false, #act and #num_args stay at `infer` —
     *          not zeros. Check `is_argument`/`is_resolved` before the builder:
     *          `value_range::infer()` has `takes_values() == false`, so skipping the
     *          check silently turns a subcommand set into a flag.
     */
    struct deduced_arg {
        /**
         * \brief How many values one occurrence takes, after applying any override.
         * `value_range::infer()` when `is_argument(shape)` is `false`.
         */
        value_range num_args = value_range::infer();

        /**
         * \brief Type-fixed element count (`N` for array, 2 for pair, else 0).
         * \note Not derived from #num_args; store-back needs the real capacity.
         */
        std::size_t arity = 0;

        /** \brief Which row of the deduction table this field landed on. */
        field_shape shape = field_shape::skipped;

        /** \brief How a parsed value is written back. Always `store_for(shape)`. */
        store_kind store = store_kind::none;

        /**
         * \brief What happens when the argument matches, after applying any override.
         * `action::infer` when `is_argument(shape)` is `false`.
         */
        arg_action act = arg_action::infer;

        /** \brief Whether the argument must be supplied. */
        bool required = false;

        /** \brief Whether this field becomes a command-line argument. */
        [[nodiscard]] constexpr bool is_argument() const noexcept {
            return clapp::is_argument(shape);
        }

        /**
         * \brief Whether both #act and #num_args are non-infer.
         * \note Matches `is_argument()` for anything `deduce()` produced.
         */
        [[nodiscard]] constexpr bool is_resolved() const noexcept {
            return clapp::is_resolved(act) && !num_args.is_infer();
        }

        /** \brief Equality of every field. */
        [[nodiscard]] constexpr bool operator==(const deduced_arg&) const noexcept = default;
    };

    namespace meta {

        // =====================================================================
        // Type-shape detection
        // =====================================================================

        /**
         * \brief Strip aliases and top-level cv from a type reflection.
         * \param type Typically `type_of(member)`.
         * \return Type with `using` and top-level cv removed.
         * \warning **Every reflective query here goes through this first** (trap 3).
         *          On raw `using sub = std::variant<a,b>`: GCC reports
         *          `has_template_arguments` true but `template_arguments_of` throws;
         *          clang reports not a specialization. Uncanonicalized aliases
         *          classify as scalar or fail the build.
         * \note Calling this is load-bearing; `return type` fails many tests. The
         *       `dealias` steps state intent; `remove_cv` alone also works.
         */
        [[nodiscard]] consteval std::meta::info canonical_type(std::meta::info type) {
            return std::meta::dealias(std::meta::remove_cv(std::meta::dealias(type)));
        }

        /**
         * \brief Whether \p type is a specialization of class template \p tmpl.
         * \param type Type reflection (aliases/cv stripped first).
         * \param tmpl Class template, e.g. `^^std::optional`.
         * \return `true` when \p type is `tmpl<...>`.
         * \note `is_type` guard returns false instead of throwing on non-types.
         */
        [[nodiscard]] consteval bool is_specialization_of(std::meta::info type,
                                                          std::meta::info tmpl) {
            if (!std::meta::is_type(type)) return false;
            const std::meta::info canonical = canonical_type(type);
            return std::meta::has_template_arguments(canonical) &&
                   std::meta::template_of(canonical) == tmpl;
        }

        /** \brief Whether \p type is `std::optional<...>`. */
        [[nodiscard]] consteval bool is_optional(std::meta::info type) {
            return is_specialization_of(type, ^^std::optional);
        }

        /** \brief Whether \p type is `std::vector<...>`. */
        [[nodiscard]] consteval bool is_vector(std::meta::info type) {
            return is_specialization_of(type, ^^std::vector);
        }

        /** \brief Whether \p type is `std::deque<...>`. */
        [[nodiscard]] consteval bool is_deque(std::meta::info type) {
            return is_specialization_of(type, ^^std::deque);
        }

        /**
         * \brief Whether \p type is one of the growable sequences clapp appends into.
         * \return `true` for `std::vector<...>` and `std::deque<...>`, the two
         *         containers named in the deduction table.
         */
        [[nodiscard]] consteval bool is_sequence(std::meta::info type) {
            return is_vector(type) || is_deque(type);
        }

        /**
         * \brief Whether \p type is `std::array<T, N>`.
         * \note Not `std::meta::is_array_type`, which answers for the built-in `T[N]`.
         */
        [[nodiscard]] consteval bool is_fixed_array(std::meta::info type) {
            return is_specialization_of(type, ^^std::array);
        }

        /** \brief Whether \p type is `std::pair<A, B>`. */
        [[nodiscard]] consteval bool is_pair(std::meta::info type) {
            return is_specialization_of(type, ^^std::pair);
        }

        /** \brief Whether \p type is `std::variant<...>`. */
        [[nodiscard]] consteval bool is_variant(std::meta::info type) {
            return is_specialization_of(type, ^^std::variant);
        }

        /** \brief Whether \p type is exactly `bool`, ignoring aliases and cv. */
        [[nodiscard]] consteval bool is_boolean(std::meta::info type) {
            return std::meta::is_type(type) && canonical_type(type) == ^^bool;
        }

        /** \brief Whether \p type is an enumeration, scoped or not. */
        [[nodiscard]] consteval bool is_enumeration(std::meta::info type) {
            return std::meta::is_type(type) && std::meta::is_enum_type(canonical_type(type));
        }

        /**
         * \brief Whether \p type can accumulate an occurrence count.
         * \return `true` for every integral type except `bool`.
         * \note Character types included: `count_type` is `uint8_t` / `unsigned char`.
         */
        [[nodiscard]] consteval bool is_countable(std::meta::info type) {
            if (!std::meta::is_type(type)) return false;
            const std::meta::info canonical = canonical_type(type);
            return std::meta::is_integral_type(canonical) && canonical != ^^bool;
        }

        /**
         * \brief First type argument of `optional`/`vector`/`deque`/`array`.
         * \param type Specialization whose first argument is the element type.
         * \return Canonicalized argument reflection.
         * \warning A non-specialization aborts constant evaluation.
         */
        [[nodiscard]] consteval std::meta::info sole_type_argument(std::meta::info type) {
            const std::meta::info canonical = canonical_type(type);
            if (!std::meta::has_template_arguments(canonical)) std::abort();
            return canonical_type(std::meta::template_arguments_of(canonical)[0]);
        }

        /**
         * \brief Extent `N` of `std::array<T, N>`.
         * \param type Reflection of a `std::array` specialization.
         * \return Second template argument as `std::size_t`.
         * \warning A non-array aborts constant evaluation.
         * \note Constant argument; use `extract`, not `type_of`.
         */
        [[nodiscard]] consteval std::size_t array_extent_of(std::meta::info type) {
            if (!is_fixed_array(type)) std::abort();
            return std::meta::extract<std::size_t>(
                    std::meta::template_arguments_of(canonical_type(type))[1]);
        }

        /**
         * \brief Element types of `std::pair<A, B>`, both canonicalized.
         * \param type Reflection of a `std::pair` specialization.
         * \warning A non-pair aborts constant evaluation.
         */
        [[nodiscard]] consteval std::array<std::meta::info, 2> pair_types_of(std::meta::info type) {
            if (!is_pair(type)) std::abort();
            const auto args = std::meta::template_arguments_of(canonical_type(type));
            return {canonical_type(args[0]), canonical_type(args[1])};
        }

        // =====================================================================
        // Classification
        // =====================================================================

        /**
         * \brief Deduction-table row for \p type alone (no annotations).
         * \param type Field type reflection.
         * \return Never skipped/flattened/counter (those need annotations).
         * \note Bare `std::variant` is a subcommand set even without the marker
         *       Nested `vector` is a sequence and
         *       fails later with no `value_parser` (clap has `value_delimiter`).
         */
        [[nodiscard]] consteval field_shape shape_of_type(std::meta::info type) {
            const std::meta::info canonical = canonical_type(type);

            if (canonical == ^^bool) return field_shape::flag;
            if (std::meta::is_enum_type(canonical)) return field_shape::enumeration;

            if (is_optional(canonical)) {
                const std::meta::info inner = sole_type_argument(canonical);
                if (inner == ^^bool) return field_shape::optional_flag;
                if (is_optional(inner)) return field_shape::optional_optional_scalar;
                if (is_sequence(inner)) return field_shape::optional_sequence;
                if (is_variant(inner)) return field_shape::optional_subcommand_set;
                return field_shape::optional_scalar;
            }

            if (is_sequence(canonical)) return field_shape::sequence;
            if (is_fixed_array(canonical)) return field_shape::fixed_array;
            if (is_pair(canonical)) return field_shape::pair;
            if (is_variant(canonical)) return field_shape::subcommand_set;

            return field_shape::scalar;
        }

        /**
         * \brief Deduction-table row for a data member.
         *
         * Precedence: `skip` > `flatten` > type. `subcommand` only validates.
         * \param member Non-static data member reflection.
         * \return Includes annotation-dependent skipped/flattened/counter.
         * \warning Invalid `[[= subcommand{}]]` placement aborts constant evaluation.
         * \note Counter needs `.act = count` on a countable type. `from_global` and
         *       `external_subcommand` do not change shape (handled above).
         */
        [[nodiscard]] consteval field_shape shape_of(std::meta::info member) {
            if (has_annotation<skip>(member)) return field_shape::skipped;
            if (has_annotation<flatten>(member)) return field_shape::flattened;

            const std::meta::info type = std::meta::type_of(member);
            const field_shape shape    = shape_of_type(type);

            if (has_annotation<subcommand>(member) && shape != field_shape::subcommand_set &&
                shape != field_shape::optional_subcommand_set) {
                std::abort();
            }

            if (shape == field_shape::scalar && is_countable(type) &&
                annotation_or<arg_attr>(member).act == action::count) {
                return field_shape::counter;
            }
            return shape;
        }

        /**
         * \brief Whether \p member has a default member initializer.
         * \param member Non-static data member reflection.
         * \return `true` for `= 8080` / `{}`, `false` for bare `int plain;`.
         * \note Only presence matters for `required`; help-text defaults are separate.
         *       Implemented on both GCC 16 and clang-p2996.
         */
        [[nodiscard]] consteval bool has_member_initializer(std::meta::info member) {
            return std::meta::is_nonstatic_data_member(member) &&
                   std::meta::has_default_member_initializer(member);
        }

        // =====================================================================
        // The table itself
        // =====================================================================

        /**
         * \brief Apply the deduction table (reflection-free half).
         *
         * Explicit non-infer `attr.act` / `num_args_override` / `attr.required` win;
         * else the row. Action without arity uses the action's arity (`.act =
         * count` -> 0 values). `required` needs the row, no default, and values.
         * \param shape From `shape_of` / `shape_of_type`.
         * \param arity Type-fixed count (`N` for array; pair -> 2).
         * \param attr Field `arg_attr`, or default if none.
         * \param has_member_initializer See `has_member_initializer()`.
         * \param num_args_override Explicit arity, or `infer()`.
         * \return Fully resolved `deduced_arg`.
         * \warning **Arity override is not in the annotation DSL yet** — `arg_attr`
         *          has no `num_args`; `deduce_member` always passes `infer()`.
         *          Explicit-wins holds for `.act`/`.required`, not arity, for now.
         * \note Defaults from annotation or member initializer clear `required`.
         * \warning `cstr` cannot tell absent from empty: `.default_value = ""`
         *          does **not** clear `required`. Use a member initializer instead.
         */
        [[nodiscard]] consteval deduced_arg
        deduce(field_shape shape,
               std::size_t arity,
               const arg_attr& attr,
               bool has_member_initializer,
               value_range num_args_override = value_range::infer()) {
            deduced_arg result{};
            result.shape = shape;
            result.store = store_for(shape);
            result.arity = shape == field_shape::pair          ? std::size_t{2}
                           : shape == field_shape::fixed_array ? arity
                                                               : std::size_t{0};

            if (!clapp::is_argument(shape)) {
                // The four structural rows produce no argument, so both sentinels stay
                // unresolved rather than being given a meaningless zero. Only
                // `required` carries information here, and only for the subcommand
                // rows, where it is clap's `subcommand_required`.
                result.act      = arg_action::infer;
                result.num_args = value_range::infer();
                result.required = shape == field_shape::subcommand_set;
                if (attr.required != tri::infer) result.required = attr.required == tri::yes;
                return result;
            }

            arg_action row_act    = arg_action::set;
            value_range row_range = value_range::single();
            bool row_required     = false;

            switch (shape) {
            case field_shape::flag:
            case field_shape::optional_flag:
                row_act   = arg_action::set_true;
                row_range = value_range::empty();
                break;
            case field_shape::counter:
                row_act   = arg_action::count;
                row_range = value_range::empty();
                break;
            case field_shape::scalar:
            case field_shape::enumeration:
                row_act      = arg_action::set;
                row_range    = value_range::single();
                row_required = true;
                break;
            case field_shape::optional_scalar:
                row_act   = arg_action::set;
                row_range = value_range::single();
                break;
            case field_shape::optional_optional_scalar:
                row_act   = arg_action::set;
                row_range = value_range::optional();  // 0..=1
                break;
            case field_shape::sequence:
            case field_shape::optional_sequence:
                row_act   = arg_action::append;
                row_range = value_range::at_least(1);
                break;
            case field_shape::fixed_array:
                row_act      = arg_action::set;
                row_range    = value_range::exactly(result.arity);
                row_required = true;
                break;
            case field_shape::pair:
                row_act      = arg_action::set;
                row_range    = value_range::exactly(2);
                row_required = true;
                break;
            case field_shape::skipped:
            case field_shape::flattened:
            case field_shape::subcommand_set:
            case field_shape::optional_subcommand_set:
                break;  // unreachable: filtered by is_argument() above
            }

            const bool act_given = attr.act != action::infer;
            result.act           = act_given ? attr.act : row_act;

            if (!num_args_override.is_infer()) {
                result.num_args = num_args_override;
            } else if (act_given) {
                result.num_args = default_num_args(attr.act);
            } else {
                result.num_args = row_range;
            }

            const bool has_default = has_member_initializer || !attr.default_value.empty();

            if (attr.required != tri::infer) {
                result.required = attr.required == tri::yes;
            } else {
                result.required = row_required && !has_default &&
                                  takes_values(result.act) == tri::yes &&
                                  result.num_args.takes_values();
            }
            return result;
        }

        /**
         * \brief Apply the deduction table to a data member.
         * \param member Non-static data member reflection.
         * \return Fully resolved `deduced_arg`.
         * \throws Whatever `shape_of` throws.
         * \note No arity override — `arg_attr` has no `num_args` yet (see `deduce`).
         */
        [[nodiscard]] consteval deduced_arg deduce_member(std::meta::info member) {
            const field_shape shape    = shape_of(member);
            const std::meta::info type = std::meta::type_of(member);

            const std::size_t arity =
                    shape == field_shape::fixed_array ? array_extent_of(type) : std::size_t{0};

            return deduce(
                    shape, arity, annotation_or<arg_attr>(member), has_member_initializer(member));
        }

        /**
         * \brief Scalar parse type for one token (peels optional/sequence/array).
         * \param type Field type reflection.
         * \return Canonicalized scalar value type.
         * \throws For pair (use `pair_types_of`) and variant (no value type).
         * \warning Branch on pair/subcommand_set first; throws are compile errors.
         */
        [[nodiscard]] consteval std::meta::info value_type_of(std::meta::info type) {
            const field_shape shape = shape_of_type(type);
            if (shape == field_shape::flag || shape == field_shape::scalar ||
                shape == field_shape::enumeration)
                return canonical_type(type);
            if (shape == field_shape::optional_flag || shape == field_shape::optional_scalar ||
                shape == field_shape::sequence || shape == field_shape::fixed_array)
                return sole_type_argument(type);
            if (shape == field_shape::optional_optional_scalar ||
                shape == field_shape::optional_sequence)
                return sole_type_argument(sole_type_argument(type));
            if (shape == field_shape::pair) std::abort();
            if (shape == field_shape::subcommand_set ||
                shape == field_shape::optional_subcommand_set)
                std::abort();
            return canonical_type(type);
        }

    }  // namespace meta

    namespace detail {

        // ---------------------------------------------------------------------
        // Compile-time contracts. Each of these is a check that a plausible edit
        // would otherwise break silently.
        // ---------------------------------------------------------------------

        /** `deduced_arg` must stay structural for `define_static_array`. */
        static_assert(std::meta::is_structural_type(^^deduced_arg),
                      "clapp: deduced_arg must remain a structural type.");

        /** Value-init must be skipped/inert, not a flag (enumerator-zero trap). */
        static_assert(deduced_arg{}.shape == field_shape::skipped &&
                              deduced_arg{}.store == store_kind::none &&
                              !deduced_arg{}.is_argument() && !deduced_arg{}.is_resolved(),
                      "clapp: a value-initialized deduced_arg must describe a skipped "
                      "field, not an argument.");

        /** Every `field_shape` handled; structural rows leave both sentinels open. */
        [[nodiscard]] consteval bool deduction_table_is_total() {
            for (const std::meta::info enumerator : std::meta::enumerators_of(^^field_shape)) {
                const auto shape = std::meta::extract<field_shape>(enumerator);

                if (name_of(shape).empty()) return false;
                if (name_of(store_for(shape)).empty()) return false;

                const deduced_arg deduced = meta::deduce(shape, 3, arg_attr{}, false);
                if (deduced.shape != shape) return false;
                if (deduced.store != store_for(shape)) return false;
                if (deduced.is_argument() != clapp::is_argument(shape)) return false;

                // The invariant the \warning on deduced_arg rests on.
                if (deduced.is_argument() != deduced.is_resolved()) return false;

                // store_kind::none belongs to `skipped` alone; every other row has a
                // way to write its value back.
                if ((store_for(shape) == store_kind::none) != (shape == field_shape::skipped)) {
                    return false;
                }
            }
            return true;
        }

        static_assert(deduction_table_is_total(),
                      "clapp: a clapp::field_shape enumerator is missing from one of the "
                      "switches in deduce.hpp, or a structural row leaked a resolved "
                      "action / value_range.");

        /** Every `store_kind` must be produced by some `field_shape`. */
        [[nodiscard]] consteval bool every_store_kind_is_reachable() {
            for (const std::meta::info store_enumerator : std::meta::enumerators_of(^^store_kind)) {
                const auto store = std::meta::extract<store_kind>(store_enumerator);

                bool reached = false;
                for (const std::meta::info shape_enumerator :
                     std::meta::enumerators_of(^^field_shape)) {
                    if (store_for(std::meta::extract<field_shape>(shape_enumerator)) == store) {
                        reached = true;
                    }
                }
                if (!reached) return false;
            }
            return true;
        }

        static_assert(every_store_kind_is_reachable(),
                      "clapp: every clapp::store_kind must be produced by store_for() for "
                      "at least one clapp::field_shape.");

    }  // namespace detail

}  // namespace clapp
