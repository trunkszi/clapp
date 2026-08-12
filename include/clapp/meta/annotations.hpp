/**
 * \file
 * \brief Annotation DSL for clapp's reflection-driven derive layer.
 */

#pragma once

#include <clapp/detail/std_meta.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string_view>
#include <variant>
#include <vector>

namespace clapp {
    /**
     * \brief Fixed-capacity structural string for P3394 annotation members.
     *
     * `std::string_view`, `inplace_vector`, and `const char*` all fail structural
     * rules or `extract`; a public `char` array plus length works.
     *
     * \tparam Cap Capacity in characters, excluding the terminator.
     * \note `freeze()` lifts payloads via `define_static_string`, so the binary
     *       keeps only the true length.
     */
    template<std::size_t Cap>
    struct cstr {
        /**
         * \name Structural storage
         * Public only because structural types cannot have private data members.
         * \{
         */
        char chars[Cap + 1]{};  /**< Inline character storage, followed by a null terminator. */
        std::size_t length = 0; /**< Number of payload characters currently stored. */
        /** \} */

        constexpr cstr() = default;

        /**
         * \brief Construct from a string literal.
         * \tparam N Literal length including the terminator.
         * \note Not `explicit`: designated initializers need
         *       `.help = "..."` conversion. Pinned by `dsl_ergonomics_hold`.
         */
        template<std::size_t N>
            requires(N <= Cap + 1)
        consteval cstr(const char (&literal)[N]) : length(N - 1) {
            std::ranges::copy(literal | std::views::take(N - 1), chars);
        }

        /** \brief Return the maximum payload length. */
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Cap; }

        /** \brief Return the current payload length. */
        [[nodiscard]] constexpr std::size_t size() const noexcept { return length; }
        /** \brief Return whether the payload is empty. */
        [[nodiscard]] constexpr bool empty() const noexcept { return length == 0; }

        /** \brief Return an iterator to the first character. */
        [[nodiscard]] constexpr const char* begin() const noexcept { return chars; }
        /** \brief Return an iterator one past the final payload character. */
        [[nodiscard]] constexpr const char* end() const noexcept { return chars + length; }
        /** \brief Return the address of the character storage. */
        [[nodiscard]] constexpr const char* data() const noexcept { return chars; }

        /** \brief Return the character at index \p i. */
        [[nodiscard]] constexpr char operator[](std::size_t i) const noexcept { return chars[i]; }

        /**
         * \brief Borrow the payload as a view.
         * \warning Points into `*this`. Lift before crossing a consteval boundary:
         *          `std::define_static_string(c.view())`. Returning unlifted from a
         *          consteval owner dangles (GCC: use after deallocation).
         */
        [[nodiscard]] constexpr std::string_view view() const noexcept { return {chars, length}; }

