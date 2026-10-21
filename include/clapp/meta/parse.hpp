/**
 * \file
 * \brief Derive-layer entry points: parse / parse_from / try_parse / try_parse_from.
 */

#pragma once

#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/detail/std_meta.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/meta/deduce.hpp>
#include <clapp/meta/from_matches.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/util/str.hpp>

#include <cstddef>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__JETBRAINS_IDE__)
#define CLAPP_DETAIL_DIAGNOSTIC_ASSERT(condition, ...) \
    static_assert(condition, "clapp: invalid reflection-derived command")
#else
#define CLAPP_DETAIL_DIAGNOSTIC_ASSERT(condition, ...) static_assert(condition, __VA_ARGS__)
#endif

namespace clapp {

    namespace meta {
        /** Reflect an opt-in default initializer into the same spelling used by the CLI. */
        template<class T, std::meta::info Member>
        [[nodiscard]] consteval std::string_view extract_default_member_string() {
            T defaults{};
            const auto& value = defaults.[:Member:];
            using V = std::remove_cvref_t<decltype(value)>;
            std::string out;
            if constexpr (std::is_same_v<V, bool>) {
                out = value ? "true" : "false";
            } else if constexpr (std::is_integral_v<V>) {
                using U = std::make_unsigned_t<V>;
                U magnitude = static_cast<U>(value);
                if constexpr (std::is_signed_v<V>) {
                    if (value < 0) { out.push_back('-'); magnitude = U{0} - magnitude; }
                }
                char digits[std::numeric_limits<U>::digits10 + 3]{};
                std::size_t count = 0;
                do { digits[count++] = static_cast<char>('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
                while (count) out.push_back(digits[--count]);
            } else if constexpr (std::is_enum_v<V>) {
                template for (constexpr auto item : std::define_static_array(std::meta::enumerators_of(^^V))) {
                    if (value == [:item:]) {
                        constexpr auto attr = annotation_or<value_attr>(item);
                        if constexpr (!attr.name.empty()) detail::append_bytes(out, attr.name.view());
                        else out = clapp::rename(std::meta::identifier_of(item), naming::kebab);
                    }
                }
            } else if constexpr (std::is_floating_point_v<V>) {
                // Build the exact binary value as decimal digits, then round once
                // to max_digits10 significant digits. This avoids non-constexpr
                // to_chars and any floating-to-integer overflow for large values.
                V magnitude = value;
                if (magnitude < 0) { out.push_back('-'); magnitude = -magnitude; }
                if (magnitude != magnitude) out += "nan";
                else if (magnitude > std::numeric_limits<V>::max()) out += "inf";
                else if (magnitude == 0) out += "0";
                else {
                    int exponent = 0;
                    while (magnitude >= V{2}) { magnitude /= V{2}; ++exponent; }
                    while (magnitude < V{1}) { magnitude *= V{2}; --exponent; }
                    std::string digits = "0"; // least significant decimal digit first
                    auto multiply = [&](unsigned factor, unsigned carry = 0) {
                        for (char& digit : digits) {
                            const unsigned n = static_cast<unsigned>(digit - '0') * factor + carry;
                            digit = static_cast<char>('0' + n % 10);
                            carry = n / 10;
                        }
                        while (carry) { digits.push_back(static_cast<char>('0' + carry % 10)); carry /= 10; }
                    };
                    for (int i = 0; i < std::numeric_limits<V>::digits; ++i) {
                        const bool bit = magnitude >= V{1};
                        multiply(2, bit ? 1U : 0U);
                        magnitude = (magnitude - (bit ? V{1} : V{0})) * V{2};
                    }
                    exponent -= std::numeric_limits<V>::digits - 1;
                    int scale = 0;
                    if (exponent < 0) {
                        scale = -exponent;
                        for (int i = 0; i < scale; ++i) multiply(5);
                    } else for (int i = 0; i < exponent; ++i) multiply(2);
                    std::ranges::reverse(digits);
                    // Trailing zeroes carry no precision. Retain the original
                    // decimal point while dropping them.
                    int point = static_cast<int>(digits.size()) - scale;
                    const auto precision = static_cast<std::size_t>(std::numeric_limits<V>::max_digits10);
                    if (digits.size() > precision) {
                        const bool round_up = digits[precision] >= '5';
                        digits.resize(precision);
                        if (round_up) {
                            auto i = digits.size();
                            while (i && digits[i - 1] == '9') digits[--i] = '0';
                            if (i) ++digits[i - 1];
                            else { digits.insert(digits.begin(), '1'); ++point; }
                        }
                    }
                    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
                    if (point <= 0) {
                        out += "0.";
                        out.append(static_cast<std::size_t>(-point), '0');
                        out += digits;
                    } else {
                        for (std::size_t i = 0; i < digits.size(); ++i) {
                            if (i == static_cast<std::size_t>(point)) out.push_back('.');
                            out.push_back(digits[i]);
                        }
                        if (static_cast<std::size_t>(point) > digits.size()) out.append(static_cast<std::size_t>(point) - digits.size(), '0');
                    }
                }
            } else if constexpr (std::is_convertible_v<const V&, std::string_view>) {
                detail::append_bytes(out, std::string_view(value));
            } else {
                static_assert(std::is_arithmetic_v<V>, "clapp: auto_default_value requires a scalar number, bool, string, or enum initializer");
            }
            return std::string_view(std::define_static_string(out));
        }
    }

    namespace detail {

        // =====================================================================
        // Compile-time diagnostics
        // =====================================================================

        /**
         * \brief Copy a promoted static string into a transient one.
         * \param text Typically `arg_id_of` / `subcommand_name_of` / `cstr::view`.
         * \return Owning `std::string` with the same bytes.
         * \warning **Load-bearing under ubsan.** Builders take `string_view` and
         *          construct `string`; libstdc++ null-tests the source. Under
         *          `-fsanitize=null` GCC will not fold that for variables
         *          (`define_static_string`). Literals and push_back strings fold;
         *          promoted views do not. Diagnostic in `basic_string.h` (trap 10).
         *          Every builder string in this header goes through here.
         */
        [[nodiscard]] consteval std::string builder_text(std::string_view text) {
            std::string out;
            append_bytes(out, text);
            return out;
        }

        /**
         * \brief Whether \p message contains \p needle (no null pointer tests).
         * \param message Haystack (often a promoted diagnostic).
         * \param needle Substring.
         * \return `true` if \p needle occurs in \p message.
         * \warning **`string_view::find` is unusable on promoted strings under ubsan.**
         *          libstdc++ `__str_find` null-tests the haystack; GCC will not fold
         *          that for `define_static_string` variables. Whole-message `==`
         *          still folds (`memcmp`). Third instance of trap 10.
         */
        [[nodiscard]] consteval bool message_mentions(std::string_view message,
                                                      std::string_view needle) {
            if (needle.size() > message.size()) return false;
            for (std::size_t start = 0; start + needle.size() <= message.size(); ++start) {
                bool same = true;
                for (std::size_t offset = 0; offset < needle.size(); ++offset) {
                    if (message[start + offset] != needle[offset]) {
                        same = false;
                        break;
                    }
                }
                if (same) return true;
            }
            return false;
        }

        /**
         * \brief Diagnostic for a field with no `value_parser`.
         * \param field Field identifier.
         * \param type Value type display string only.
         * \return Message in static storage for `static_assert`.
         * \note `type` is `display_string_of` (trap 11) — fine for diagnostics, not
         *       identity. Tests must use literal arguments, not reflected type names.
         */
        [[nodiscard]] consteval std::string_view no_value_parser_message(std::string_view field,
                                                                         std::string_view type) {
            std::string out;
            append_bytes(out, "clapp: field '");
            append_bytes(out, field);
            append_bytes(out, "' of type '");
            append_bytes(out, type);
            append_bytes(out,
                         "' has no value_parser. Specialize clapp::value_parser<> for it, "
                         "or mark the field [[= clapp::skip{}]].");
            return std::string_view(std::define_static_string(out));
        }

        /**
         * \brief Diagnostic when the parse type is not storable in `any_value`.
         * \param field Field identifier.
         * \param type Display string only.
         * \return Message in static storage.
         * \note Separate from no-parser: fix is type properties, not a missing parser.
         */
        [[nodiscard]] consteval std::string_view not_storable_message(std::string_view field,
                                                                      std::string_view type) {
            std::string out;
            append_bytes(out, "clapp: field '");
            append_bytes(out, field);
            append_bytes(out, "' of type '");
            append_bytes(out, type);
            append_bytes(out,
                         "' has a clapp::value_parser but the parsed type cannot be stored "
                         "in a clapp::arg_matches; it must be copy-constructible and "
                         "destructible. Mark the field [[= clapp::skip{}]] if it is not "
                         "meant to be parsed.");
            return std::string_view(std::define_static_string(out));
        }

        /**
         * \brief Diagnostic for a duplicated derived name.
         * \param command Command's derived name.
         * \param what `"argument id"` / `"long option"` / `"short option"` / `"subcommand"`.
         * \param name Colliding spelling.
         * \param first Owner that claimed it first.
         * \param second Owner that claimed it again.
         * \return Message in static storage.
         * \note Both owners are essential (ADR-0008); enforced via compile_fail tests.
         */
        [[nodiscard]] consteval std::string_view duplicate_name_message(std::string_view command,
                                                                        std::string_view what,
                                                                        std::string_view name,
                                                                        std::string_view first,
                                                                        std::string_view second) {
            std::string out;
            append_bytes(out, "clapp: command '");
            append_bytes(out, command);
            append_bytes(out, "': the fields '");
            append_bytes(out, first);
            append_bytes(out, "' and '");
            append_bytes(out, second);
            append_bytes(out, "' both derive the ");
            append_bytes(out, what);
            append_bytes(out, " '");
            append_bytes(out, name);
            append_bytes(out,
                         "'. Rename one of the fields, or give it an explicit "
                         "[[= clapp::arg{.long_ = \"...\"}]] / .short_ / .no_long, or "
                         "[[= clapp::cmd{.name = \"...\"}]] for a subcommand.");
            return std::string_view(std::define_static_string(out));
        }

        /**
         * \brief Diagnostic for `[[= flatten{}]]` on a non-aggregate type.
         * \param field Field identifier.
         * \param type Display string only.
         * \return Message in static storage.
         * \note Without it, both compilers fail deep with opaque meta errors that
         *       name neither field nor annotation.
         */
        [[nodiscard]] consteval std::string_view not_flattenable_message(std::string_view field,
                                                                         std::string_view type) {
            std::string out;
            append_bytes(out, "clapp: field '");
            append_bytes(out, field);
            append_bytes(out, "' of type '");
            append_bytes(out, type);
            append_bytes(out,
                         "' carries [[= clapp::flatten{}]], which needs a "
                         "default-constructible aggregate class whose fields are "
                         "spliced into this command. Remove the annotation, or make "
                         "the type an aggregate with public data members.");
            return std::string_view(std::define_static_string(out));
        }

        // =====================================================================
        // The predicate half of clapp::parsable_command
        // =====================================================================

        /**
         * \brief Variant behind a subcommand / optional-subcommand field.
         * \param member Subcommand-set member.
         * \return Canonical variant (optional peeled if present).
         * \note Bare form must be canonicalized (trap 3); optional unwrapped first.
         */
        [[nodiscard]] consteval std::meta::info variant_of(std::meta::info member) {
            const std::meta::info type = std::meta::type_of(member);
            return meta::is_variant(type) ? meta::canonical_type(type)
                                          : meta::sole_type_argument(type);
        }

        /**
         * \brief Whether every parsed field of \p T (and nested) has a usable parser.
         * \tparam T Command struct.
         * \return `true` when the whole tree is parsable.
         * \note Answers only; diagnostics live in `check_fields_parsable`.
         */
        template<class T>
        [[nodiscard]] consteval bool all_fields_parsable() {
            if constexpr (!derivable_command<T>) {
                return false;
            } else {
                bool ok = true;
                template for (constexpr std::meta::info member :
                              std::define_static_array(std::meta::nonstatic_data_members_of(
                                      ^^T, std::meta::access_context::current()))) {
                    constexpr field_shape shape = meta::shape_of(member);

                    // Every `using` alias below is declared inside the branch that uses
                    // it, not once at the top of the body. One at the top would be dead
                    // weight in every other branch, which -Wunused-local-typedefs reports
                    // once per field of every command struct in the program; the same
                    // reasoning is written out on clapp::detail::fill_struct.
                    if constexpr (meta::has_annotation<subcommand>(member) &&
                                  shape != field_shape::subcommand_set &&
                                  shape != field_shape::optional_subcommand_set) {
                        ok = false;
                    } else if constexpr (shape == field_shape::skipped) {
                        // Never parsed; its type is nobody's business.
                    } else if constexpr (shape == field_shape::flattened) {
                        using nested_type = [:std::meta::type_of(member):];
                        if (!all_fields_parsable<nested_type>()) ok = false;
                    } else if constexpr (shape == field_shape::subcommand_set ||
                                         shape == field_shape::optional_subcommand_set) {
                        using variant_type = [:variant_of(member):];
                        template for (constexpr std::meta::info alternative :
                                      meta::variant_traits<variant_type>::reflections) {
                            using alternative_type = [:alternative:];
                            if constexpr (std::is_same_v<alternative_type, std::monostate>) {
                                ok = false;
                            } else if constexpr (meta::has_annotation<external_subcommand>(alternative)) {
                                if constexpr (!derivable_command<alternative_type> || meta::external_subcommand_shape(alternative) == 0) ok = false;
                            } else if (!all_fields_parsable<alternative_type>()) ok = false;
                        }
                    } else if constexpr (shape == field_shape::counter) {
                        // clapp::count_type is pinned by clapp::arg_action::count and is
                        // always parsable; the field's own integer type never reaches a
                        // value_parser. See the \note on clapp::detail::store_count.
                    } else if constexpr (shape == field_shape::tuple) {
                        template for (constexpr auto elem : std::define_static_array(meta::tuple_types_of(std::meta::type_of(member)))) {
                            using E = [:elem:];
                            if (!erasable_parsable<E>) ok = false;
                        }
                    } else if constexpr (shape == field_shape::pair) {
                        using first_type  = [:meta::pair_types_of(std::meta::type_of(member))[0]:];
                        using second_type = [:meta::pair_types_of(std::meta::type_of(member))[1]:];
                        if (!erasable_parsable<first_type> || !erasable_parsable<second_type>) {
                            ok = false;
                        }
                    } else {
                        using value_type = [:meta::value_type_of(std::meta::type_of(member)):];
                        if (!erasable_parsable<value_type>) ok = false;
                    }
                }
                return ok;
            }
        }

        /**
         * \brief One derived spelling together with whoever derived it.
         *
         * \tparam Name `std::string` for ids, long options and subcommand names;
         *         `char` for short options.
         *
         * \note The owner travels with the name because the diagnostic needs it and
         *       cannot recover it afterwards: by the time a repeat is found, the two
         *       fields that produced it are indistinguishable from any other pair.
         */
        template<class Name>
        struct derived_name {
            Name name;         /**< The spelling as the command line will see it. */
            std::string owner; /**< Identifier of the field, or of the variant alternative. */
        };

        /**
         * \brief Every name one command level derives, for the duplicate check.
         *
         * \note Transient by design: the vectors live and die inside one constant
         *       evaluation, so nothing here needs `std::define_static_string`.
         */
        struct derived_names {
            std::vector<derived_name<std::string>> ids; /**< clapp::meta::arg_id_of() per field. */
            std::vector<derived_name<std::string>> longs; /**< Derived or explicit long options. */
            std::vector<derived_name<char>> shorts;       /**< Explicit or `auto_short` letters. */
            std::vector<derived_name<std::string>>
                    subcommands; /**< clapp::meta::subcommand_name_of(). */
        };

        /**
         * \brief Collect names \p T contributes to its own command level.
         * \tparam T Command struct.
         * \param into Appended to.
         * \param style Command `rename_all`.
         * \note Recurses through flatten; not into subcommands. `from_global` skipped.
         */
        template<class T>
        consteval void collect_level(derived_names& into, naming style) {
            template for (constexpr std::meta::info member :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                                  ^^T, std::meta::access_context::current()))) {
                constexpr field_shape shape = meta::shape_of(member);
                constexpr bool inherited    = meta::has_annotation<from_global>(member);

                if constexpr (shape == field_shape::skipped || inherited) {
                    // Contributes no name.
                } else if constexpr (shape == field_shape::flattened) {
                    using nested_type = [:std::meta::type_of(member):];
                    // Guarded because `[[= clapp::flatten{}]]` can sit on anything at
                    // all, including `int`, and nonstatic_data_members_of() rejects
                    // those as 'not a complete class type'. Reaching that call from
                    // inside clapp::parsable_command would make the concept ill-formed
                    // rather than false, and `static_assert(!parsable_command<bad>)`
                    // untestable. check_fields_parsable() owns the readable report.
                    if constexpr (derivable_command<nested_type>) {
                        collect_level<nested_type>(into, style);
                    }
                } else if constexpr (shape == field_shape::subcommand_set ||
                                     shape == field_shape::optional_subcommand_set) {
                    using variant_type = [:variant_of(member):];
                    template for (constexpr std::meta::info alternative :
                                  meta::variant_traits<variant_type>::reflections) {
                        if constexpr (!meta::has_annotation<external_subcommand>(alternative)) {
                            const auto owner = builder_text(std::meta::identifier_of(alternative));
                            into.subcommands.push_back({builder_text(meta::subcommand_name_of(alternative, style)), owner});
                            constexpr auto attr = meta::annotation_or<command_attr>(alternative);
                            if constexpr (!attr.alias.empty()) into.subcommands.push_back({builder_text(attr.alias.view()), owner});
                            if constexpr (!attr.visible_alias.empty()) into.subcommands.push_back({builder_text(attr.visible_alias.view()), owner});
                        }
                    }
                } else {
                    constexpr arg_attr attribute = meta::annotation_or<arg_attr>(member);
                    const std::string owner      = builder_text(std::meta::identifier_of(member));

                    into.ids.push_back({builder_text(meta::arg_id_of<T, member>()), owner});

                    if constexpr (attribute.index == 0) {
                        if constexpr (!attribute.no_long) {
                            into.longs.push_back(
                                    {attribute.long_.empty()
                                             ? clapp::rename(std::meta::identifier_of(member),
                                                             style)
                                             : builder_text(attribute.long_.view()),
                                     owner});
                        }
                        if constexpr (!attribute.alias.empty()) into.longs.push_back({builder_text(attribute.alias.view()), owner});
                        if constexpr (!attribute.visible_alias.empty()) into.longs.push_back({builder_text(attribute.visible_alias.view()), owner});
                        if constexpr (attribute.short_alias != '\0') into.shorts.push_back({attribute.short_alias, owner});
                        if constexpr (attribute.visible_short_alias != '\0') into.shorts.push_back({attribute.visible_short_alias, owner});
                        if constexpr (attribute.short_ != '\0') {
                            into.shorts.push_back({attribute.short_, owner});
                        } else if constexpr (attribute.auto_short) {
                            const char letter =
                                    clapp::rename_initial(std::meta::identifier_of(member), style);
                            if (letter != '\0') into.shorts.push_back({letter, owner});
                        }
                    }
                }
            }
        }

