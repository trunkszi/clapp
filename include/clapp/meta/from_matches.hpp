/**
 * \file
 * \brief from_matches / update_from_matches — run-time mirror of command_of<T>().
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/detail/std_meta.hpp>
#include <clapp/error/context.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/meta/deduce.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/str.hpp>

#include <cstddef>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace clapp {

    namespace meta {

        // =====================================================================
        // Shared identity — the one place either direction may ask
        // =====================================================================

        /**
         * \brief Argument id for a data member (field name, not long option).
         *
         * clap_derive's `Item::id()`: `no_color` is the id, `--no-color` the spelling.
         * \param member Non-static data member reflection.
         * \return View into static storage.
         * \warning **Both directions must call this.** A private id in `command_of`
         *          is a silent data-loss bug, not a build failure.
         * \note Trailing `_` stripped (`type_` -> `type`); lifted with
         *       `define_static_string` (unlifted rename string dangles).
         * \warning **Do not pass this view straight to arg/command builders under
         *          ubsan.** libstdc++ `string(ptr, n)` null-tests the source; under
         *          `-fsanitize=null` GCC will not fold that when the base is a
         *          variable (`define_static_string` is one). String literals and
         *          push_back-built strings fold; promoted views do not. Diagnostic
         *          points into `basic_string.h`. Use `append_bytes` first (trap 10).
         */
        [[nodiscard]] consteval std::string_view arg_id_of(std::meta::info member) {
            if (!std::meta::is_nonstatic_data_member(member)) std::abort();
            return std::string_view(std::define_static_string(
                    clapp::rename(std::meta::identifier_of(member), naming::verbatim)));
        }

        /**
         * \brief `arg_id_of` with enclosing type checked (`parent_of` must be \p T).
         * \tparam T Class the member must belong to.
         * \tparam Member One of `T`'s data members.
         * \return The id, in static storage.
         */
        template<class T, std::meta::info Member>
        [[nodiscard]] consteval std::string_view arg_id_of() {
            if (std::meta::dealias(std::meta::parent_of(Member)) != std::meta::dealias(^^T))
                std::abort();
            return arg_id_of(Member);
        }

        /**
         * \brief Command-line name of a variant alternative.
         * \param type Alternative type reflection.
         * \param style Enclosing `rename_all`.
         * \return View into static storage.
         * \note Prefers `[[= cmd{.name}]]`, else affix-stripped rename. Shared by both
         *       directions.
         * \warning Type annotations must follow `struct`
         *          (`struct [[= cmd{...}]] T`); prefix form compiles with a warning
         *          and silently falls back to the type name (trap 1).
         */
        [[nodiscard]] consteval std::string_view subcommand_name_of(std::meta::info type,
                                                                    naming style = naming::kebab) {
            const command_attr attr = annotation_or<command_attr>(type);
            if (!attr.name.empty()) {
                return std::string_view(std::define_static_string(attr.name.view()));
            }
            return std::string_view(std::define_static_string(
                    clapp::subcommand_name(std::meta::identifier_of(type), style)));
        }

        /**
         * \brief The clapp::naming a command applies to the names it derives.
         * \param type Reflection of the command struct.
         * \return `command_attr::rename_all`, or clapp::naming::kebab when the type
         *         carries no clapp::command_attr.
         */
        [[nodiscard]] consteval naming rename_all_of(std::meta::info type) {
            return annotation_or<command_attr>(type).rename_all;
        }

    }  // namespace meta

    /**
     * \brief Type requirements for `from_matches` (class, aggregate, default-ctor).
     * \note Aggregate so `access_context::current()` sees public members only.
     */
    template<class T>
    concept derivable_command =
            std::is_class_v<T> && std::is_aggregate_v<T> && std::is_default_constructible_v<T>;

    namespace detail {

        // =====================================================================
        // Errors
        // =====================================================================

        /**
         * \brief Wrap a `matches_error` as a `clapp::error`.
         * \param id Argument id.
         * \param why Access violation.
         * \return `unknown_argument` or `invalid_value`.
         * \note Uses try-accessors, not aborting ones, so mismatched command trees
         *       yield a printable error instead of `abort`.
         */
        [[nodiscard]] inline error matches_access_error(std::string_view id,
                                                        const matches_error& why) {
            std::string message;
            append_bytes(message, "clapp: cannot read argument `");
            append_bytes(message, id);
            append_bytes(message, "` out of the matches: ");
            append_bytes(message, why.to_string());
            const error_kind kind = why.kind() == matches_error_kind::downcast
                                            ? error_kind::invalid_value
                                            : error_kind::unknown_argument;
            return error::raw(kind, cow_str{std::move(message)});
        }

        /**
         * \brief clap_derive's "the following required argument was not provided".
         * \param id The field's argument id.
         */
        [[nodiscard]] inline error missing_required_error(std::string_view id) {
            std::string message;
            append_bytes(message, "the following required argument was not provided: ");
            append_bytes(message, id);
            return error::raw(error_kind::missing_required_argument, cow_str{std::move(message)});
        }

        /** \brief clap_derive's "a subcommand is required but one was not provided". */
        [[nodiscard]] inline error missing_subcommand_error() {
            std::string message;
            append_bytes(message, "a subcommand is required but one was not provided");
            return error::raw(error_kind::missing_subcommand, cow_str{std::move(message)});
        }

        /**
         * \brief clap_derive's "the subcommand '...' wasn't recognized".
         * \param name The subcommand the parse reported.
         *
         * \note Reachable only when the clapp::command_spec has a subcommand that the
         *       `std::variant` does not, i.e. when the two directions have drifted.
         */
        [[nodiscard]] inline error unrecognized_subcommand_error(std::string_view name) {
            std::string message;
            append_bytes(message, "the subcommand '");
            append_bytes(message, name);
            append_bytes(message, "' wasn't recognized");
            return error::raw(error_kind::invalid_subcommand, cow_str{std::move(message)});
        }

        /**
         * \brief A `std::array<T, N>` or `std::pair<A, B>` got the wrong value count.
         * \param id       The field's argument id.
         * \param expected The field type's fixed arity.
         * \param actual   How many values the matches hold.
         */
        [[nodiscard]] inline error
        arity_error(std::string_view id, std::size_t expected, std::size_t actual) {
            return error::wrong_number_of_values(
                    cow_str::owned(id), expected, actual, std::nullopt);
        }

        /**
         * \brief A clapp::value_parser rejected one element of a `std::pair`.
         * \param id    The field's argument id.
         * \param raw   The bytes that failed.
         * \param why   The parser's report.
         */
        [[nodiscard]] inline error
        element_parse_error(std::string_view id, os_str raw, const parse_error& why) {
            return error::value_validation(
                    cow_str::owned(id), cow_str::owned(raw.chars()), cow_str::owned(why.message()));
        }

        // =====================================================================
        // Reading
        // =====================================================================

        /**
         * \brief Whether a recorded value may overwrite this field.
         * \param matches Matches being read.
         * \param id Field argument id.
         * \param initializer_is_the_only_default Member initializer is the sole default
         *        (no contradicting `.default_value` annotation).
         * \return `false` only for parser `default_value` into an initializer-only field.
         * \note Deliberate clap divergence: member initializers are not readable into
         *       matches. Parser defaults yield so `bool verbose = true` stays true;
         *       explicit CLI/env always win. Annotation `.default_value` outranks the
         *       initializer (see `arg_attr::default_value` \warning).
         */
        [[nodiscard]] inline bool wins_over_initializer(const arg_matches& matches,
                                                        std::string_view id,
                                                        bool initializer_is_the_only_default) {
            if (!initializer_is_the_only_default) return true;
            const std::optional<clapp::value_source> source = matches.value_source(id);
            return !source.has_value() || clapp::is_explicit(source.value());
        }

        /**
         * \brief The first value recorded for \p id, copied out as a \p V.
         * \tparam V The type the argument's clapp::value_parser produces.
         * \return The value, `std::nullopt` when the id recorded none, or the access
         *         error.
         */
        template<class V>
            requires any_storable<V> && std::copy_constructible<V>
        [[nodiscard]] std::expected<std::optional<V>, error> read_one(const arg_matches& matches,
                                                                      std::string_view id) {
            const std::expected<std::optional<const V*>, matches_error> got =
                    matches.try_get_one<V>(id);
            if (!got.has_value()) return std::unexpected(matches_access_error(id, got.error()));
            if (!got.value().has_value()) return std::optional<V>{};
            return std::optional<V>{*got.value().value()};
        }

        /**
         * \brief Every value recorded for \p id, copied out as `V`s.
         * \tparam V The type the argument's clapp::value_parser produces.
         * \return The values (possibly empty when the id is present but recorded
         *         none), `std::nullopt` when the id is absent, or the access error.
         */
        template<class V>
            requires any_storable<V> && std::copy_constructible<V>
        [[nodiscard]] std::expected<std::optional<std::vector<V>>, error>
        read_many(const arg_matches& matches, std::string_view id) {
            const std::expected<std::optional<values_ref<V>>, matches_error> got =
                    matches.try_get_many<V>(id);
            if (!got.has_value()) return std::unexpected(matches_access_error(id, got.error()));
            if (!got.value().has_value()) return std::optional<std::vector<V>>{};

            std::vector<V> out;
            // Bound to a named variable first: values_ref is a view into `matches` and
            // iterating it out of a temporary optional is CLAUDE.md trap 12.
            const values_ref<V>& view = got.value().value();
            for (const V& one : view) out.push_back(one);
            return std::optional<std::vector<V>>{std::move(out)};
        }

        /** \brief Whether \p id is present at all, as an error rather than an abort. */
        [[nodiscard]] inline std::expected<bool, error> read_present(const arg_matches& matches,
                                                                     std::string_view id) {
            const std::expected<bool, matches_error> got = matches.try_contains_id(id);
            if (!got.has_value()) return std::unexpected(matches_access_error(id, got.error()));
            return got.value();
        }

        // =====================================================================
        // The walker
        // =====================================================================

        /** \brief Which entry point is running. */
        enum class fill_mode : unsigned char {
            construct, /**< `from_matches`: missing required is an error. */
            update,    /**< `update_from_matches`: omit keeps caller value. */
        };

        /**
         * \brief Per-field context for store-back helpers (by value).
         */
        struct fill_context {
            const arg_matches* matches = nullptr; /**< Never null (trap 10). */
            std::string_view id{};                /**< `arg_id_of` for the field. */
            fill_mode mode = fill_mode::construct;
            bool required  = false; /**< `deduced_arg::required`. */

            /**
             * \brief Member initializer is the sole default (no `.default_value`).
             * Tie-break for `wins_over_initializer`, set once in `fill_struct`.
             */
            bool initializer_is_the_only_default = false;

            std::size_t arity = 0; /**< `deduced_arg::arity`. */
        };

        // The command's clapp::naming is deliberately NOT a member of fill_context.
        // clapp::meta::subcommand_name_of() is `consteval`, so the style has to reach it
        // as a constant expression; carrying it in a run-time struct compiles right up
        // to the call and then fails with `'ctx' is not a constant expression`. It is a
        // template parameter of fill_struct() and the three subcommand helpers instead.

        // Forward declaration: the flatten and subcommand rows recurse into a whole
        // struct, and that function is defined below the helpers it dispatches to.
        //
        // \tparam Style The naming in force AT THIS LEVEL, inherited from the enclosing
        //         command rather than recomputed from `T`. See the \warning on the
        //         definition below for why the difference is load-bearing.
        template<naming Style, class T>
        [[nodiscard]] std::expected<void, error>
        fill_struct(T& out, const arg_matches& matches, fill_mode mode);

        /** \brief clapp::store_kind::flag — a plain `bool`. */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_flag(M& field, fill_context ctx) {
            const std::expected<std::optional<bool>, error> got =
                    read_one<bool>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) return {};
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};
            field = got.value().value();
            return {};
        }

        /**
         * \brief `store_kind::optional_flag` — `std::optional<bool>`.
         * \note Engage only for explicit sources; parser default `false` must not.
         */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_optional_flag(M& field, fill_context ctx) {
            const std::optional<clapp::value_source> source = ctx.matches->value_source(ctx.id);
            if (!source.has_value() || !clapp::is_explicit(source.value())) return {};

            const std::expected<std::optional<bool>, error> got =
                    read_one<bool>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) return {};
            field = got.value().value();
            return {};
        }

        /**
         * \brief `store_kind::count` — occurrence count.
         * \note Reads `count_type` (`uint8_t`), not the field's integer type.
         */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_count(M& field, fill_context ctx) {
            const std::expected<std::optional<count_type>, error> got =
                    read_one<count_type>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) return {};
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};
            field = static_cast<M>(got.value().value());
            return {};
        }

        /** \brief clapp::store_kind::single — a scalar or an enumeration. */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_single(M& field, fill_context ctx) {
            using V                                          = [:meta::value_type_of(^^M):];
            const std::expected<std::optional<V>, error> got = read_one<V>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) {
                if (ctx.mode == fill_mode::construct && ctx.required) {
                    return std::unexpected(missing_required_error(ctx.id));
                }
                return {};
            }
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};
            field = got.value().value();
            return {};
        }

        /** \brief clapp::store_kind::optional_single — `std::optional<V>`. */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_optional_single(M& field, fill_context ctx) {
            using V                                          = [:meta::value_type_of(^^M):];
            const std::expected<std::optional<V>, error> got = read_one<V>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) return {};
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};
            field = got.value().value();
            return {};
        }

        /**
         * \brief `store_kind::optional_optional_single` — `optional<optional<V>>`.
         * Outer: flag present; inner: value present.
         */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_optional_optional_single(M& field,
                                                                                fill_context ctx) {
            using Inner = [:meta::sole_type_argument(^^M):];
            using V     = [:meta::value_type_of(^^M):];

            const std::expected<bool, error> present = read_present(*ctx.matches, ctx.id);
            if (!present.has_value()) return std::unexpected(present.error());
            if (!present.value()) return {};

            const std::expected<std::optional<V>, error> got = read_one<V>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};

            Inner inner{};
            if (got.value().has_value()) inner = got.value().value();
            field = std::move(inner);
            return {};
        }

        /** \brief clapp::store_kind::many — `std::vector<V>` or `std::deque<V>`. */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_many(M& field, fill_context ctx) {
            using V = [:meta::value_type_of(^^M):];
            const std::expected<std::optional<std::vector<V>>, error> got =
                    read_many<V>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) return {};
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};

            M collected{};
            for (const V& one : got.value().value()) collected.push_back(one);
            field = std::move(collected);
            return {};
        }

        /**
         * \brief `store_kind::optional_many` — `optional<vector<V>>`.
         * \note Absent vs empty differ: presence from `try_contains_id`, not value count.
         */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_optional_many(M& field, fill_context ctx) {
            using Inner = [:meta::sole_type_argument(^^M):];
            using V     = [:meta::value_type_of(^^M):];

            const std::expected<bool, error> present = read_present(*ctx.matches, ctx.id);
            if (!present.has_value()) return std::unexpected(present.error());
            if (!present.value()) return {};

            const std::expected<std::optional<std::vector<V>>, error> got =
                    read_many<V>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};

            Inner collected{};
            if (got.value().has_value()) {
                for (const V& one : got.value().value()) collected.push_back(one);
            }
            field = std::move(collected);
            return {};
        }

        /**
         * \brief `store_kind::fixed` — `std::array<V, N>`.
         * \note Count from type `arity`, not overridden `num_args`.
         */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_fixed(M& field, fill_context ctx) {
            using V = [:meta::value_type_of(^^M):];
            const std::expected<std::optional<std::vector<V>>, error> got =
                    read_many<V>(*ctx.matches, ctx.id);
            if (!got.has_value()) return std::unexpected(got.error());
            if (!got.value().has_value()) {
                if (ctx.mode == fill_mode::construct && ctx.required) {
                    return std::unexpected(missing_required_error(ctx.id));
                }
                return {};
            }

            const std::vector<V>& values = got.value().value();
            if (values.size() != ctx.arity) {
                return std::unexpected(arity_error(ctx.id, ctx.arity, values.size()));
            }
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};

            M collected{};
            for (std::size_t i = 0; i < ctx.arity; ++i) collected[i] = values[i];
            field = std::move(collected);
            return {};
        }

        /**
         * \brief `store_kind::pair` — `std::pair<A, B>`.
         * \note Uses `try_get_raw` and re-parses each side (one arg_spec, two types).
         *       Argument-level parser options (e.g. `ignore_case`) are not applied.
         */
        template<class M>
        [[nodiscard]] std::expected<void, error> store_pair(M& field, fill_context ctx) {
            using A = [:meta::pair_types_of(^^M)[0]:];
            using B = [:meta::pair_types_of(^^M)[1]:];

            const std::expected<std::optional<std::span<const os_string>>, matches_error> got =
                    ctx.matches->try_get_raw(ctx.id);
            if (!got.has_value()) {
                return std::unexpected(matches_access_error(ctx.id, got.error()));
            }
            if (!got.value().has_value()) {
                if (ctx.mode == fill_mode::construct && ctx.required) {
                    return std::unexpected(missing_required_error(ctx.id));
                }
                return {};
            }

            const std::span<const os_string> raw = got.value().value();
            if (raw.size() != 2) {
                return std::unexpected(arity_error(ctx.id, 2, raw.size()));
            }
            if (!wins_over_initializer(*ctx.matches, ctx.id, ctx.initializer_is_the_only_default))
                return {};

            const std::expected<A, parse_error> first = value_parser<A>::parse(raw[0].view());
            if (!first.has_value()) {
                return std::unexpected(element_parse_error(ctx.id, raw[0].view(), first.error()));
            }
            const std::expected<B, parse_error> second = value_parser<B>::parse(raw[1].view());
            if (!second.has_value()) {
                return std::unexpected(element_parse_error(ctx.id, raw[1].view(), second.error()));
            }

            field = M{first.value(), second.value()};
            return {};
        }

        /**
         * \brief Fill the variant alternative matching the parsed subcommand name.
         *
         * Runtime name vs compile-time type: `template for` over alternatives (not a
         * loop; no `break`).
         * \tparam Style Enclosing `rename_all` (NTTP: `subcommand_name_of` is consteval).
         * \tparam V Variant type.
         * \param out Variant to write.
         * \param name Parsed subcommand name.
         * \param sub That subcommand's matches.
         * \param ctx Only `mode` is used.
         * \return Void or first failure (`invalid_subcommand` if no match).
         * \note `matched` enables the final error; short-circuit is not load-bearing.
         *       Update mode mutates an already-held alternative in place.
         * \note Compile cost linear in alternative count (~40 ms each on GCC 16).
         */
        template<naming Style, class V>
        [[nodiscard]] std::expected<void, error>
        store_variant(V& out, std::string_view name, const arg_matches& sub, fill_context ctx) {
            std::optional<error> failure;
            bool matched = false;

            template for (constexpr std::meta::info alternative :
                          meta::variant_traits<V>::reflections) {
                if (!matched) {
                    using alternative_type = [:alternative:];
                    if (name == meta::subcommand_name_of(alternative, Style)) {
                        matched = true;
                        // The alternative opens a NEW command level, so its own
                        // `rename_all` takes over here — mirroring
                        // clapp::detail::derive_subcommands_into(), which passes
                        // `meta::rename_all_of(alternative)` into the child builder.
                        // This is the one place the inherited style is legitimately
                        // dropped; every other recursion inherits.
                        constexpr naming sub_style = meta::rename_all_of(alternative);
                        if (ctx.mode == fill_mode::update &&
                            std::holds_alternative<alternative_type>(out)) {
                            std::expected<void, error> updated = fill_struct<sub_style>(
                                    std::get<alternative_type>(out), sub, fill_mode::update);
                            if (!updated.has_value()) failure = std::move(updated.error());
                        } else {
                            alternative_type fresh{};
                            std::expected<void, error> built =
                                    fill_struct<sub_style>(fresh, sub, fill_mode::construct);
                            if (!built.has_value())
                                failure = std::move(built.error());
                            else
                                out = V(std::in_place_type<alternative_type>, std::move(fresh));
                        }
                    }
                }
            }

            if (failure.has_value()) return std::unexpected(std::move(failure.value()));
            if (!matched) return std::unexpected(unrecognized_subcommand_error(name));
            return {};
        }

        /** \brief clapp::store_kind::subcommand — a bare `std::variant<...>`. */
        template<naming Style, class M>
        [[nodiscard]] std::expected<void, error> store_subcommand(M& field, fill_context ctx) {
            // Named, not chained: subcommand() returns an optional BY VALUE and the
            // reference inside it borrows `*ctx.matches`. CLAUDE.md trap 12.
            const std::optional<std::pair<std::string_view, const arg_matches&>> chosen =
                    ctx.matches->subcommand();
            if (!chosen.has_value()) {
                if (ctx.mode == fill_mode::construct) {
                    return std::unexpected(missing_subcommand_error());
                }
                return {};
            }
            return store_variant<Style>(field, chosen.value().first, chosen.value().second, ctx);
        }

        /**
         * \brief clapp::store_kind::optional_subcommand —
         *        `std::optional<std::variant<...>>`.
         */
        template<naming Style, class M>
        [[nodiscard]] std::expected<void, error> store_optional_subcommand(M& field,
                                                                           fill_context ctx) {
            using V = [:meta::sole_type_argument(^^M):];

            const std::optional<std::pair<std::string_view, const arg_matches&>> chosen =
                    ctx.matches->subcommand();
            if (!chosen.has_value()) return {};

            V staged{};
            if (ctx.mode == fill_mode::update && field.has_value()) staged = field.value();

            std::expected<void, error> outcome =
                    store_variant<Style>(staged, chosen.value().first, chosen.value().second, ctx);
            if (!outcome.has_value()) return std::unexpected(std::move(outcome.error()));

            field = std::move(staged);
            return {};
        }

        /**
         * \brief Walk every data member of \p T and write it back.
         * \tparam Style Naming in force at this command level.
         * \tparam T Command struct.
         * \param out Object to fill (value-initialized by caller).
         * \param matches Matches for this command level.
         * \param mode Construct vs update.
         * \return Void or first failure.
         * \note `template for` is an expansion; failures set a flag, no `break`.
         * \warning **`Style` is INHERITED, never recomputed from `T`.** Flatten does
         *          not open a new level (`derive_args_into` forwards the enclosing
         *          style). Recomputing `rename_all_of(^^T)` desyncs subcommand names
         *          across a flatten that crosses a `rename_all` boundary (real bug:
         *          snake root + flattened set froze `do_thing`, reverse looked up
         *          `do-thing`). Only `store_variant` may replace `Style`.
         */
        template<naming Style, class T>
        [[nodiscard]] std::expected<void, error>
        fill_struct(T& out, const arg_matches& matches, fill_mode mode) {
            static_assert(derivable_command<T>,
                          "clapp: from_matches<T>() needs an aggregate with public data "
                          "members; std::meta::access_context::current() cannot see private "
                          "ones.");

            std::optional<error> failure;

            template for (constexpr std::meta::info member :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                                  ^^T, std::meta::access_context::current()))) {
                if (!failure.has_value()) {
                    // No `using member_type = [: type_of(member) :];` here on purpose:
                    // every store-back helper is a function template deduced from
                    // `out.[: member :]`, so naming the type would be dead weight that
                    // `-Wunused-local-typedefs` reports once per field.
                    constexpr deduced_arg deduced = meta::deduce_member(member);
                    constexpr std::string_view id = meta::arg_id_of<T, member>();

                    // Two channels can state a default; the initializer only gets the
                    // tie-break when it is the only one. Read from the same annotation
                    // clapp::detail::derive_arg() reads, so the two directions cannot
                    // disagree about which channel the user used.
                    constexpr bool wrote_default_value =
                            !meta::annotation_or<arg_attr>(member).default_value.empty();

                    const fill_context ctx{
                            .matches  = &matches,
                            .id       = id,
                            .mode     = mode,
                            .required = deduced.required,
                            .initializer_is_the_only_default =
                                    meta::has_member_initializer(member) && !wrote_default_value,
                            .arity = deduced.arity,
                    };

                    std::expected<void, error> outcome;

                    if constexpr (deduced.store == store_kind::none) {
                        // `[[= clapp::skip{}]]`: `out` was value-initialized by the
                        // caller, so the member initializer is already in place and
                        // there is nothing to do — in either mode. Writing `field = {}`
                        // here would make update_from_matches() erase a skipped field,
                        // which clap_derive's updater explicitly does not do.
                    } else if constexpr (deduced.store == store_kind::nested) {
                        // `Style`, NOT `meta::rename_all_of(^^member_type)`: a flatten
                        // stays on the same command level. See the \warning above.
                        outcome = fill_struct<Style>(out.[:member:], matches, mode);
                    } else if constexpr (deduced.store == store_kind::subcommand) {
                        outcome = store_subcommand<Style>(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::optional_subcommand) {
                        outcome = store_optional_subcommand<Style>(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::flag) {
                        outcome = store_flag(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::optional_flag) {
                        outcome = store_optional_flag(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::count) {
                        outcome = store_count(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::single) {
                        outcome = store_single(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::optional_single) {
                        outcome = store_optional_single(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::optional_optional_single) {
                        outcome = store_optional_optional_single(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::many) {
                        outcome = store_many(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::optional_many) {
                        outcome = store_optional_many(out.[:member:], ctx);
                    } else if constexpr (deduced.store == store_kind::fixed) {
                        outcome = store_fixed(out.[:member:], ctx);
                    } else {
                        static_assert(deduced.store == store_kind::pair,
                                      "clapp: a clapp::store_kind was added without a "
                                      "store-back branch in from_matches.hpp.");
                        outcome = store_pair(out.[:member:], ctx);
                    }

                    if (!outcome.has_value()) failure = std::move(outcome.error());
                }
            }

            if (failure.has_value()) return std::unexpected(std::move(failure.value()));
            return {};
        }

    }  // namespace detail

    // =======================================================================
    // The seam
    // =======================================================================

    /**
     * \brief Build a \p T from parse matches (mirror of `command_of<T>()`).
     * \tparam T Aggregate used with `command_of<T>()`.
     * \param matches Matches for this command level (subcommand: that level's).
     * \return Filled object or first failure.
     * \code
     * static constexpr auto spec = clapp::command_of<cli>();
     * const auto matches = clapp::parse(spec, clapp::raw_args::from_args());
     * const auto args    = clapp::from_matches<cli>(*matches);
     * \endcode
     * \note Starts from `T out{}` so unmentioned fields keep member initializers.
     * \warning `from_global` needs no special case: `parse` already propagated
     *          globals to every level (`propagate_globals`). Do not read ancestors;
     *          strongest `value_source` wins.
     */
    template<class T>
        requires derivable_command<T>
    [[nodiscard]] std::expected<T, error> from_matches(const arg_matches& matches) {
        T out{};
        // Seeded with T's own rename_all, exactly as clapp::detail::derive_command()
        // seeds derive_args_into(). From here down the style is inherited, not
        // recomputed; see the \warning on clapp::detail::fill_struct.
        std::expected<void, error> outcome = detail::fill_struct<meta::rename_all_of(^^T)>(
                out, matches, detail::fill_mode::construct);
        if (!outcome.has_value()) return std::unexpected(std::move(outcome.error()));
        return out;
    }

    /**
     * \brief Update existing \p out from matches; omit leaves prior values.
     * \tparam T Aggregate command struct.
     * \param out Updated in place; partial on mid-walk failure.
     * \param matches Matches for this command level.
     * \return Void or first failure.
     * \note `[[= skip{}]]` fields are not touched.
     * \warning Not transactional (same as clap). Use `from_matches` for all-or-nothing.
     */
    template<class T>
        requires derivable_command<T>
    [[nodiscard]] std::expected<void, error> update_from_matches(T& out,
                                                                 const arg_matches& matches) {
        // Seeded exactly as from_matches() seeds it. `Style` is not deducible — it is a
        // non-type parameter no argument mentions — so leaving it off here is not a
        // silent default, it is a hard error at the first instantiation. It was one:
        // when fill_struct() grew the parameter, this call site was missed and every
        // translation unit that instantiated the update family stopped compiling.
        return detail::fill_struct<meta::rename_all_of(^^T)>(
                out, matches, detail::fill_mode::update);
    }

    namespace detail {

        // ---------------------------------------------------------------------
        // Compile-time contracts.
        // ---------------------------------------------------------------------

        /** Id is field name, not long option (`no_color` != `--no-color`). */
        struct id_probe {
            bool no_color;          /**< Snake-case source identifier. */
            int type_;              /**< Keyword-escaped source identifier. */
            std::string outputFile; /**< Camel-case source identifier. */
        };

        static_assert(meta::arg_id_of(std::meta::nonstatic_data_members_of(
                              ^^id_probe, std::meta::access_context::current())[0]) == "no_color");
        static_assert(meta::arg_id_of(std::meta::nonstatic_data_members_of(
                              ^^id_probe, std::meta::access_context::current())[1]) == "type");
        static_assert(meta::arg_id_of(std::meta::nonstatic_data_members_of(
                              ^^id_probe, std::meta::access_context::current())[2]) ==
                      "outputFile");

        /** Every `store_kind` has a `fill_struct` branch (fixture-free check). */
        [[nodiscard]] consteval bool every_store_kind_has_a_branch() {
            for (const std::meta::info enumerator : std::meta::enumerators_of(^^store_kind)) {
                switch (const auto store = std::meta::extract<store_kind>(enumerator); store) {
                case store_kind::none:
                case store_kind::nested:
                case store_kind::subcommand:
                case store_kind::optional_subcommand:
                case store_kind::flag:
                case store_kind::optional_flag:
                case store_kind::count:
                case store_kind::single:
                case store_kind::optional_single:
                case store_kind::optional_optional_single:
                case store_kind::many:
                case store_kind::optional_many:
                case store_kind::fixed:
                case store_kind::pair:
                    break;
                default:
                    return false;
                }
            }
            return true;
        }

        static_assert(every_store_kind_has_a_branch(),
                      "clapp: a clapp::store_kind enumerator has no store-back branch in "
                      "from_matches.hpp.");

    }  // namespace detail

}  // namespace clapp