        /** \brief Compare capacity, length, and stored characters. */
        [[nodiscard]] constexpr bool operator==(const cstr&) const = default;
    };

    /**
     * \brief Three-state flag: unspecified vs explicit yes/no (for type inference).
     */
    enum class tri : signed char {
        infer = -1, /**< Derive the value from surrounding type information. */
        no    = 0,  /**< Explicitly disable the property. */
        yes   = 1,  /**< Explicitly enable the property. */
    };

    /** \brief What to do when an argument is matched. Mirrors clap's `ArgAction`. */
    enum class action : unsigned char {
        infer,      /**< Deduce from the field type; see the deduction table in the docs. */
        set,        /**< Store the value; a repeat is a conflict unless args_override_self. */
        append,     /**< Store the value; a repeat appends. */
        set_true,   /**< Presence means `true`. */
        set_false,  /**< Presence means `false`. */
        count,      /**< Count occurrences, e.g. `-vvv` yields 3. */
        help,       /**< Print help and exit. */
        help_short, /**< Print short help and exit. */
        help_long,  /**< Print long help and exit. */
        version,    /**< Print version and exit. */
    };

    /** \brief How a field identifier is rewritten into an option or value name. */
    enum class naming : unsigned char {
        kebab,           /**< `output_file` -> `output-file` (default for options) */
        snake,           /**< `output_file` -> `output_file` */
        camel,           /**< `output_file` -> `outputFile` */
        pascal,          /**< `output_file` -> `OutputFile` */
        screaming_snake, /**< `output_file` -> `OUTPUT_FILE` (default for env vars) */
        lower,           /**< `output_file` -> `outputfile` */
        upper,           /**< `output_file` -> `OUTPUTFILE` */
        verbatim,        /**< Used as written. */
    };

    /**
     * \brief Shell-completion hint for a value. Mirrors clap's `ValueHint`.
     * \note Recorded but unused until the completion generator lands; see roadmap M6+.
     */
    enum class value_hint : unsigned char {
        unknown,                /**< No completion category is known. */
        other,                  /**< A value outside the predefined categories. */
        any_path,               /**< Any filesystem path. */
        file_path,              /**< A path expected to identify a file. */
        dir_path,               /**< A path expected to identify a directory. */
        executable_path,        /**< A path expected to identify an executable. */
        command_name,           /**< A command name discoverable on the system. */
        command_string,         /**< A shell command represented as one string. */
        command_with_arguments, /**< A command followed by its arguments. */
        username,               /**< A system user name. */
        hostname,               /**< A network host name. */
        url,                    /**< A URL. */
        email_address,          /**< An email address. */
    };

    /**
     * \brief Command-level annotation. Attach to a struct or class.
     *        Counterpart of clap's `#[command(...)]`.
     */
    struct command_attr {
        cstr<64> name{};              /**< Empty: derive from the type name, kebab-cased. */
        cstr<32> version{};           /**< Version displayed by the generated version action. */
        cstr<256> about{};            /**< Short command description. */
        cstr<1024> long_about{};      /**< Long command description. */
        cstr<128> author{};           /**< Author line included in help output. */
        cstr<512> after_help{};       /**< Text rendered after the generated help body. */
        cstr<512> before_help{};      /**< Text rendered before the generated help body. */
        cstr<1024> help_template{};   /**< Custom help template, or empty for the default. */
        cstr<64> next_help_heading{}; /**< Heading assigned to subsequently derived arguments. */

        naming rename_all = naming::kebab; /**< Naming convention for derived argument names. */

        /**
         * \brief clap's `#[command(rename_all_env)]`.
         * \warning **Recorded and never read.** `derive_arg()` does not derive env
         *          names; no bare `.env` spelling exists. Kept for designated-init
         *          member order; removing it is source-breaking.
         */
        naming rename_all_env = naming::screaming_snake;

        bool propagate_version = false; /**< Give subcommands the parent version when absent. */
        bool arg_required_else_help = false; /**< Show help when no argument is supplied. */
        bool subcommand_required    = false; /**< Reject invocations that select no subcommand. */
        bool infer_subcommands      = false; /**< Match unambiguous subcommand name prefixes. */
        bool infer_long_args        = false; /**< Match unambiguous long-option name prefixes. */
        bool allow_external_subcommands = false; /**< Capture an otherwise unknown subcommand. */
        bool multicall = false; /**< Interpret the executable name as a subcommand selector. */
        bool disable_help_flag    = false; /**< Omit the generated help flag. */
        bool disable_version_flag = false; /**< Omit the generated version flag. */
    };

    /**
     * \brief Argument-level annotation on a data member (clap's `#[arg(...)]`).
     *
     * Defaults mean "unspecified"; deduction fills them.
     * \warning Member order is part of the interface: designated initializers must
     *          follow declaration order. **Reordering is source-breaking for every
     *          downstream CLI definition.**
     */
    struct arg_attr {
        /**
         * \name Identity — what the option is called
         * \{
         */
        char short_ = '\0'; /**< `'\0'`: no short option. */
        bool auto_short =
                false;    /**< Take the first letter of the long name; clap's `#[arg(short)]`. */
        cstr<64> long_{}; /**< Empty: derive using clapp::command_attr::rename_all. */
        bool no_long      = false; /**< Suppress the long option entirely. */
        std::size_t index = 0;     /**< Non-zero makes this a positional at that 1-based index. */
        /** \} */

        /**
         * \name Behavior — what happens when it matches
         * \{
         */
        action act   = action::infer; /**< Match action, inferred from the field type by default. */
        tri required = tri::infer;    /**< Whether the argument is required. */
        value_hint hint = value_hint::unknown; /**< Completion category for parsed values. */
        /** \} */

        /**
         * \name Help presentation
         * \{
         */
        cstr<256> help{};                /**< Short help text. */
        cstr<1024> long_help{};          /**< Long help text. */
        cstr<64> value_name{};           /**< Placeholder shown for a value. */
        cstr<64> help_heading{};         /**< Help section containing this argument. */
        std::size_t display_order = 999; /**< Relative order within generated help. */
        /** \} */

        /**
         * \name Value sources and parsing
         * \{
         */
        cstr<64> env{}; /**< Environment variable used as a fallback value source. */

        /**
         * \brief Value when absent (clap's `#[arg(default_value)]`).
         * \warning **Outranks a default member initializer on the same field.**
         *          Annotation is the only compile-time-readable channel (help +
         *          `required`); when both are set, the annotation wins.
         * \note Only a user-written annotation wins. Parser-injected defaults
         *       (e.g. `"false"` for absent `set_true`) still yield to the
         *       initializer — see `wins_over_initializer()`.
         */
        cstr<128> default_value{};

        cstr<128> default_missing_value{}; /**< Value used when an option appears without one. */
        char delimiter = '\0'; /**< Delimiter for splitting one token into multiple values. */
        /** \} */

        /**
         * \name Grouping
         * \{
         */
        cstr<64> group{}; /**< Identifier of the argument group to join. */
        /** \} */

        /**
         * \name Behavior flags
         * \{
         */
        bool global    = false; /**< Propagate matches between a command and its descendants. */
        bool hide      = false; /**< Omit the argument from generated help. */
        bool exclusive = false; /**< Conflict with every other non-required argument. */
        bool allow_hyphen_values    = false; /**< Accept flag-shaped tokens as values. */
        bool allow_negative_numbers = false; /**< Accept negative numeric tokens as values. */
        bool require_equals = false; /**< Require an attached option value after an equals sign. */
        bool trailing_var_arg = false; /**< Treat all following tokens as values once matched. */
        bool last        = false; /**< Require the positional after the double-dash delimiter. */
        bool ignore_case = false; /**< Compare possible values without ASCII case distinctions. */
        /** \} */
    };

    /**
     * \brief Enumerator-level annotation. Counterpart of clap's `#[value(...)]`.
     * \note Only needed to override the defaults; enumerators are picked up
     *       automatically via `std::meta::enumerators_of`, so no `ValueEnum`
     *       opt-in is required.
     */
    struct value_attr {
        cstr<64> name{};   /**< Override for the generated value name. */
        cstr<256> help{};  /**< Help text for the value. */
        bool hide = false; /**< Omit the value from generated possible-value lists. */
    };

    /**
     * \name Argument relations
     * Single-valued and stackable (P3394 repeats). Variadic `ids[N]` would make a
     * distinct type per N that `annotation_of` cannot match.
     * \{
     */
    /** \brief Declare an argument that cannot occur with the annotated argument. */
    struct conflicts_with {
        cstr<64> id{}; /**< Identifier of the conflicting argument. */
    };

    /** \brief Declare an argument required when the annotated argument occurs. */
    struct requires_arg {
        cstr<64> id{}; /**< Identifier of the required argument. */
    };

    /** \brief Declare an argument removed when the annotated argument occurs. */
    struct overrides_with {
        cstr<64> id{}; /**< Identifier of the overridden argument. */
    };

    /** \brief Make the argument required unless any named argument occurs. */
    struct required_unless_any {
        cstr<64> id{}; /**< Identifier of one alternative argument. */
    };

    /** \brief Make the argument required unless every named argument occurs. */
    struct required_unless_all {
        cstr<64> id{}; /**< Identifier of one jointly required alternative. */
    };

    /** \} */

    /**
     * \name Structural markers
     * Empty annotations that change how a member is interpreted rather than configure it.
     * \{
     */

    /** \brief Splice the nested struct's fields into the enclosing command. */
    struct flatten {};

    /** \brief Treat this `std::variant` member as the command's subcommand set. */
    struct subcommand {};

    /** \brief Exclude from parsing; the field is value-initialized. */
    struct skip {};

    /** \brief Read from an ancestor command's `global` argument. */
    struct from_global {};

    /** \brief Catch-all subcommand collecting every remaining argument. */
    struct external_subcommand {};

    /** \} */

    /**
     * \name Convenience aliases
     * Make call sites read close to their clap counterparts.
     * \{
     */
    /** \brief Short spelling for clapp::arg_attr. */
    using arg = arg_attr;
    /** \brief Short spelling for clapp::command_attr. */
    using cmd = command_attr;
    /** \brief Short spelling for clapp::value_attr. */
    using value = value_attr;
    /** \} */

    namespace detail {
        /** Implicit `cstr` conversion the DSL needs; fails first if made `explicit`. */
        static_assert(std::is_convertible_v<const char (&)[6], cstr<64>>,
                      "clapp: cstr's literal constructor must stay implicit, otherwise "
                      "designated initializers such as [[= clapp::arg{.help = \"...\"}]] "
                      "no longer compile.");

        inline constexpr bool dsl_ergonomics_hold =
                arg_attr{.long_ = "output-file", .help = "Where to write"}.long_.view() ==
                std::string_view{"output-file"};
        static_assert(dsl_ergonomics_hold);
    }  // namespace detail

    namespace meta {
        /**
         * \brief Read the annotation of type \p A on \p item, if any.
         *
         * Must use `constant_of` first (direct splice is rejected); compare
         * `type_of(c)` to `add_const(^^A)`; no string-literal pointers in values
         * (`extract` fails — that is why `cstr` exists).
         *
         * \warning Write `add_const(^^A)`, **not** `^^const A`. Inside a template,
         *          `^^const A` silently fails on clang-p2996 (annotations vanish,
         *          no diagnostic). GCC accepts both. Outside a template both work;
         *          `remove_const(type_of(c)) == ^^A` is also portable.
         * \tparam A Annotation type to look for.
         * \param item Reflection of a type, data member, or enumerator.
         * \return The value, or `nullopt` if absent.
         */
        template<class A>
        consteval std::optional<A> annotation_of(std::meta::info item) {
            auto constants =
                    std::meta::annotations_of(item) | std::views::transform([](std::meta::info a) {
                        return std::meta::constant_of(a);
                    });

            auto it = std::ranges::find_if(constants, [](std::meta::info c) {
                return std::meta::type_of(c) == std::meta::add_const(^^A);
            });

            if (it == std::ranges::end(constants)) return std::nullopt;
            return std::meta::extract<A>(*it);
        }

        /**
         * \brief Whether \p item carries an annotation of type \p A.
         * \tparam A Typically one of the empty structural markers: clapp::flatten,
         *         clapp::subcommand, clapp::skip, clapp::from_global.
         */
        template<class A>
        consteval bool has_annotation(std::meta::info item) {
            return annotation_of<A>(item).has_value();
        }

        /**
         * \brief Annotation of type \p A, or \p fallback.
         * \param item Reflection to inspect.
         * \param fallback Used when no annotation of type \p A is present.
         * \note Prefer over `value_or` in consteval: `_GLIBCXX_ASSERTIONS` makes
         *       `optional::operator->` non-constexpr.
         */
        template<class A>
        consteval A annotation_or(std::meta::info item, A fallback = {}) {
            std::optional<A> found = annotation_of<A>(item);
            return found.has_value() ? *found : fallback;
        }

        /**
         * \brief Every annotation of type \p A on \p item (for stackable relations).
         */
        template<class A>
        consteval std::vector<A> annotations_all_of(std::meta::info item) {
            return std::meta::annotations_of(item) | std::views::transform([](std::meta::info a) {
                       return std::meta::constant_of(a);
                   }) |
                   std::views::filter([](std::meta::info c) {
                       return std::meta::type_of(c) == std::meta::add_const(^^A);
                   }) |
                   std::views::transform(
                           [](std::meta::info c) { return std::meta::extract<A>(c); }) |
                   std::ranges::to<std::vector>();
        }

        /**
         * \brief Argument ids named by a stackable relation annotation.
         * \tparam A `conflicts_with`, `requires_arg`, `overrides_with`,
         *         `required_unless_any`, or `required_unless_all`.
         * \return Views into static storage.
         * \note Ids are lifted with `define_static_string`; unlifted `cstr::view()`
         *       after `extract` dangles.
         */
        template<class A>
        consteval std::vector<std::string_view> relation_ids_of(std::meta::info item) {
            return annotations_all_of<A>(item) | std::views::transform([](const A& relation) {
                       return std::string_view(std::define_static_string(relation.id.view()));
                   }) |
                   std::ranges::to<std::vector>();
        }

        /**
         * \brief Type-level view of a `std::variant` (subcommand alternatives).
         * \tparam V A `std::variant` specialization.
         */
        template<class V>
        struct variant_traits;

        /** \brief Expose the alternatives of a `std::variant` as reflected subcommands. */
        template<class... Ts>
        struct variant_traits<std::variant<Ts...>> {
            /** Number of alternatives, i.e. of subcommands. */
            static constexpr std::size_t size = sizeof...(Ts);

            /** The \p N -th alternative type. */
            template<std::size_t N>
                requires(N < sizeof...(Ts))
            using alternative = Ts...[N];

            /** Reflections of every alternative, for `command_of<T>()` to recurse into. */
            static constexpr auto reflections =
                    std::define_static_array(std::array<std::meta::info, sizeof...(Ts)>{^^Ts...});
        };
    }  // namespace meta
}  // namespace clapp