        /**
         * \brief Outcome of scanning one namespace for a repeated spelling.
         * \tparam Name `std::string` or `char`.
         * \note `found` flag, not a sentinel (empty id is a real collision case).
         */
        template<class Name>
        struct repeated_name {
            bool found = false;   /**< Whether any spelling appears twice. */
            Name name{};          /**< The repeated spelling. */
            std::string first{};  /**< Who derived it first. */
            std::string second{}; /**< Who derived it again. */
        };

        /**
         * \brief The first spelling that appears twice in \p values, with both owners.
         * \tparam Name `std::string` or `char`.
         * \param values The collected names.
         * \return The repeat and the two owners, or a default `repeated_name` when all
         *         spellings are distinct.
         */
        template<class Name>
        [[nodiscard]] consteval repeated_name<Name>
        first_repeat(const std::vector<derived_name<Name>>& values) {
            for (std::size_t a = 0; a < values.size(); ++a) {
                for (std::size_t b = a + 1; b < values.size(); ++b) {
                    if (values[a].name == values[b].name) {
                        return {true, values[a].name, values[a].owner, values[b].owner};
                    }
                }
            }
            return {};
        }

        /**
         * \brief Whether \p T's own command level has unique derived names.
         * \tparam T Command struct.
         * \return Uniqueness on this level only.
         * \note Split so diagnostics name the level that collides.
         */
        template<class T>
        [[nodiscard]] consteval bool level_is_unique() {
            if constexpr (!derivable_command<T>) {
                return false;
            } else {
                derived_names level;
                collect_level<T>(level, meta::rename_all_of(^^T));
                return !first_repeat(level.ids).found && !first_repeat(level.longs).found &&
                       !first_repeat(level.shorts).found && !first_repeat(level.subcommands).found;
            }
        }

        /**
         * \brief Whether every command level under \p T has unique derived names.
         * \tparam T Command struct.
         * \return Uniqueness within each single level.
         * \note Does not cover freeze-injected `--help` / group cycles; answers only.
         */
        template<class T>
        [[nodiscard]] consteval bool names_are_unique();

        /**
         * \brief Recurse names_are_unique() into every subcommand \p T declares.
         * \tparam T A command struct.
         * \return `true` when every level strictly below \p T is unique.
         */
        template<class T>
        [[nodiscard]] consteval bool subcommand_levels_are_unique() {
            bool ok = true;
            template for (constexpr std::meta::info member :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                                  ^^T, std::meta::access_context::current()))) {
                constexpr field_shape shape = meta::shape_of(member);

                if constexpr (shape == field_shape::flattened) {
                    using nested_type = [:std::meta::type_of(member):];
                    // See collect_level(): a flatten marker on a non-aggregate must
                    // leave this walk answering `false`, never throwing.
                    if constexpr (derivable_command<nested_type>) {
                        if (!subcommand_levels_are_unique<nested_type>()) ok = false;
                    }
                } else if constexpr (shape == field_shape::subcommand_set ||
                                     shape == field_shape::optional_subcommand_set) {
                    using variant_type = [:variant_of(member):];
                    template for (constexpr std::meta::info alternative :
                                  meta::variant_traits<variant_type>::reflections) {
                        using alternative_type = [:alternative:];
                        if constexpr (!meta::has_annotation<external_subcommand>(alternative))
                            if (!names_are_unique<alternative_type>()) ok = false;
                    }
                }
            }
            return ok;
        }

        template<class T>
        [[nodiscard]] consteval bool names_are_unique() {
            if constexpr (!derivable_command<T>) {
                return false;
            } else {
                return level_is_unique<T>() && subcommand_levels_are_unique<T>();
            }
        }

    }  // namespace detail

    // =======================================================================
    // The concept
    // =======================================================================

    /**
     * \brief Requirements for `parse<T>`: derivable + parsable fields + unique names.
     * \tparam T Command struct.
     * \note Three atoms so constraint notes name which rule failed. Never hard-errors
     *       (supports `static_assert(!parsable_command<bad>)`). Necessary not
     *       sufficient — freeze still rejects help collisions / group cycles.
     */
    template<class T>
    concept parsable_command = derivable_command<T> && detail::all_fields_parsable<T>() &&
                               detail::names_are_unique<T>();

    namespace detail {

        // =====================================================================
        // The readable report
        // =====================================================================

        /**
         * \brief `static_assert` every field of \p T against `value_parser`.
         * \tparam T Command struct.
         * \note Only from `reject_command` after the concept is false; names the field.
         */
        template<class T>
        constexpr void check_fields_parsable() {
            template for (constexpr std::meta::info member :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                                  ^^T, std::meta::access_context::current()))) {
                constexpr field_shape shape = meta::shape_of(member);

                if constexpr (meta::has_annotation<subcommand>(member) &&
                              shape != field_shape::subcommand_set &&
                              shape != field_shape::optional_subcommand_set) {
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(!meta::has_annotation<subcommand>(member),
                        "clapp::subcommand{} needs a std::variant or std::optional<std::variant>");
                } else if constexpr (shape == field_shape::skipped) {
                    // Never parsed.
                } else if constexpr (shape == field_shape::flattened) {
                    using nested_type = [:std::meta::type_of(member):];
                    // Bound to named objects before the call; see the pair branch below
                    // for the GCC 16.1.0 wrong-code reason.
                    [[maybe_unused]] constexpr std::string_view field =
                            std::meta::identifier_of(member);
                    [[maybe_unused]] constexpr std::string_view text =
                            std::meta::display_string_of(std::meta::type_of(member));
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(derivable_command<nested_type>,
                                                   not_flattenable_message(field, text));
                    if constexpr (derivable_command<nested_type>) {
                        check_fields_parsable<nested_type>();
                    }
                } else if constexpr (shape == field_shape::subcommand_set ||
                                     shape == field_shape::optional_subcommand_set) {
                    using variant_type = [:variant_of(member):];
                    template for (constexpr std::meta::info alternative :
                                  meta::variant_traits<variant_type>::reflections) {
                        using alternative_type = [:alternative:];
                        if constexpr (std::is_same_v<alternative_type, std::monostate>) {
                            CLAPP_DETAIL_DIAGNOSTIC_ASSERT(!std::is_same_v<alternative_type, std::monostate>,
                                "std::monostate is not supported as a std::variant alternative");
                        } else if constexpr (meta::has_annotation<external_subcommand>(alternative)) {
                            CLAPP_DETAIL_DIAGNOSTIC_ASSERT(meta::external_subcommand_shape(alternative) != 0,
                                "clapp::external_subcommand{} on a std::variant alternative is not implemented for this shape; use a sequence field (std::vector<std::string>) or (name, sequence) fields");
                        } else check_fields_parsable<alternative_type>();
                    }
                } else if constexpr (shape == field_shape::counter) {
                    // clapp::count_type, never the field's own integer type.
                } else if constexpr (shape == field_shape::tuple) {
                    template for (constexpr auto elem : std::define_static_array(meta::tuple_types_of(std::meta::type_of(member)))) {
                        using E = [:elem:];
                        CLAPP_DETAIL_DIAGNOSTIC_ASSERT(erasable_parsable<E>, "clapp: tuple element needs a storable value_parser");
                    }
                } else if constexpr (shape == field_shape::pair) {
                    using first_type  = [:meta::pair_types_of(std::meta::type_of(member))[0]:];
                    using second_type = [:meta::pair_types_of(std::meta::type_of(member))[1]:];
                    // Bound to named objects before the call: GCC 16.1.0 has a
                    // wrong-code bug where a constexpr function called with string
                    // *literal* arguments can disagree with its own compile-time value
                    // (CLAUDE.md, known toolchain workarounds).
                    //
                    // The type is spelled from the *reflection*, not from `^^first_type`:
                    // splicing a local alias and reflecting it back makes GCC print
                    // `'first_type {aka nope}'`, and the alias is this header's private
                    // business rather than anything the reader wrote.
                    [[maybe_unused]] constexpr std::string_view field =
                            std::meta::identifier_of(member);
                    [[maybe_unused]] constexpr std::string_view first_text =
                            std::meta::display_string_of(
                            meta::pair_types_of(std::meta::type_of(member))[0]);
                    [[maybe_unused]] constexpr std::string_view second_text =
                            std::meta::display_string_of(
                                    meta::pair_types_of(std::meta::type_of(member))[1]);
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(
                            parsable<first_type>, no_value_parser_message(field, first_text));
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(
                            parsable<second_type>, no_value_parser_message(field, second_text));
                    // Nested, so a type with no parser at all fires exactly one
                    // assertion. Both at the same level would print two messages with two
                    // different fixes for one mistake.
                    if constexpr (parsable<first_type> && parsable<second_type>) {
                        CLAPP_DETAIL_DIAGNOSTIC_ASSERT(
                                erasable_parsable<first_type>,
                                not_storable_message(field, first_text));
                        CLAPP_DETAIL_DIAGNOSTIC_ASSERT(
                                erasable_parsable<second_type>,
                                not_storable_message(field, second_text));
                    }
                } else {
                    using value_type = [:meta::value_type_of(std::meta::type_of(member)):];
                    [[maybe_unused]] constexpr std::string_view field =
                            std::meta::identifier_of(member);
                    [[maybe_unused]] constexpr std::string_view text =
                            std::meta::display_string_of(
                                    meta::value_type_of(std::meta::type_of(member)));
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(parsable<value_type>,
                                                   no_value_parser_message(field, text));
                    if constexpr (parsable<value_type>) {
                        CLAPP_DETAIL_DIAGNOSTIC_ASSERT(erasable_parsable<value_type>,
                                                       not_storable_message(field, text));
                    }
                }
            }
        }

        /**
         * \brief Duplicate-name report for \p T (`static_assert` message).
         * \tparam T Command struct.
         * \return Message in static storage (unreachable when unique).
         */
        template<class T>
        [[nodiscard]] consteval std::string_view duplicate_name_report() {
            const std::string_view command = meta::subcommand_name_of(^^T, naming::kebab);

            derived_names level;
            collect_level<T>(level, meta::rename_all_of(^^T));

            const repeated_name<std::string> repeated_id = first_repeat(level.ids);
            if (repeated_id.found) {
                return duplicate_name_message(command,
                                              "argument id",
                                              repeated_id.name,
                                              repeated_id.first,
                                              repeated_id.second);
            }
            const repeated_name<std::string> repeated_long = first_repeat(level.longs);
            if (repeated_long.found) {
                return duplicate_name_message(command,
                                              "long option",
                                              repeated_long.name,
                                              repeated_long.first,
                                              repeated_long.second);
            }
            const repeated_name<char> repeated_short = first_repeat(level.shorts);
            if (repeated_short.found) {
                std::string letter;
                letter.push_back(repeated_short.name);
                return duplicate_name_message(command,
                                              "short option",
                                              letter,
                                              repeated_short.first,
                                              repeated_short.second);
            }
            const repeated_name<std::string> repeated_sub = first_repeat(level.subcommands);
            if (repeated_sub.found) {
                return duplicate_name_message(command,
                                              "subcommand",
                                              repeated_sub.name,
                                              repeated_sub.first,
                                              repeated_sub.second);
            }
            return std::string_view(std::define_static_string(
                    builder_text("clapp: a subcommand of this command derives one of its names "
                                 "twice; the report names the level it happened on.")));
        }

        /**
         * \brief `static_assert` the duplicate-name rule for \p T and every level below.
         * \tparam T A command struct.
         */
        template<class T>
        constexpr void check_names_unique();

        /**
         * \brief Recurse name checks into subcommands under \p T.
         * \tparam T Command struct.
         * \note Flatten is walked for nested subcommands only, not as its own level.
         */
        template<class T>
        constexpr void check_subcommand_names_unique() {
            template for (constexpr std::meta::info member :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                                  ^^T, std::meta::access_context::current()))) {
                constexpr field_shape shape = meta::shape_of(member);

                if constexpr (shape == field_shape::flattened) {
                    using nested_type = [:std::meta::type_of(member):];
                    // check_fields_parsable() has already fired the readable assertion
                    // for a flatten marker on a non-aggregate; walking into it here
                    // would only add 'not a complete class type' underneath it.
                    if constexpr (derivable_command<nested_type>) {
                        check_subcommand_names_unique<nested_type>();
                    }
                } else if constexpr (shape == field_shape::subcommand_set ||
                                     shape == field_shape::optional_subcommand_set) {
                    using variant_type = [:variant_of(member):];
                    template for (constexpr std::meta::info alternative :
                                  meta::variant_traits<variant_type>::reflections) {
                        using alternative_type = [:alternative:];
                        if constexpr (!meta::has_annotation<external_subcommand>(alternative))
                            check_names_unique<alternative_type>();
                    }
                }
            }
        }

        template<class T>
        constexpr void check_names_unique() {
            CLAPP_DETAIL_DIAGNOSTIC_ASSERT(level_is_unique<T>(), duplicate_name_report<T>());
            check_subcommand_names_unique<T>();
        }

        /**
         * \brief Explain why \p T is not `parsable_command`, then abort.
         * \tparam T Caller-supplied type.
         * \note `static_assert`s fire at compile time; `abort` for non-returning T.
         */
        template<class T>
        [[noreturn]] void reject_command() {
            static_assert(std::is_class_v<T>,
                          "clapp: parse<T>() needs a class type — T is the struct whose fields "
                          "describe the command line.");
            if constexpr (std::is_class_v<T>) {
                static_assert(std::is_aggregate_v<T>,
                              "clapp: parse<T>() needs an aggregate. clapp reads members through "
                              "std::meta::access_context::current() and therefore sees only "
                              "public ones; a constructor, a base class or a private member "
                              "hides fields silently.");
                static_assert(std::is_default_constructible_v<T>,
                              "clapp: parse<T>() needs a default-constructible T, because "
                              "clapp::from_matches() starts from `T out{}` so that every default "
                              "member initializer is in place before any value is written.");
                if constexpr (derivable_command<T>) {
                    check_fields_parsable<T>();
                    check_names_unique<T>();
                }
            }
            std::abort();
        }

        // =====================================================================
        // Runtime plumbing
        // =====================================================================

        /**
         * \brief Run the M3 parser over a frozen tree.
         * \param cmd Frozen command tree.
         * \param raw Command line.
         * \return Matches or error.
         * \note Non-dependent forwarder so name lookup binds the M3 `parse` overload
         *       at definition time (this header also adds `parse<T>(argc, argv)`).
         */
        [[nodiscard]] inline std::expected<arg_matches, error> run_parser(const command_spec& cmd,
                                                                          const raw_args& raw) {
            return clapp::parse(cmd, raw);
        }

        /**
         * \brief Print \p err (clap `Error::exit` style) and leave the process.
         * \param err Includes help/version control-flow kinds.
         * \note Stream and exit code from `use_stderr`/`exit_code` (ADR-0001), not kind.
         * \note `fwrite` on the exit path; message is already rendered.
         */
        [[noreturn]] inline void report_and_exit(const error& err, const command_spec& spec) {
            error printable = err;
            printable.set_output_if_unset(spec.get_styles(), spec.get_color());
            printable.exit();
        }

        // =====================================================================
        // The forward direction
        // =====================================================================
        //
        // ---------------------------------------------------------------------
        // INTEGRATION NOTE. `clapp::command_of<T>()` and `clapp::command_for_update<T>()`
        // live here because `parse<T>()` cannot exist without them and no other header
        // in the tree defines one. If a sibling stage lands them in their own header,
        // delete this section and the two entry points that follow it, and
        // `#include` that header instead; nothing else in this file changes. What must
        // NOT happen is two implementations: `command_of` and `from_matches` are held
        // together only by clapp::meta::arg_id_of() and clapp::meta::subcommand_name_of()
        // being the single answer to "what is this member called", and a second forward
        // direction is exactly the drift those two functions exist to prevent.
        // ---------------------------------------------------------------------

        /**
         * \brief Parse tree vs update tree (`override_required` in clap).
         */
        enum class derive_mode : unsigned char {
            parse,  /**< Fresh parse; required from the deduction table. */
            update, /**< Incremental; nothing required. */
        };

        template<class T, bool AutoDefault = meta::annotation_or<command_attr>(^^T).auto_default_value>
        consteval void derive_args_into(command_builder& into, naming style, derive_mode mode, const command_attr& attr);

        template<class V>
        consteval void
        derive_subcommands_into(command_builder& into, naming style, derive_mode mode);

        /**
         * \brief Apply a `command_attr` to a builder.
         * \param into Builder to configure.
         * \param attr Annotation, or default.
         * \param mode Parse vs update.
         * \note Empty `cstr` means unspecified (skip). Naming fields applied elsewhere.
         */
        inline consteval void
        apply_command_attr(command_builder& into, const command_attr& attr, derive_mode mode) {
            if (!attr.version.empty()) std::move(into).version(builder_text(attr.version.view()));
            if (!attr.about.empty()) std::move(into).about(builder_text(attr.about.view()));
            if (!attr.long_about.empty()) {
                std::move(into).long_about(builder_text(attr.long_about.view()));
            }
            if (!attr.author.empty()) std::move(into).author(builder_text(attr.author.view()));
            if (!attr.after_help.empty()) {
                std::move(into).after_help(builder_text(attr.after_help.view()));
            }
            if (!attr.before_help.empty()) {
                std::move(into).before_help(builder_text(attr.before_help.view()));
            }
            if (!attr.help_template.empty()) {
                std::move(into).help_template(builder_text(attr.help_template.view()));
            }
            if (!attr.next_help_heading.empty()) {
                std::move(into).next_help_heading(builder_text(attr.next_help_heading.view()));
            }

            if (attr.propagate_version) std::move(into).propagate_version();
            if (attr.infer_subcommands) std::move(into).infer_subcommands();
            if (attr.infer_long_args) std::move(into).infer_long_args();
            if (attr.allow_external_subcommands) std::move(into).allow_external_subcommands();
            if (attr.multicall) std::move(into).multicall();
            if (attr.disable_help_flag) std::move(into).disable_help_flag();
            if (attr.disable_version_flag) std::move(into).disable_version_flag();

            if (!attr.alias.empty()) std::move(into).alias(builder_text(attr.alias.view()));
            if (!attr.visible_alias.empty()) std::move(into).visible_alias(builder_text(attr.visible_alias.view()));
            if (!attr.long_flag.empty()) std::move(into).long_flag(builder_text(attr.long_flag.view()));
            if (!attr.long_version.empty()) std::move(into).long_version(builder_text(attr.long_version.view()));
            if (!attr.display_name.empty()) std::move(into).display_name(builder_text(attr.display_name.view()));
            if (!attr.bin_name.empty()) std::move(into).bin_name(builder_text(attr.bin_name.view()));
            if (attr.short_flag != '\0') std::move(into).short_flag(attr.short_flag);
            if (attr.subcommands_negate_reqs) std::move(into).subcommand_negates_reqs();
            if (attr.args_negate_subcommands) std::move(into).args_conflicts_with_subcommands();
            if (attr.subcommand_precedence_over_arg) std::move(into).subcommand_precedence_over_arg();
            if (attr.allow_missing_positional) std::move(into).allow_missing_positional();
            if (attr.trailing_var_arg) std::move(into).trailing_var_arg();
            if (attr.dont_delimit_trailing_values) std::move(into).dont_delimit_trailing_values();
            if (attr.all_args_override_self) std::move(into).args_override_self();
            if (attr.flatten_help) std::move(into).flatten_help();
            if (attr.next_line_help) std::move(into).next_line_help();
            if (attr.expand_args_from_files) std::move(into).expand_args_from_files();
            std::move(into).color(attr.color);
            if (attr.styled) std::move(into).styles(styles::styled());
            if (attr.term_width) std::move(into).term_width(attr.term_width);
            if (attr.max_term_width) std::move(into).max_term_width(attr.max_term_width);

            // clap's `override_required`: an update parse demands nothing.
            if (mode == derive_mode::parse) {
                if (attr.arg_required_else_help) std::move(into).arg_required_else_help();
                if (attr.subcommand_required) std::move(into).subcommand_required();
            }
        }

        /**
         * \brief Build the `arg_builder` for one data member.
         *
         * Deduction sets action/num_args/required; annotation overrides named fields.
         * \tparam Member Member reflection.
         * \param id From `arg_id_of` (field name, not long option).
         * \param style Enclosing `rename_all`.
         * \param mode Parse vs update.
         * \return Configured builder for `command_builder::arg()`.
         * \note Long options and opt-in environment names follow their respective
         *       rename policies; argument ids retain the source field name.
         */
        template<class T, std::meta::info Member, bool AutoDefault>
        [[nodiscard]] consteval arg_builder
        derive_arg(std::string_view id, naming style, derive_mode mode, const command_attr& cmd_attr) {
            constexpr deduced_arg deduced    = meta::deduce_member(Member);
            constexpr arg_attr attribute     = meta::annotation_or<arg_attr>(Member);
            constexpr std::string_view field = std::meta::identifier_of(Member);

            arg_builder out{builder_text(id)};
            std::move(out).action(deduced.act).num_args(deduced.num_args);

            // --- identity ----------------------------------------------------
            if constexpr (attribute.index != 0) {
                std::move(out).index(attribute.index);
            } else {
                if constexpr (!attribute.no_long) {
                    std::move(out).long_(attribute.long_.empty()
                                                 ? clapp::rename(field, style)
                                                 : builder_text(attribute.long_.view()));
                }
                if constexpr (attribute.short_ != '\0') {
                    std::move(out).short_(attribute.short_);
                } else if constexpr (attribute.auto_short) {
                    const char letter = clapp::rename_initial(field, style);
                    if (letter != '\0') std::move(out).short_(letter);
                }
            }

            // --- values ------------------------------------------------------
            //
            // A value_parser is attached only where the field's type decides one. The
            // action-pinned rows already carry the right parser (`set_true` -> bool,
            // `count` -> clapp::count_type, and clapp::arg_builder::freeze() REJECTS a
            // contradicting parser on a counter), and `std::pair` has two element types
            // and no single parser — clapp::detail::store_pair re-parses its raw bytes.
            if constexpr (deduced.shape != field_shape::flag &&
                          deduced.shape != field_shape::optional_flag &&
                          deduced.shape != field_shape::counter &&
                          deduced.shape != field_shape::pair &&
                          deduced.shape != field_shape::tuple) {
                using value_type = [:meta::value_type_of(std::meta::type_of(Member)):];
                if constexpr (erasable_parsable<value_type>) {
                    std::move(out).value_parser<value_type>();
                }
            }

            if (!attribute.value_name.empty()) {
                std::move(out).value_name(builder_text(attribute.value_name.view()));
            }
            if (!attribute.env.empty() || attribute.auto_env || cmd_attr.auto_env) {
                std::string env = attribute.env.empty()
                    ? clapp::rename(field, cmd_attr.rename_all_env)
                    : builder_text(attribute.env.view());
                const std::string prefix = builder_text(cmd_attr.env_prefix.view());
                if (!prefix.empty() && !env.starts_with(prefix)) env = prefix + env;
                std::move(out).env(env);
            }
            if (!attribute.default_value.empty()) {
                std::move(out).default_value(builder_text(attribute.default_value.view()));
            } else if constexpr (meta::has_member_initializer(Member) && (attribute.auto_default_value || AutoDefault)) {
                using M = [:std::meta::type_of(Member):];
                if constexpr (std::is_arithmetic_v<M> || std::is_enum_v<M> || std::is_convertible_v<const M&, std::string_view>) {
                    constexpr auto text = meta::extract_default_member_string<T, Member>();
                    std::move(out).default_value(builder_text(text)).reflected_default_value();
                }
            }
            if (!attribute.default_missing_value.empty()) {
                std::move(out).default_missing_value(
                        builder_text(attribute.default_missing_value.view()));
            }
            if (attribute.delimiter != '\0') std::move(out).value_delimiter(attribute.delimiter);
            if (attribute.hint != value_hint::unknown) std::move(out).value_hint(attribute.hint);

            // --- help --------------------------------------------------------
            if (!attribute.help.empty()) std::move(out).help(builder_text(attribute.help.view()));
            if (!attribute.long_help.empty()) {
                std::move(out).long_help(builder_text(attribute.long_help.view()));
            }
            if (!attribute.help_heading.empty()) {
                std::move(out).help_heading(builder_text(attribute.help_heading.view()));
            }
            // 999 is clap's "never set one" sentinel, and clapp::command_builder uses the
            // same number for its own default; forwarding it would be a no-op that
            // defeats next_display_order().
            if (attribute.display_order != 999) {
                std::move(out).display_order(attribute.display_order);
            }

            // --- flags -------------------------------------------------------
            if (attribute.global) std::move(out).global();
            if (attribute.hide) std::move(out).hide();
            if (attribute.exclusive) std::move(out).exclusive();
            if (attribute.allow_hyphen_values) std::move(out).allow_hyphen_values();
            if (attribute.allow_negative_numbers) std::move(out).allow_negative_numbers();
            if (attribute.require_equals) std::move(out).require_equals();
            if (attribute.trailing_var_arg) std::move(out).trailing_var_arg();
            if (attribute.last) std::move(out).last();
            if (attribute.ignore_case) std::move(out).ignore_case();
            if (!attribute.group.empty()) {
                // clapp::command_builder::materialise_groups() creates the group when no
                // group_builder declared it, so naming one here is always well-formed.
                std::move(out).group(builder_text(attribute.group.view()));
            }

            if (!attribute.alias.empty()) std::move(out).alias(builder_text(attribute.alias.view()));
            if (!attribute.visible_alias.empty()) std::move(out).visible_alias(builder_text(attribute.visible_alias.view()));
            if (!attribute.value_terminator.empty()) std::move(out).value_terminator(builder_text(attribute.value_terminator.view()));
            if (attribute.short_alias != '\0') std::move(out).short_alias(attribute.short_alias);
            if (attribute.visible_short_alias != '\0') std::move(out).visible_short_alias(attribute.visible_short_alias);
            if (attribute.hide_default_value) std::move(out).hide_default_value();
            if (attribute.hide_env) std::move(out).hide_env();
            if (attribute.hide_env_values) std::move(out).hide_env_values();
            if (attribute.hide_possible_values) std::move(out).hide_possible_values();
            if (attribute.hide_short_help) std::move(out).hide_short_help();
            if (attribute.hide_long_help) std::move(out).hide_long_help();
            if (attribute.next_line_help) std::move(out).next_line_help();

            // --- stackable relations -----------------------------------------
            for (const std::string_view other : meta::relation_ids_of<conflicts_with>(Member)) {
                std::move(out).conflicts_with(builder_text(other));
            }
            for (const std::string_view other : meta::relation_ids_of<requires_arg>(Member)) {
                std::move(out).requires_(builder_text(other));
            }
            for (const std::string_view other : meta::relation_ids_of<overrides_with>(Member)) {
                std::move(out).overrides_with(builder_text(other));
            }
            for (const std::string_view other :
                 meta::relation_ids_of<required_unless_any>(Member)) {
                std::move(out).required_unless_present(builder_text(other));
            }
            {
                std::vector<std::string> all_of;
                for (const std::string_view other :
                     meta::relation_ids_of<required_unless_all>(Member)) {
                    all_of.push_back(builder_text(other));
                }
                if (!all_of.empty()) std::move(out).required_unless_present_all(all_of);
            }

            for (const auto& rule : meta::annotations_all_of<required_if_eq>(Member))
                std::move(out).required_if_eq(builder_text(rule.id.view()), builder_text(rule.value.view()));
            for (const auto& rule : meta::annotations_all_of<requires_if>(Member))
                std::move(out).requires_if(builder_text(rule.value.view()), builder_text(rule.target_id.view()));
            for (const auto& rule : meta::annotations_all_of<default_value_if>(Member))
                std::move(out).default_value_if(builder_text(rule.id.view()), builder_text(rule.predicate_value.view()), builder_text(rule.default_value.view()));

            // --- required, last, so an explicit tri and the update mode both win ---
            std::move(out).required(mode == derive_mode::parse && deduced.required);
            return out;
        }

        template<class T>
        consteval void add_group_members(group_builder& group) {
            template for (constexpr auto member : std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()))) {
                constexpr auto shape = meta::shape_of(member);
                if constexpr (shape == field_shape::flattened) {
                    using Nested = [:std::meta::type_of(member):];
                    add_group_members<Nested>(group);
                } else if constexpr (clapp::is_argument(shape) && !meta::has_annotation<from_global>(member)) {
                    std::move(group).arg(builder_text(meta::arg_id_of<T, member>()));
                }
            }
        }

        template<class T>
        consteval void derive_groups_into(command_builder& into, std::meta::info target, derive_mode mode) {
            for (const auto& attribute : meta::annotations_all_of<group_attr>(target)) {
                const auto id = attribute.id.empty() ? builder_text(std::meta::identifier_of(^^T))
                                                    : builder_text(attribute.id.view());
                group_builder group{id};
                std::move(group).required(mode == derive_mode::parse && attribute.required).multiple(attribute.multiple);
                if (attribute.arg.empty()) add_group_members<T>(group);
                else std::move(group).arg(builder_text(attribute.arg.view()));
                if (!attribute.conflicts_with.empty()) std::move(group).conflicts_with(builder_text(attribute.conflicts_with.view()));
                if (!attribute.requires_arg.empty()) std::move(group).requires_(builder_text(attribute.requires_arg.view()));
                for (auto other : meta::relation_ids_of<conflicts_with>(target)) std::move(group).conflicts_with(builder_text(other));
                for (auto other : meta::relation_ids_of<requires_arg>(target)) std::move(group).requires_(builder_text(other));
                std::move(into).group(std::move(group));
            }
        }

        /** \brief Derive the reflected arguments of \p T into \p into. */
        template<class T, bool AutoDefault>
        consteval void derive_args_into(command_builder& into, naming style, derive_mode mode, const command_attr& attr) {
            static_assert(derivable_command<T>,
                          "clapp: command_of<T>() needs an aggregate with public data members; "
                          "std::meta::access_context::current() cannot see private ones.");

            template for (constexpr std::meta::info member :
                          std::define_static_array(std::meta::nonstatic_data_members_of(
                                  ^^T, std::meta::access_context::current()))) {
                constexpr deduced_arg deduced = meta::deduce_member(member);
                constexpr bool inherited      = meta::has_annotation<from_global>(member);

                if constexpr (deduced.shape == field_shape::skipped || inherited) {
                    // `skip` never becomes an argument. `from_global` is declared by an
                    // ancestor, and clapp::command_builder::propagate_global_args()
                    // copies the declaration into every child, so declaring it again here
                    // would be a duplicate id rather than a second argument.
                } else if constexpr (deduced.shape == field_shape::flattened) {
                    using nested_type = [:std::meta::type_of(member):];
                    derive_args_into<nested_type, AutoDefault || meta::annotation_or<command_attr>(^^nested_type).auto_default_value>(into, style, mode, attr);
                    derive_groups_into<nested_type>(into, member, mode);
                } else if constexpr (deduced.shape == field_shape::subcommand_set) {
                    // clap's implicit pair for a non-Option `#[command(subcommand)]`:
                    // `.subcommand_required(true).arg_required_else_help(true)`, both
                    // turned off again by augment_args_for_update.
                    if (mode == derive_mode::parse) {
                        std::move(into).subcommand_required().arg_required_else_help();
                    }
                    using variant_type = [:variant_of(member):];
                    derive_subcommands_into<variant_type>(into, style, mode);
                } else if constexpr (deduced.shape == field_shape::optional_subcommand_set) {
                    using variant_type = [:variant_of(member):];
                    derive_subcommands_into<variant_type>(into, style, mode);
                } else {
                    constexpr std::string_view id = meta::arg_id_of<T, member>();
                    std::move(into).arg(derive_arg<T, member, AutoDefault>(id, style, mode, attr));
                }
            }
            derive_groups_into<T>(into, ^^T, mode);
        }

        /** \brief Derive each reflected variant alternative as a subcommand. */
        template<class V>
        consteval void
        derive_subcommands_into(command_builder& into, naming style, derive_mode mode) {
            template for (constexpr std::meta::info alternative :
                          meta::variant_traits<V>::reflections) {
                using alternative_type = [:alternative:];

                if constexpr (std::is_same_v<alternative_type, std::monostate>) {
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(!std::is_same_v<alternative_type, std::monostate>,
                        "std::monostate is not supported as a std::variant alternative");
                } else if constexpr (meta::has_annotation<external_subcommand>(alternative)) {
                    constexpr unsigned shape = meta::external_subcommand_shape(alternative);
                    CLAPP_DETAIL_DIAGNOSTIC_ASSERT(shape != 0,
                        "clapp::external_subcommand{} on a std::variant alternative is not implemented for this shape; use a sequence field (std::vector<std::string>) or (name, sequence) fields");
                    if constexpr (shape != 0) {
                        constexpr auto payload = [] consteval {
                            std::meta::info last{};
                            for (auto m : std::meta::nonstatic_data_members_of(alternative, std::meta::access_context::current()))
                                if (!meta::has_annotation<skip>(m)) last = m;
                            return meta::sole_type_argument(std::meta::type_of(last));
                        }();
                        using E = [:payload:];
                        std::move(into).allow_external_subcommands().external_subcommand_value_parser<E>();
                    }
                } else {
                    command_builder child{
                            builder_text(meta::subcommand_name_of(alternative, style))};
                    apply_command_attr(child, meta::annotation_or<command_attr>(alternative), mode);
                    derive_args_into<alternative_type>(
                            child, meta::rename_all_of(alternative), mode, meta::annotation_or<command_attr>(alternative));
                    std::move(into).subcommand(std::move(child));
                }
            }
        }

        /**
         * \brief Shared body of command_of() and command_for_update().
         * \tparam T A command struct.
         * \param mode See derive_mode.
         */
        template<class T>
        [[nodiscard]] consteval command_spec derive_command(derive_mode mode) {
            constexpr command_attr attr = meta::annotation_or<command_attr>(^^T);

            // subcommand_name_of() already prefers `[[= clapp::cmd{.name = "..."}]]` and
            // falls back to the type name; using it here rather than a private copy is
            // what keeps the root's name and a nested alternative's name one rule.
            command_builder root{builder_text(meta::subcommand_name_of(^^T, naming::kebab))};
            apply_command_attr(root, attr, mode);
            derive_args_into<T>(root, meta::rename_all_of(^^T), mode, attr);
            return root.freeze();
        }

    }  // namespace detail

    // =======================================================================
    // The forward direction, public
    // =======================================================================

    /**
     * \brief Build the frozen command tree \p T describes (mirror of `from_matches`).
     * \tparam T Aggregate command struct.
     * \return Frozen tree (static storage via freeze; ADR-0005).
     * \throws Compile error for unsupported alternatives or invalid frozen schemas.
     * \code
     *     static constexpr clapp::command_spec spec = clapp::command_of<cli>();
     *     static_assert(spec.has_arg("verbose"));
     * \endcode
     * \note Name from `subcommand_name_of` (shared with alternatives). Constrained on
     *       `derivable_command` so missing parsers hit named `static_assert`s.
     */
    template<class T>
        requires derivable_command<T>
    [[nodiscard]] consteval command_spec command_of() {
        return detail::derive_command<T>(detail::derive_mode::parse);
    }

    /**
     * \brief Frozen tree for incremental parse (clap `override_required`).
     *
     * Unlike `command_of`, required / subcommand_required / arg_required_else_help
     * are all off so omitted fields keep the caller's values.
     * \tparam T Aggregate command struct.
     * \return Frozen tree.
     * \note Without this, `update` would demand every required arg each call.
     */
    template<class T>
        requires derivable_command<T>
    [[nodiscard]] consteval command_spec command_for_update() {
        return detail::derive_command<T>(detail::derive_mode::update);
    }

    // =======================================================================
    // Entry points — the non-exiting half
    // =======================================================================

    /**
     * \brief Parse \p raw into \p T without exiting.
     * \tparam T A `parsable_command`.
     * \param raw Command line (includes `argv[0]` unless multicall / no_binary_name).
     * \return Filled struct or error (help/version are control flow; use
     *         `use_stderr`/`exit_code`).
     * \code
     *     const auto got = clapp::try_parse_from<cli>(clapp::raw_args::from_args());
     * \endcode
     * \note Spec is function-local `static constexpr`.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<T, error> try_parse_from(const raw_args& raw) {
        static constexpr command_spec spec = command_of<T>();

        std::expected<arg_matches, error> matches = detail::run_parser(spec, raw);
        if (!matches.has_value()) return std::unexpected(std::move(matches.error()));
        auto parsed = from_matches<T>(matches.value());
        if (!parsed) parsed.error().set_output(spec.get_styles(), spec.get_color());
        return parsed;
    }

    /**
     * \brief try_parse_from() over a span of clapp::os_str.
     *
     * \tparam T A clapp::parsable_command.
     * \param args The command line, `argv[0]` first.
     * \return As try_parse_from(const raw_args&).
     *
     * \see try_parse_from(std::initializer_list<os_str>) — the overload that exists so
     *      that a *braced* argument list is not ambiguous against this one.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<T, error> try_parse_from(std::span<const os_str> args) {
        return try_parse_from<T>(raw_args{args});
    }

    /**
     * \brief try_parse_from over a braced list of `os_str`.
     * \tparam T A `parsable_command`.
     * \param args Command line, `argv[0]` first.
     * \return As `try_parse_from(const raw_args&)`.
     * \warning **Not convenience; fixes a real ambiguity.** Both `span<const os_str>`
     *          (P2447) and `raw_args` convert from `initializer_list`, equal rank on
     *          clang/libc++ (ambiguous) while GCC/libstdc++ happens to accept. An
     *          exact `initializer_list` parameter wins. Same overload on
     *          parse_from / try_update_from / update_from, each with a
     *          `!parsable_command` diagnosing counterpart.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<T, error> try_parse_from(std::initializer_list<os_str> args) {
        return try_parse_from<T>(raw_args(args));
    }

    /**
     * \brief try_parse_from over `main`'s parameters.
     * \tparam T A `parsable_command`.
     * \param argc Argument count.
     * \param argv Argument vector.
     * \return As `try_parse_from(const raw_args&)`.
     * \note `const native_char* const*`; plain `char**` binds when native is `char`.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<T, error> try_parse(int argc, const native_char* const* argv) {
        return try_parse_from<T>(raw_args{argc, argv});
    }

    // =======================================================================
    // Entry points — the exiting half
    // =======================================================================

    /**
     * \brief Parse \p raw into \p T, or print and exit.
     * \tparam T A `parsable_command`.
     * \param raw Command line.
     * \return Filled struct; does not return on failure.
     * \note Help/version -> stdout/0; errors -> stderr/2 (ADR-0001 via the error).
     * \warning Calls `std::exit`; no automatic destructors on the failure path.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] T parse_from(const raw_args& raw) {
        std::expected<T, error> got = try_parse_from<T>(raw);
        if (!got.has_value()) detail::report_and_exit(got.error(), command_of<T>());
        return std::move(got.value());
    }

    /**
     * \brief parse_from() over a span of clapp::os_str.
     * \tparam T A clapp::parsable_command.
     * \param args The command line, `argv[0]` first.
     * \return The filled struct. Does not return otherwise.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] T parse_from(std::span<const os_str> args) {
        return parse_from<T>(raw_args{args});
    }

    /**
     * \brief parse_from() over a braced list of clapp::os_str.
     * \tparam T A clapp::parsable_command.
     * \param args The command line, `argv[0]` first.
     * \return The filled struct. Does not return otherwise.
     * \warning Load-bearing, not convenience — see
     *          try_parse_from(std::initializer_list<os_str>).
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] T parse_from(std::initializer_list<os_str> args) {
        return parse_from<T>(raw_args(args));
    }

    /**
     * \brief Parse from `main` arguments (exiting).
     * \tparam T A `parsable_command`.
     * \param argc Argument count.
     * \param argv Argument vector.
     * \return Filled struct; does not return on failure.
     * \code
     *     int main(int argc, char** argv) {
     *         const cli args = clapp::parse<cli>(argc, argv);
     *     }
     * \endcode
     * \note Shares the name with M3 `parse(spec, raw)`; overload sets do not collide
     *       (explicit template args vs non-template; deduction fails the reverse).
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] T parse(int argc, const native_char* const* argv) {
        return parse_from<T>(raw_args{argc, argv});
    }

    template<class T> requires parsable_command<T>
    [[nodiscard]] T parse() { return parse_from<T>(raw_args::from_args()); }

    template<class T> requires parsable_command<T>
    [[nodiscard]] std::expected<T, error> try_parse() { return try_parse_from<T>(raw_args::from_args()); }

    template<class T> requires (!parsable_command<T>)
    T parse() { detail::reject_command<T>(); }

    template<class T> requires (!parsable_command<T>)
    std::expected<T, error> try_parse() { detail::reject_command<T>(); }

#if defined(_WIN32)
    template<class T> requires parsable_command<T>
    [[nodiscard]] T parse(int argc, const char* const* argv) { return parse_from<T>(raw_args{argc, argv}); }
    template<class T> requires parsable_command<T>
    [[nodiscard]] std::expected<T, error> try_parse(int argc, const char* const* argv) { return try_parse_from<T>(raw_args{argc, argv}); }
    template<class T> requires (!parsable_command<T>)
    T parse(int, const char* const*) { detail::reject_command<T>(); }
    template<class T> requires (!parsable_command<T>)
    std::expected<T, error> try_parse(int, const char* const*) { detail::reject_command<T>(); }
#endif

    // =======================================================================
    // Entry points — the incremental half
    // =======================================================================

    /**
     * \brief Update \p out from \p raw without exiting (`command_for_update`).
     * \tparam T A `parsable_command`.
     * \param out Object updated in place.
     * \param raw Command line.
     * \return Void or first error.
     * \warning Not transactional; see `update_from_matches`.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<void, error> try_update_from(T& out, const raw_args& raw) {
        static constexpr command_spec spec = command_for_update<T>();

        std::expected<arg_matches, error> matches = detail::run_parser(spec, raw);
        if (!matches.has_value()) return std::unexpected(std::move(matches.error()));
        auto updated = update_from_matches<T>(out, matches.value());
        if (!updated) updated.error().set_output(spec.get_styles(), spec.get_color());
        return updated;
    }

    /**
     * \brief try_update_from() over a span of clapp::os_str.
     * \tparam T A clapp::parsable_command.
     * \param out  The object to update, in place.
     * \param args The command line, `argv[0]` first.
     * \return Nothing, or the first error.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<void, error> try_update_from(T& out, std::span<const os_str> args) {
        return try_update_from<T>(out, raw_args{args});
    }

    /**
     * \brief try_update_from() over a braced list of clapp::os_str.
     * \tparam T A clapp::parsable_command.
     * \param out  The object to update, in place.
     * \param args The command line, `argv[0]` first.
     * \return Nothing, or the first error.
     * \warning Load-bearing, not convenience — see
     *          try_parse_from(std::initializer_list<os_str>).
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<void, error> try_update_from(T& out,
                                                             std::initializer_list<os_str> args) {
        return try_update_from<T>(out, raw_args(args));
    }

    /**
     * \brief try_update_from() over `main`'s parameters.
     * \tparam T A clapp::parsable_command.
     * \param out  The object to update, in place.
     * \param argc `main`'s argument count.
     * \param argv `main`'s argument vector.
     * \return Nothing, or the first error.
     */
    template<class T>
        requires parsable_command<T>
    [[nodiscard]] std::expected<void, error>
    try_update(T& out, int argc, const native_char* const* argv) {
        return try_update_from<T>(out, raw_args{argc, argv});
    }

    /**
     * \brief try_update_from(), printing and leaving the process on failure.
     * \tparam T A clapp::parsable_command.
     * \param out The object to update, in place.
     * \param raw The command line.
     * \warning Calls `std::exit`; see the warning on parse_from().
     */
    template<class T>
        requires parsable_command<T>
    void update_from(T& out, const raw_args& raw) {
        const std::expected<void, error> outcome = try_update_from<T>(out, raw);
        if (!outcome.has_value()) detail::report_and_exit(outcome.error(), command_for_update<T>());
    }

    /**
     * \brief update_from() over a span of clapp::os_str.
     * \tparam T A clapp::parsable_command.
     * \param out  The object to update, in place.
     * \param args The command line, `argv[0]` first.
     */
    template<class T>
        requires parsable_command<T>
    void update_from(T& out, std::span<const os_str> args) {
        update_from<T>(out, raw_args{args});
    }

    /**
     * \brief update_from() over a braced list of clapp::os_str.
     * \tparam T A clapp::parsable_command.
     * \param out  The object to update, in place.
     * \param args The command line, `argv[0]` first.
     * \warning Load-bearing, not convenience — see
     *          try_parse_from(std::initializer_list<os_str>).
     */
    template<class T>
        requires parsable_command<T>
    void update_from(T& out, std::initializer_list<os_str> args) {
        update_from<T>(out, raw_args(args));
    }

    /**
     * \brief update_from() over `main`'s parameters.
     * \tparam T A clapp::parsable_command.
     * \param out  The object to update, in place.
     * \param argc `main`'s argument count.
     * \param argv `main`'s argument vector.
     */
    template<class T>
        requires parsable_command<T>
    void update(T& out, int argc, const native_char* const* argv) {
        update_from<T>(out, raw_args{argc, argv});
    }

    // =======================================================================
    // The diagnosing overloads
    // =======================================================================
    //
    // One per entry point, constrained on the negation. They exist because a
    // requires-clause that is not satisfied reports `no matching function for call to
    // parse<cli>(int&, char**&)` and names nothing the user can act on; reaching a
    // function body means reaching a `static_assert` whose message carries the field,
    // its type and the fix. Each one is a single call to
    // clapp::detail::reject_command(), which is `[[noreturn]]` — which is also why none
    // of them needs a `return` statement for a `T` that may not be constructible at all.

    /**
     * \brief The diagnosing counterpart of try_parse_from(const raw_args&).
     * \tparam T The type the caller supplied.
     * \param raw Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<T, error> try_parse_from(const raw_args& raw) {
        static_cast<void>(raw);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of try_parse_from(std::span<const os_str>).
     * \tparam T The type the caller supplied.
     * \param args Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<T, error> try_parse_from(std::span<const os_str> args) {
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief Diagnosing counterpart of try_parse_from(initializer_list).
     * \tparam T Caller-supplied type.
     * \param args Ignored.
     * \return Never returns.
     * \warning Required: without it a bad `T` with braces reintroduces the span /
     *          raw_args ambiguity instead of the field-naming `static_assert`.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<T, error> try_parse_from(std::initializer_list<os_str> args) {
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of try_parse().
     * \tparam T The type the caller supplied.
     * \param argc Ignored.
     * \param argv Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<T, error> try_parse(int argc, const native_char* const* argv) {
        static_cast<void>(argc);
        static_cast<void>(argv);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of parse_from(const raw_args&).
     * \tparam T The type the caller supplied.
     * \param raw Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    T parse_from(const raw_args& raw) {
        static_cast<void>(raw);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of parse_from(std::span<const os_str>).
     * \tparam T The type the caller supplied.
     * \param args Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    T parse_from(std::span<const os_str> args) {
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of parse_from(std::initializer_list<os_str>).
     * \tparam T The type the caller supplied.
     * \param args Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    T parse_from(std::initializer_list<os_str> args) {
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of parse().
     * \tparam T The type the caller supplied.
     * \param argc Ignored.
     * \param argv Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    T parse(int argc, const native_char* const* argv) {
        static_cast<void>(argc);
        static_cast<void>(argv);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of try_update_from(T&, const raw_args&).
     * \tparam T The type the caller supplied.
     * \param out Ignored.
     * \param raw Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<void, error> try_update_from(T& out, const raw_args& raw) {
        static_cast<void>(out);
        static_cast<void>(raw);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of
     *        try_update_from(T&, std::span<const os_str>).
     * \tparam T The type the caller supplied.
     * \param out  Ignored.
     * \param args Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<void, error> try_update_from(T& out, std::span<const os_str> args) {
        static_cast<void>(out);
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of
     *        try_update_from(T&, std::initializer_list<os_str>).
     * \tparam T The type the caller supplied.
     * \param out  Ignored.
     * \param args Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<void, error> try_update_from(T& out, std::initializer_list<os_str> args) {
        static_cast<void>(out);
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of try_update().
     * \tparam T The type the caller supplied.
     * \param out  Ignored.
     * \param argc Ignored.
     * \param argv Ignored.
     * \return Never returns.
     */
    template<class T>
        requires(!parsable_command<T>)
    std::expected<void, error> try_update(T& out, int argc, const native_char* const* argv) {
        static_cast<void>(out);
        static_cast<void>(argc);
        static_cast<void>(argv);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of update_from(T&, const raw_args&).
     * \tparam T The type the caller supplied.
     * \param out Ignored.
     * \param raw Ignored.
     */
    template<class T>
        requires(!parsable_command<T>)
    void update_from(T& out, const raw_args& raw) {
        static_cast<void>(out);
        static_cast<void>(raw);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of update_from(T&, std::span<const os_str>).
     * \tparam T The type the caller supplied.
     * \param out  Ignored.
     * \param args Ignored.
     */
    template<class T>
        requires(!parsable_command<T>)
    void update_from(T& out, std::span<const os_str> args) {
        static_cast<void>(out);
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of
     *        update_from(T&, std::initializer_list<os_str>).
     * \tparam T The type the caller supplied.
     * \param out  Ignored.
     * \param args Ignored.
     */
    template<class T>
        requires(!parsable_command<T>)
    void update_from(T& out, std::initializer_list<os_str> args) {
        static_cast<void>(out);
        static_cast<void>(args);
        detail::reject_command<T>();
    }

    /**
     * \brief The diagnosing counterpart of update().
     * \tparam T The type the caller supplied.
     * \param out  Ignored.
     * \param argc Ignored.
     * \param argv Ignored.
     */
    template<class T>
        requires(!parsable_command<T>)
    void update(T& out, int argc, const native_char* const* argv) {
        static_cast<void>(out);
        static_cast<void>(argc);
        static_cast<void>(argv);
        detail::reject_command<T>();
    }

    namespace detail {

        // ---------------------------------------------------------------------
        // Compile-time contracts.
        // ---------------------------------------------------------------------

        /**
         * Message builders asserted with literals only (trap 11). Bind args to named
         * objects first (GCC constexpr wrong-code with string-literal args).
         */
        inline constexpr std::string_view probe_field = "bad_field";
        /** \brief Synthetic type name used by compile-time diagnostics checks. */
        inline constexpr std::string_view probe_type = "nope";

        static_assert(no_value_parser_message(probe_field, probe_type) ==
                      "clapp: field 'bad_field' of type 'nope' has no value_parser. "
                      "Specialize clapp::value_parser<> for it, or mark the field "
                      "[[= clapp::skip{}]].");

        // Substring queries go through message_mentions(), never through
        // `std::string_view::find`; see the \warning there for the ubsan reason.
        static_assert(message_mentions(not_storable_message(probe_field, probe_type),
                                       "field 'bad_field'"));
        static_assert(message_mentions(not_storable_message(probe_field, probe_type),
                                       "[[= clapp::skip{}]]"));

        /** \brief Synthetic command name used by compile-time diagnostics checks. */
        inline constexpr std::string_view probe_command = "demo";
        /** \brief Synthetic entity category used by compile-time diagnostics checks. */
        inline constexpr std::string_view probe_what = "long option";
        /** \brief Synthetic spelling used by compile-time diagnostics checks. */
        inline constexpr std::string_view probe_name = "verbose";
        /** \brief First synthetic owner used by duplicate-name diagnostics checks. */
        inline constexpr std::string_view probe_first = "verbose";
        /** \brief Second synthetic owner used by duplicate-name diagnostics checks. */
        inline constexpr std::string_view probe_second = "loud";

        static_assert(message_mentions(duplicate_name_message(probe_command,
                                                              probe_what,
                                                              probe_name,
                                                              probe_first,
                                                              probe_second),
                                       "the fields 'verbose' and 'loud' both derive the long "
                                       "option 'verbose'"));

        // ADR-0008's standard, asserted rather than described: both offending fields are
        // in the message. `tests/units/meta/compile_fail/duplicate_long_option_test.cpp`
        // asserts the same thing on the compiler's real output.
        static_assert(message_mentions(duplicate_name_message(probe_command,
                                                              probe_what,
                                                              probe_name,
                                                              probe_first,
                                                              probe_second),
                                       "'loud'"));

        static_assert(message_mentions(not_flattenable_message(probe_field, probe_type),
                                       "field 'bad_field'"));
        static_assert(message_mentions(not_flattenable_message(probe_field, probe_type),
                                       "[[= clapp::flatten{}]]"));

        /** `message_mentions` must answer false, or positive asserts are vacuous. */
        static_assert(!message_mentions(no_value_parser_message(probe_field, probe_type),
                                        "field 'other_field'"));
        static_assert(!message_mentions(probe_field, "bad_field_and_more"));

        /** Exactly two derive modes (parse / update). */
        static_assert(std::meta::enumerators_of(^^derive_mode).size() == 2);

    }  // namespace detail

}  // namespace clapp

#undef CLAPP_DETAIL_DIAGNOSTIC_ASSERT
