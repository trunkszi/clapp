/**
 * \file
 * \brief clapp::command_builder and frozen clapp::command_spec (command tree).
 */

#pragma once

#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <cstdlib>
#include <functional>
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
     * \brief One boolean knob on a command (clap AppSettings). Order is load-bearing.
     * \note No Built/BinNameBuilt (freeze produces a built spec). Colour is a tri-state
     *       via color() / get_color(), not three independent knobs.
     */
    enum class command_setting : std::uint8_t {
        ignore_errors, /**< Keep parsing past an error. clap: `IgnoreErrors`. */
        allow_hyphen_values, /**< Every value-taking argument allows `-` values. */
        allow_negative_numbers, /**< Every value-taking argument allows negatives. */
        all_args_override_self, /**< A repeated argument replaces itself silently. */
        allow_missing_positional, /**< `prog [opt] <req>` may skip the optional one. */
        trailing_var_arg, /**< The final positional swallows the rest. */
        dont_delimit_trailing_values, /**< No delimiter splitting after `--`. */
        infer_long_args, /**< `--verb` may abbreviate `--verbose`. */
        infer_subcommands, /**< `cl` may abbreviate `clone`. */
        subcommand_required, /**< A subcommand must be given. */
        allow_external_subcommands, /**< Unknown subcommands are captured, not rejected. */
        multicall, /**< argv[0] selects the subcommand (busybox style). */
        subcommands_negate_reqs, /**< A subcommand excuses the parent's requirements. */
        args_negate_subcommands, /**< An argument excuses the subcommand requirement. */
        subcommand_precedence_over_arg, /**< A subcommand name wins over a pending value. */
        flatten_help, /**< Render subcommand help inline. */
        arg_required_else_help, /**< No arguments at all prints help instead. */
        next_line_help, /**< Every argument's help starts on its own line. */
        disable_colored_help, /**< Render help without styling. */
        disable_help_flag, /**< Do not inject `-h` / `--help`. */
        disable_help_subcommand, /**< Do not inject the `help` subcommand. */
        disable_version_flag, /**< Do not inject `-V` / `--version`. */
        propagate_version, /**< Subcommands inherit version() / long_version(). */
        hidden, /**< Omit this subcommand from the parent's help. */
        hide_possible_values, /**< Suppress `[possible values: ...]` everywhere. */
        help_expected, /**< Every argument must carry help text. */
        no_binary_name, /**< argv does not start with the program name. */
        color_auto, /**< Colour when the stream supports it. */
        color_always, /**< Colour unconditionally. */
        color_never, /**< Never colour. */
    };

    /** \brief How many distinct clapp::command_setting values exist. */
    inline constexpr std::size_t command_setting_count = 30;

    /** \brief Every clapp::command_setting, in bit order. Handy for exhaustive tests. */
    inline constexpr std::array<command_setting, command_setting_count> all_command_settings{
        command_setting::ignore_errors,
        command_setting::allow_hyphen_values,
        command_setting::allow_negative_numbers,
        command_setting::all_args_override_self,
        command_setting::allow_missing_positional,
        command_setting::trailing_var_arg,
        command_setting::dont_delimit_trailing_values,
        command_setting::infer_long_args,
        command_setting::infer_subcommands,
        command_setting::subcommand_required,
        command_setting::allow_external_subcommands,
        command_setting::multicall,
        command_setting::subcommands_negate_reqs,
        command_setting::args_negate_subcommands,
        command_setting::subcommand_precedence_over_arg,
        command_setting::flatten_help,
        command_setting::arg_required_else_help,
        command_setting::next_line_help,
        command_setting::disable_colored_help,
        command_setting::disable_help_flag,
        command_setting::disable_help_subcommand,
        command_setting::disable_version_flag,
        command_setting::propagate_version,
        command_setting::hidden,
        command_setting::hide_possible_values,
        command_setting::help_expected,
        command_setting::no_binary_name,
        command_setting::color_auto,
        command_setting::color_always,
        command_setting::color_never,
    };

    /**
     * \brief The spelling of \p setting, for diagnostics.
     * \param setting The knob to name.
     * \return A view into static storage; equals the enumerator's own spelling.
     */
    [[nodiscard]] constexpr std::string_view name_of(command_setting setting) noexcept {
        switch (setting) {
            case command_setting::ignore_errors:
                return "ignore_errors";
            case command_setting::allow_hyphen_values:
                return "allow_hyphen_values";
            case command_setting::allow_negative_numbers:
                return "allow_negative_numbers";
            case command_setting::all_args_override_self:
                return "all_args_override_self";
            case command_setting::allow_missing_positional:
                return "allow_missing_positional";
            case command_setting::trailing_var_arg:
                return "trailing_var_arg";
            case command_setting::dont_delimit_trailing_values:
                return "dont_delimit_trailing_values";
            case command_setting::infer_long_args:
                return "infer_long_args";
            case command_setting::infer_subcommands:
                return "infer_subcommands";
            case command_setting::subcommand_required:
                return "subcommand_required";
            case command_setting::allow_external_subcommands:
                return "allow_external_subcommands";
            case command_setting::multicall:
                return "multicall";
            case command_setting::subcommands_negate_reqs:
                return "subcommands_negate_reqs";
            case command_setting::args_negate_subcommands:
                return "args_negate_subcommands";
            case command_setting::subcommand_precedence_over_arg:
                return "subcommand_precedence_over_arg";
            case command_setting::flatten_help:
                return "flatten_help";
            case command_setting::arg_required_else_help:
                return "arg_required_else_help";
            case command_setting::next_line_help:
                return "next_line_help";
            case command_setting::disable_colored_help:
                return "disable_colored_help";
            case command_setting::disable_help_flag:
                return "disable_help_flag";
            case command_setting::disable_help_subcommand:
                return "disable_help_subcommand";
            case command_setting::disable_version_flag:
                return "disable_version_flag";
            case command_setting::propagate_version:
                return "propagate_version";
            case command_setting::hidden:
                return "hidden";
            case command_setting::hide_possible_values:
                return "hide_possible_values";
            case command_setting::help_expected:
                return "help_expected";
            case command_setting::no_binary_name:
                return "no_binary_name";
            case command_setting::color_auto:
                return "color_auto";
            case command_setting::color_always:
                return "color_always";
            case command_setting::color_never:
                return "color_never";
        }
        return "unknown";
    }

    /**
     * \brief Thirty command_setting knobs in one word (clap AppFlags; not arg_flags).
     * \note Structural; bit n is command_setting(n). Prefer members over raw #bits.
     */
    struct command_flags {
        /** \brief Storage. Bit `n` corresponds to `static_cast<command_setting>(n)`. */
        std::uint32_t bits = 0;

        /** \brief The single-bit mask for \p setting. */
        [[nodiscard]] static constexpr std::uint32_t bit_of(command_setting setting) noexcept {
            return std::uint32_t{1} << static_cast<unsigned>(setting);
        }

        /** \brief Whether \p setting is on. */
        [[nodiscard]] constexpr bool is_set(command_setting setting) const noexcept {
            return (bits & bit_of(setting)) != 0;
        }

        /** \brief Turn \p setting on, in place. */
        constexpr command_flags &set(command_setting setting) noexcept {
            bits |= bit_of(setting);
            return *this;
        }

        /** \brief Turn \p setting off, in place. */
        constexpr command_flags &unset(command_setting setting) noexcept {
            bits &= ~bit_of(setting);
            return *this;
        }

        /** \brief Turn \p setting on or off according to \p enable, in place. */
        constexpr command_flags &set(command_setting setting, bool enable) noexcept {
            return enable ? set(setting) : unset(setting);
        }

        /** \brief Union \p other into `*this`, in place. */
        constexpr command_flags &insert(command_flags other) noexcept {
            bits |= other.bits;
            return *this;
        }

        /** \brief A copy with \p setting forced to \p enable. */
        [[nodiscard]] constexpr command_flags with(command_setting setting,
                                                   bool enable) const noexcept {
            command_flags copy = *this;
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
        [[nodiscard]] friend constexpr command_flags operator|(command_flags lhs,
                                                               command_flags rhs) noexcept {
            return command_flags{.bits = lhs.bits | rhs.bits};
        }

        /** \brief Word equality. */
        [[nodiscard]] constexpr bool operator==(const command_flags &) const noexcept = default;
    };

    namespace detail {
        /**
         * \brief Fail the build while evaluating a command contract.
         * \param pieces Diagnostic fragments retained in the instantiation trace.
         * \note Calling abort during constant evaluation makes the expression invalid
         *       without relying on exception support.
         */
        [[noreturn]] consteval void fail(std::initializer_list<std::string_view> pieces) {
            static_cast<void>(pieces);
            std::abort();
        }

        /**
         * \brief Decimal digits of \p number for diagnostics (no constexpr format/to_string).
         * \param number Count to spell.
         * \return Decimal digit string.
         */
        [[nodiscard]] constexpr std::string spell_number(std::size_t number) {
            if (number == 0) return std::string{"0"};
            std::string digits;
            for (std::size_t rest = number; rest != 0; rest /= 10)
                digits.push_back(static_cast<char>('0' + static_cast<int>(rest % 10)));
            std::ranges::reverse(digits);
            return digits;
        }

        /**
         * \brief Join parent display name and child (`parent-child`, or child if empty).
         * \param parent Parent display name (empty under multicall).
         * \param child Child command name.
         * \return Hyphen-joined display name for version/help identity.
         * \note push_back only (ubsan-safe consteval string build).
         */
        [[nodiscard]] constexpr std::string join_display_name(std::string_view parent,
                                                              std::string_view child) {
            std::string out;
            for (const char byte: parent) out.push_back(byte);
            if (!out.empty()) out.push_back('-');
            for (const char byte: child) out.push_back(byte);
            return out;
        }

        /**
         * \brief Promoted prose: static pointer + length (#size 0 = absent; no null tests).
         */
        struct static_text {
            const char *data = nullptr; /**< Bytes in static storage. */
            std::size_t size = 0; /**< Length in bytes; 0 = absent. */
        };

        /**
         * \brief Promote an optional owning string into static storage.
         * \param text The string to lift, or `std::nullopt` for "absent".
         * \return A pointer/length pair; both are zero when \p text is absent.
         */
        [[nodiscard]] consteval static_text promote_text(const std::optional<std::string> &text) {
            if (!text.has_value()) return {};
            return static_text{.data = std::define_static_string(*text), .size = text->size()};
        }

        /**
         * \brief Promote an optional owning string into a clapp::arg_id.
         * \param name The token to lift, or `std::nullopt` for "absent".
         * \return An id that is empty() when \p name is absent, which is how every
         *         optional id in a frozen spec spells "unset".
         */
        [[nodiscard]] consteval arg_id promote_token(const std::optional<std::string> &name) {
            if (!name.has_value()) return arg_id{};
            return make_static_id(*name);
        }

        /**
         * \brief clapp's default palette, handed back by clapp::command_spec::get_styles()
         *        when a command did not choose one.
         */
        inline constexpr styles default_styles = styles::styled();

        /**
         * \brief The name freeze() gives the subcommand it injects for `cmd help SUBCOMMAND`.
         *
         * \note Named rather than spelled inline because two unrelated decisions read
         *       it and must agree: freeze() skips injection when the author already
         *       declared a subcommand by this name, and
         *       clapp::command_spec::has_visible_subcommands() refuses to count it as
         *       something worth advertising. clap ties the same two to the same literal
         *       `"help"`.
         */
        inline constexpr std::string_view default_help_subcommand_name = "help";

        /** \brief What kind of thing claims a spelling, for the diagnostic wording. */
        enum class owner_kind : std::uint8_t {
            argument, /**< An clapp::arg_builder of this command. */
            group, /**< An clapp::group_builder of this command. */
            subcommand, /**< A child clapp::command_builder. */
        };

        /**
         * \brief One claim on one spelling: `(token, who, what kind of who)`.
         *
         * clap's `enum Flag { Command(String, &str), Arg(String, &str) }`, plus a third
         * kind for groups so the id namespace can use the same machinery.
         *
         * \tparam Token `std::string_view` for long spellings and ids, `char` for
         *         short ones. A `char` key avoids materialising a one-byte string per
         *         short option, which would need storage that outlives the scan.
         */
        template<class Token>
        struct claim {
            Token token{}; /**< The spelling, without any leading dashes. */
            std::string_view owner; /**< Id or name of whatever claims it. */
            owner_kind kind = owner_kind::argument; /**< Which of the three it is. */
            std::size_t index = 0; /**< Position of the owner within its own list. */

            /**
             * \brief Whether two claims come from the same declaration.
             *
             * \note Identity is `(kind, index)`, not the owner *name*: the id
             *       namespace feeds this the very name it is checking, so two
             *       arguments that share an id would otherwise look like one
             *       argument claiming its id twice, and the collision would vanish.
             */
            [[nodiscard]] constexpr bool same_owner_as(const claim &other) const noexcept {
                return kind == other.kind && index == other.index;
            }
        };

        /**
         * \brief Sort \p claims and return the first spelling claimed by two owners.
         * \tparam Token string_view or char.
         * \param claims Claims sorted in place.
         * \return Colliding pair, or nullopt. Same owner twice is not a collision.
         */
        template<class Token>
        [[nodiscard]] constexpr std::optional<std::pair<claim<Token>, claim<Token> > >
        find_duplicate(std::vector<claim<Token> > &claims) {
            std::ranges::stable_sort(claims, std::less<>{}, &claim<Token>::token);
            // Raw loop rather than `views::adjacent<2>`: the answer is the *pair*, and
            // the loop stops at the first one, which a pipeline cannot express without
            // an extra `take_while`.
            for (std::size_t i = 1; i < claims.size(); ++i) {
                if (claims[i - 1].token != claims[i].token) continue;
                if (claims[i - 1].same_owner_as(claims[i])) continue;
                return std::pair{claims[i - 1], claims[i]};
            }
            return std::nullopt;
        }

        /** \brief The article-plus-noun a diagnostic uses for \p kind. */
        [[nodiscard]] constexpr std::string_view name_of(owner_kind kind) noexcept {
            switch (kind) {
                case owner_kind::argument:
                    return "the argument";
                case owner_kind::group:
                    return "the group";
                case owner_kind::subcommand:
                    return "the subcommand";
            }
            return "the argument";
        }
    } // namespace detail

    // -----------------------------------------------------------------------------
    // command_spec — the frozen command tree
    // -----------------------------------------------------------------------------

    /**
     * \brief Frozen command tree for .rodata (from command_builder::freeze).
     *
     * Fully resolved: help/version args, groups, positionals, globals. Structural
     * pointer+count layout; accessors return spans/optionals.
     */
    struct command_spec {
        /**
         * \name Identity
         * \{
         */
        arg_id name{}; /**< The command's name; also its id. */
        arg_id bin_name{}; /**< Executable name; empty = none. */
        arg_id display_name{}; /**< Name used in help; empty = fall back to #name. */
        char short_flag = '\0'; /**< `-C`-style subcommand flag; `'\0'` = none. */
        /**
         * \brief Whether external_subcommand_value_parser() was set.
         * \see has_external_subcommand_value_parser()
         * \note In alignment hole after short_flag (keeps command_spec 456 bytes).
         *       Bool avoids pointer null compares under ubsan consteval.
         */
        bool external_parser_present = false;
        arg_id long_flag{}; /**< `--config`-style subcommand flag; empty = none. */
        const alias_spec *alias_data = nullptr; /**< Alternative command names. */
        std::size_t alias_count = 0; /**< Number of name aliases. */
        const short_alias_spec *short_flag_alias_data = nullptr; /**< Short-flag aliases. */
        std::size_t short_flag_alias_count = 0; /**< How many. */
        const alias_spec *long_flag_alias_data = nullptr; /**< Long-flag aliases. */
        std::size_t long_flag_alias_count = 0; /**< How many. */
        arg_id version{}; /**< `-V` text; empty = none. */
        arg_id long_version{}; /**< `--version` text; empty = none. */
        /** \} */

        /**
         * \name Composition
         * \{
         */
        const arg_spec *arg_data = nullptr; /**< The arguments, in declaration order. */
        std::size_t arg_count = 0; /**< How many arguments. */
        const group_spec *group_data = nullptr; /**< The argument groups. */
        std::size_t group_count = 0; /**< How many groups. */
        const command_spec *sub_data = nullptr; /**< The subcommands, recursively frozen. */
        std::size_t sub_count = 0; /**< How many subcommands. */
        /**
         * \brief Parser applied to captured external subcommand arguments.
         * \note Read through #external_parser_present, never by testing for null. That
         *       flag is declared up in "Identity" so it can sit in that group's
         *       alignment hole; its semantics are documented there.
         */
        const parser_vtable *external_parser = nullptr;
        /** \} */

        /**
         * \name Help presentation
         * Length is the sentinel (0 = unset); never null-compare pointers (ubsan).
         * Empty prose collapses to unset except arg_spec help_heading_present.
         * \{
         */
        const char *author_text = nullptr; /**< `author()`. */
        std::size_t author_length = 0; /**< Bytes of #author_text; 0 = unset. */
        const char *about_text = nullptr; /**< `about()`. */
        std::size_t about_length = 0; /**< Bytes of #about_text; 0 = unset. */
        const char *long_about_text = nullptr; /**< `long_about()`. */
        std::size_t long_about_length = 0; /**< Bytes of #long_about_text; 0 = unset. */
        const char *before_help_text = nullptr; /**< `before_help()`. */
        std::size_t before_help_length = 0; /**< Bytes of #before_help_text; 0 = unset. */
        const char *before_long_help_text = nullptr; /**< `before_long_help()`. */
        std::size_t before_long_help_length = 0; /**< Bytes of the above; 0 = unset. */
        const char *after_help_text = nullptr; /**< `after_help()`. */
        std::size_t after_help_length = 0; /**< Bytes of #after_help_text; 0 = unset. */
        const char *after_long_help_text = nullptr; /**< `after_long_help()`. */
        std::size_t after_long_help_length = 0; /**< Bytes of the above; 0 = unset. */
        const char *override_usage_text = nullptr; /**< `override_usage()`. */
        std::size_t override_usage_length = 0; /**< Bytes of the above; 0 = unset. */
        const char *override_help_text = nullptr; /**< `override_help()`. */
        std::size_t override_help_length = 0; /**< Bytes of the above; 0 = unset. */
        const char *help_template_text = nullptr; /**< `help_template()`. */
        std::size_t help_template_length = 0; /**< Bytes of the above; 0 = unset. */
        const char *next_help_heading_text = nullptr; /**< Default section for arguments. */
        std::size_t next_help_heading_length = 0; /**< Bytes of the above; 0 = unset. */
        const char *subcommand_help_heading_text = nullptr; /**< Heading over subcommands. */
        std::size_t subcommand_help_heading_length = 0; /**< Bytes of the above; 0 = unset. */
        arg_id subcommand_value_name{}; /**< Placeholder in usage; empty = none. */
        std::size_t display_order = 999; /**< Sort key among sibling subcommands. */
        std::size_t term_width = 0; /**< Fixed wrap width; 0 = unset. */
        std::size_t max_term_width = 0; /**< Upper bound on the wrap width; 0 = unset. */
        /**
         * \brief The palette help and errors are rendered with.
         * \note **Never null.** A command that never called
         *       clapp::command_builder::styles() points straight at
         *       clapp::detail::default_styles, so get_styles() is a dereference rather
         *       than a null test — which is the point, since a null test does not fold
         *       under `-fsanitize=null`.
         */
        const styles *style_data = &detail::default_styles;
        /** \} */

        /** \brief The thirty boolean knobs, after global settings were folded in. */
        command_flags settings{};
        /** \brief The subset declared with global_setting(), which subcommands inherit. */
        command_flags global_settings{};

        // -- identity -------------------------------------------------------------

        /** \brief The command's name, which is also its id. */
        [[nodiscard]] constexpr std::string_view get_name() const noexcept { return name.name(); }

        /** \brief The command's id, for lookups that want byte comparison. */
        [[nodiscard]] constexpr arg_id get_id() const noexcept { return name; }

        /** \brief The executable name, if one was set or derived. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_bin_name() const noexcept {
            if (bin_name.empty()) return std::nullopt;
            return bin_name.name();
        }

        /** \brief The name help displays, if it differs from get_name(). */
        [[nodiscard]] constexpr std::optional<std::string_view> get_display_name() const noexcept {
            if (display_name.empty()) return std::nullopt;
            return display_name.name();
        }

        /** \brief The `-C`-style flag that selects this subcommand, if any. */
        [[nodiscard]] constexpr std::optional<char> get_short_flag() const noexcept {
            return short_flag == '\0' ? std::nullopt : std::optional<char>{short_flag};
        }

        /** \brief The `--config`-style flag that selects this subcommand, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_flag() const noexcept {
            if (long_flag.empty()) return std::nullopt;
            return long_flag.name();
        }

        /** \brief Every alternative command name, visible and hidden alike. */
        [[nodiscard]] constexpr std::span<const alias_spec> get_all_aliases() const noexcept {
            return {alias_data, alias_count};
        }

        /** \brief The command names help should list, as a lazy range. */
        [[nodiscard]] constexpr auto get_visible_aliases() const noexcept {
            return get_all_aliases() |
                   std::views::filter([](const alias_spec &a) { return a.visible; }) |
                   std::views::transform([](const alias_spec &a) { return a.name.name(); });
        }

        /** \brief The command names help should hide, as a lazy range (clap's naming). */
        [[nodiscard]] constexpr auto get_aliases() const noexcept {
            return get_all_aliases() |
                   std::views::filter([](const alias_spec &a) { return !a.visible; }) |
                   std::views::transform([](const alias_spec &a) { return a.name.name(); });
        }

        /**
         * \brief Command name plus visible aliases (materialized; no views::concat on libc++).
         */
        [[nodiscard]] constexpr std::vector<std::string_view> get_name_and_visible_aliases() const {
            std::vector<std::string_view> names;
            names.reserve(alias_count + 1U);
            names.push_back(name.name());
            names.append_range(get_visible_aliases());
            return names;
        }

        /** \brief Every short-flag alias, visible and hidden alike. */
        [[nodiscard]] constexpr std::span<const short_alias_spec>
        get_all_short_flag_aliases() const noexcept {
            return {short_flag_alias_data, short_flag_alias_count};
        }

        /** \brief The short-flag aliases help should list, as a lazy range of letters. */
        [[nodiscard]] constexpr auto get_visible_short_flag_aliases() const noexcept {
            return get_all_short_flag_aliases() |
                   std::views::filter([](const short_alias_spec &a) { return a.visible; }) |
                   std::views::transform([](const short_alias_spec &a) { return a.name; });
        }

        /** \brief Every long-flag alias, visible and hidden alike. */
        [[nodiscard]] constexpr std::span<const alias_spec>
        get_all_long_flag_aliases() const noexcept {
            return {long_flag_alias_data, long_flag_alias_count};
        }

        /** \brief The long-flag aliases help should list, as a lazy range. */
        [[nodiscard]] constexpr auto get_visible_long_flag_aliases() const noexcept {
            return get_all_long_flag_aliases() |
                   std::views::filter([](const alias_spec &a) { return a.visible; }) |
                   std::views::transform([](const alias_spec &a) { return a.name.name(); });
        }

        /**
         * \brief Whether \p candidate is this command's name or one of its aliases.
         * \param candidate The token seen on the command line.
         * \param include_hidden Whether hidden aliases count. They normally should: a
         *        hidden alias is hidden from *help*, not from the parser.
         */
        [[nodiscard]] constexpr bool aliases_to(std::string_view candidate,
                                                bool include_hidden = true) const noexcept {
            if (name.name() == candidate) return true;
            return std::ranges::any_of(get_all_aliases(), [&](const alias_spec &alias) {
                return (include_hidden || alias.visible) && alias.name.name() == candidate;
            });
        }

        /** \brief Whether \p letter is this command's short flag or one of its aliases. */
        [[nodiscard]] constexpr bool
        short_flag_aliases_to(char letter, bool include_hidden = true) const noexcept {
            if (letter == '\0') return false;
            if (short_flag == letter) return true;
            return std::ranges::any_of(
                get_all_short_flag_aliases(), [&](const short_alias_spec &alias) {
                    return (include_hidden || alias.visible) && alias.name == letter;
                });
        }

        /** \brief Whether \p spelling is this command's long flag or one of its aliases. */
        [[nodiscard]] constexpr bool
        long_flag_aliases_to(std::string_view spelling, bool include_hidden = true) const noexcept {
            if (!long_flag.empty() && long_flag.name() == spelling) return true;
            return std::ranges::any_of(get_all_long_flag_aliases(), [&](const alias_spec &alias) {
                return (include_hidden || alias.visible) && alias.name.name() == spelling;
            });
        }

        /** \brief The `-V` version text, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_version() const noexcept {
            if (version.empty()) return std::nullopt;
            return version.name();
        }

        /** \brief The `--version` text, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_version() const noexcept {
            if (long_version.empty()) return std::nullopt;
            return long_version.name();
        }

        // -- composition ----------------------------------------------------------

        /** \brief Every argument, in declaration order, including the injected ones. */
        [[nodiscard]] constexpr std::span<const arg_spec> get_arguments() const noexcept {
            return {arg_data, arg_count};
        }

        /** \brief The positional arguments, as a lazy range. */
        [[nodiscard]] constexpr auto get_positionals() const noexcept {
            return get_arguments() |
                   std::views::filter([](const arg_spec &a) { return a.is_positional(); });
        }

        /** \brief The value-taking named arguments, as a lazy range. clap's `get_opts`. */
        [[nodiscard]] constexpr auto get_opts() const noexcept {
            return get_arguments() | std::views::filter([](const arg_spec &a) {
                return a.is_takes_value_set() && !a.is_positional();
            });
        }

        /** \brief The named arguments that take no value, as a lazy range. */
        [[nodiscard]] constexpr auto get_flags() const noexcept {
            return get_arguments() | std::views::filter([](const arg_spec &a) {
                return !a.is_takes_value_set() && !a.is_positional();
            });
        }

        /** \brief The argument groups. */
        [[nodiscard]] constexpr std::span<const group_spec> get_groups() const noexcept {
            return {group_data, group_count};
        }

        /** \brief The subcommands, in declaration order. */
        [[nodiscard]] constexpr std::span<const command_spec> get_subcommands() const noexcept {
            return {sub_data, sub_count};
        }

        /** \brief Whether this command has any subcommands at all. */
        [[nodiscard]] constexpr bool has_subcommands() const noexcept { return sub_count != 0; }

        /**
         * \brief Whether any non-hidden subcommand is showable in help (excludes name `help`).
         * \return True if some subcommand other than the injected help is visible.
         * \note Name-based exclude matches clap; author-named `help` is also omitted.
         * \warning Sole predicate for Commands: / [COMMAND] / flatten_help in help and
         *          usage — keep those four call sites on this function.
         */
        [[nodiscard]] constexpr bool has_visible_subcommands() const noexcept {
            return std::ranges::any_of(get_subcommands(), [](const command_spec &sc) {
                return sc.get_name() != detail::default_help_subcommand_name && !sc.is_hide_set();
            });
        }

        /** \brief Whether this command has any positional argument. */
        [[nodiscard]] constexpr bool has_positionals() const noexcept {
            return std::ranges::any_of(get_arguments(),
                                       [](const arg_spec &a) { return a.is_positional(); });
        }

        /** \brief The argument with id \p id, or `nullptr`. */
        [[nodiscard]] constexpr const arg_spec *find_arg(std::string_view id) const noexcept {
            for (const arg_spec &candidate: get_arguments()) {
                if (candidate.get_id().name() == id) return &candidate;
            }
            return nullptr;
        }

        /** \brief The group with id \p id, or `nullptr`. */
        [[nodiscard]] constexpr const group_spec *find_group(std::string_view id) const noexcept {
            for (const group_spec &candidate: get_groups()) {
                if (candidate.get_id().name() == id) return &candidate;
            }
            return nullptr;
        }

        /**
         * \brief The subcommand whose name or alias is \p name, or `nullptr`.
         * \note Does **not** recurse; a subcommand's subcommands are its own business.
         */
        [[nodiscard]] constexpr const command_spec *
        find_subcommand(std::string_view name_or_alias) const noexcept {
            for (const command_spec &candidate: get_subcommands()) {
                if (candidate.aliases_to(name_or_alias)) return &candidate;
            }
            return nullptr;
        }

        /** \brief The subcommand selected by the short flag \p letter, or `nullptr`. */
        [[nodiscard]] constexpr const command_spec *
        find_short_subcommand(char letter) const noexcept {
            for (const command_spec &candidate: get_subcommands()) {
                if (candidate.short_flag_aliases_to(letter)) return &candidate;
            }
            return nullptr;
        }

        /** \brief The subcommand selected by the long flag \p spelling, or `nullptr`. */
        [[nodiscard]] constexpr const command_spec *
        find_long_subcommand(std::string_view spelling) const noexcept {
            for (const command_spec &candidate: get_subcommands()) {
                if (candidate.long_flag_aliases_to(spelling)) return &candidate;
            }
            return nullptr;
        }

        /**
         * \brief Whether an argument with id \p id exists (use in static_assert; not null find).
         */
        [[nodiscard]] constexpr bool has_arg(std::string_view id) const noexcept {
            return std::ranges::any_of(get_arguments(),
                                       [id](const arg_spec &a) { return a.get_id().name() == id; });
        }

        /** \brief Whether a group with id \p id exists. \see has_arg() */
        [[nodiscard]] constexpr bool has_group(std::string_view id) const noexcept {
            return std::ranges::any_of(
                get_groups(), [id](const group_spec &g) { return g.get_id().name() == id; });
        }

        /** \brief Whether a subcommand answers to \p name_or_alias. \see has_arg() */
        [[nodiscard]] constexpr bool has_subcommand(std::string_view name_or_alias) const noexcept {
            return std::ranges::any_of(get_subcommands(), [name_or_alias](const command_spec &c) {
                return c.aliases_to(name_or_alias);
            });
        }

        /** \brief Whether \p id names an argument or a group of this command. */
        [[nodiscard]] constexpr bool id_exists(std::string_view id) const noexcept {
            return has_arg(id) || has_group(id);
        }

        /** \brief Whether any argument answers to the short option \p letter. */
        [[nodiscard]] constexpr bool contains_short(char letter) const noexcept {
            return std::ranges::any_of(get_arguments(),
                                       [&](const arg_spec &a) { return a.matches_short(letter); });
        }

        /**
         * \brief Every subcommand name and alias, for "did you mean" suggestions.
         * \return A fresh vector; the caller usually feeds it to clapp::did_you_mean().
         */
        [[nodiscard]] constexpr std::vector<std::string_view> all_subcommand_names() const {
            std::vector<std::string_view> names;
            for (const command_spec &sc: get_subcommands()) {
                names.push_back(sc.get_name());
                for (const alias_spec &alias: sc.get_all_aliases())
                    names.push_back(alias.name.name());
            }
            return names;
        }

        /** \brief The group ids \p id belongs to. */
        [[nodiscard]] constexpr std::vector<std::string_view>
        groups_for_arg(std::string_view id) const {
            return get_groups() |
                   std::views::filter([id](const group_spec &g) { return g.contains(id); }) |
                   std::views::transform([](const group_spec &g) { return g.get_id().name(); }) |
                   std::ranges::to<std::vector>();
        }

        /**
         * \brief Arguments \p argument conflicts with (groups unrolled); skips unknown ids.
         * \param argument An argument of this command.
         * \return Pointers into #arg_data. Avoids find_arg()!=nullptr (ubsan consteval).
         */
        [[nodiscard]] constexpr std::vector<const arg_spec *>
        get_arg_conflicts_with(const arg_spec &argument) const {
            std::vector<const arg_spec *> found;
            const auto append_arg = [&](std::string_view id) {
                for (const arg_spec &candidate: get_arguments()) {
                    if (candidate.get_id().name() != id) continue;
                    found.push_back(&candidate);
                    return true;
                }
                return false;
            };
            for (const arg_id &id: argument.get_conflicts()) {
                if (append_arg(id.name())) continue;
                for (const group_spec &group: get_groups()) {
                    if (group.get_id().name() != id.name()) continue;
                    for (const arg_id &member: group.get_args()) append_arg(member.name());
                    break;
                }
            }
            return found;
        }

        // -- help -----------------------------------------------------------------

        /** \brief The author line, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_author() const noexcept {
            if (author_length == 0) return std::nullopt;
            return std::string_view{author_text, author_length};
        }

        /** \brief The one-line description, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_about() const noexcept {
            if (about_length == 0) return std::nullopt;
            return std::string_view{about_text, about_length};
        }

        /** \brief The `--help` (long form) description, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_about() const noexcept {
            if (long_about_length == 0) return std::nullopt;
            return std::string_view{long_about_text, long_about_length};
        }

        /** \brief Text printed before the help body, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_before_help() const noexcept {
            if (before_help_length == 0) return std::nullopt;
            return std::string_view{before_help_text, before_help_length};
        }

        /** \brief Text printed before the long help body, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_before_long_help() const noexcept {
            if (before_long_help_length == 0) return std::nullopt;
            return std::string_view{before_long_help_text, before_long_help_length};
        }

        /** \brief Text printed after the help body, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_after_help() const noexcept {
            if (after_help_length == 0) return std::nullopt;
            return std::string_view{after_help_text, after_help_length};
        }

        /** \brief Text printed after the long help body, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_after_long_help() const noexcept {
            if (after_long_help_length == 0) return std::nullopt;
            return std::string_view{after_long_help_text, after_long_help_length};
        }

        /** \brief A hand-written usage line replacing the generated one, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_override_usage() const noexcept {
            if (override_usage_length == 0) return std::nullopt;
            return std::string_view{override_usage_text, override_usage_length};
        }

        /** \brief A hand-written help page replacing the generated one, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_override_help() const noexcept {
            if (override_help_length == 0) return std::nullopt;
            return std::string_view{override_help_text, override_help_length};
        }

        /** \brief The help layout template, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help_template() const noexcept {
            if (help_template_length == 0) return std::nullopt;
            return std::string_view{help_template_text, help_template_length};
        }

        /**
         * \brief The default section heading for this command's arguments, if any.
         * \note An argument whose clapp::arg_spec::get_help_heading() is `nullopt`
         *       inherits this; one that returns an *empty* string opted out explicitly.
         */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_next_help_heading() const noexcept {
            if (next_help_heading_length == 0) return std::nullopt;
            return std::string_view{next_help_heading_text, next_help_heading_length};
        }

        /** \brief The heading printed above the subcommand list, if overridden. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_subcommand_help_heading() const noexcept {
            if (subcommand_help_heading_length == 0) return std::nullopt;
            return std::string_view{subcommand_help_heading_text, subcommand_help_heading_length};
        }

        /** \brief The placeholder used for the subcommand in usage, if overridden. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_subcommand_value_name() const noexcept {
            if (subcommand_value_name.empty()) return std::nullopt;
            return subcommand_value_name.name();
        }

        /** \brief The sort key among sibling subcommands. Defaults to 999, as in clap. */
        [[nodiscard]] constexpr std::size_t get_display_order() const noexcept {
            return display_order;
        }

        /** \brief The fixed terminal width to wrap at, if one was set. */
        [[nodiscard]] constexpr std::optional<std::size_t> get_term_width() const noexcept {
            return term_width == 0 ? std::nullopt : std::optional<std::size_t>{term_width};
        }

        /** \brief The upper bound on the detected terminal width, if one was set. */
        [[nodiscard]] constexpr std::optional<std::size_t> get_max_term_width() const noexcept {
            return max_term_width == 0 ? std::nullopt : std::optional<std::size_t>{max_term_width};
        }

        /** \brief The colour policy, decoded from the three colour bits. */
        [[nodiscard]] constexpr color_choice get_color() const noexcept {
            if (is_set(command_setting::color_never)) return color_choice::never;
            if (is_set(command_setting::color_always)) return color_choice::always;
            return color_choice::auto_;
        }

        /**
         * \brief The palette help and errors are rendered with.
         * \return The command's own palette, or clapp's default when none was set —
         *         the distinction lives in #style_data, which is never null.
         */
        [[nodiscard]] constexpr const styles &get_styles() const noexcept { return *style_data; }

        /**
         * \brief Whether external_subcommand_value_parser() was set (bool; not null compare).
         */
        [[nodiscard]] constexpr bool has_external_subcommand_value_parser() const noexcept {
            return external_parser_present;
        }

        /**
         * \brief Parser for captured external subcommand args, or null if unset.
         * \warning Null does not mean raw bytes — parse falls back to string (UTF-8).
         *          Use external_subcommand_value_parser<os_string>() for clap OsString.
         */
        [[nodiscard]] constexpr const parser_vtable *
        get_external_subcommand_value_parser() const noexcept {
            return external_parser;
        }

        // -- settings -------------------------------------------------------------

        /** \brief Whether \p setting is on. */
        [[nodiscard]] constexpr bool is_set(command_setting setting) const noexcept {
            return settings.is_set(setting);
        }

        /** \brief The whole flag word, for callers that want to test several knobs. */
        [[nodiscard]] constexpr command_flags get_settings() const noexcept { return settings; }

        /** \brief The knobs declared global, which every subcommand inherited. */
        [[nodiscard]] constexpr command_flags get_global_settings() const noexcept {
            return global_settings;
        }

        /** \brief Reports clapp::command_builder::ignore_errors(). */
        [[nodiscard]] constexpr bool is_ignore_errors_set() const noexcept {
            return is_set(command_setting::ignore_errors);
        }

        /** \brief Reports clapp::command_builder::allow_hyphen_values(). */
        [[nodiscard]] constexpr bool is_allow_hyphen_values_set() const noexcept {
            return is_set(command_setting::allow_hyphen_values);
        }

        /** \brief Reports clapp::command_builder::allow_negative_numbers(). */
        [[nodiscard]] constexpr bool is_allow_negative_numbers_set() const noexcept {
            return is_set(command_setting::allow_negative_numbers);
        }

        /** \brief Reports clapp::command_builder::args_override_self(). */
        [[nodiscard]] constexpr bool is_args_override_self() const noexcept {
            return is_set(command_setting::all_args_override_self);
        }

        /** \brief Reports clapp::command_builder::allow_missing_positional(). */
        [[nodiscard]] constexpr bool is_allow_missing_positional_set() const noexcept {
            return is_set(command_setting::allow_missing_positional);
        }

        /** \brief Reports clapp::command_builder::trailing_var_arg(). */
        [[nodiscard]] constexpr bool is_trailing_var_arg_set() const noexcept {
            return is_set(command_setting::trailing_var_arg);
        }

        /** \brief Reports clapp::command_builder::dont_delimit_trailing_values(). */
        [[nodiscard]] constexpr bool is_dont_delimit_trailing_values_set() const noexcept {
            return is_set(command_setting::dont_delimit_trailing_values);
        }

        /** \brief Reports clapp::command_builder::infer_long_args(). */
        [[nodiscard]] constexpr bool is_infer_long_args_set() const noexcept {
            return is_set(command_setting::infer_long_args);
        }

        /** \brief Reports clapp::command_builder::infer_subcommands(). */
        [[nodiscard]] constexpr bool is_infer_subcommands_set() const noexcept {
            return is_set(command_setting::infer_subcommands);
        }

        /** \brief Reports clapp::command_builder::subcommand_required(). */
        [[nodiscard]] constexpr bool is_subcommand_required_set() const noexcept {
            return is_set(command_setting::subcommand_required);
        }

        /** \brief Reports clapp::command_builder::allow_external_subcommands(). */
        [[nodiscard]] constexpr bool is_allow_external_subcommands_set() const noexcept {
            return is_set(command_setting::allow_external_subcommands);
        }

        /** \brief Reports clapp::command_builder::multicall(). */
        [[nodiscard]] constexpr bool is_multicall_set() const noexcept {
            return is_set(command_setting::multicall);
        }

        /** \brief Reports clapp::command_builder::subcommand_negates_reqs(). */
        [[nodiscard]] constexpr bool is_subcommand_negates_reqs_set() const noexcept {
            return is_set(command_setting::subcommands_negate_reqs);
        }

        /** \brief Reports clapp::command_builder::args_conflicts_with_subcommands(). */
        [[nodiscard]] constexpr bool is_args_conflicts_with_subcommands_set() const noexcept {
            return is_set(command_setting::args_negate_subcommands);
        }

        /** \brief Reports clapp::command_builder::subcommand_precedence_over_arg(). */
        [[nodiscard]] constexpr bool is_subcommand_precedence_over_arg_set() const noexcept {
            return is_set(command_setting::subcommand_precedence_over_arg);
        }

        /** \brief Reports clapp::command_builder::flatten_help(). */
        [[nodiscard]] constexpr bool is_flatten_help_set() const noexcept {
            return is_set(command_setting::flatten_help);
        }

        /** \brief Reports clapp::command_builder::arg_required_else_help(). */
        [[nodiscard]] constexpr bool is_arg_required_else_help_set() const noexcept {
            return is_set(command_setting::arg_required_else_help);
        }

        /** \brief Reports clapp::command_builder::next_line_help(). */
        [[nodiscard]] constexpr bool is_next_line_help_set() const noexcept {
            return is_set(command_setting::next_line_help);
        }

        /** \brief Reports clapp::command_builder::disable_colored_help(). */
        [[nodiscard]] constexpr bool is_disable_colored_help_set() const noexcept {
            return is_set(command_setting::disable_colored_help);
        }

        /** \brief Reports clapp::command_builder::disable_help_flag(). */
        [[nodiscard]] constexpr bool is_disable_help_flag_set() const noexcept {
            return is_set(command_setting::disable_help_flag);
        }

        /** \brief Reports clapp::command_builder::disable_help_subcommand(). */
        [[nodiscard]] constexpr bool is_disable_help_subcommand_set() const noexcept {
            return is_set(command_setting::disable_help_subcommand);
        }

        /** \brief Reports clapp::command_builder::disable_version_flag(). */
        [[nodiscard]] constexpr bool is_disable_version_flag_set() const noexcept {
            return is_set(command_setting::disable_version_flag);
        }

        /** \brief Reports clapp::command_builder::propagate_version(). */
        [[nodiscard]] constexpr bool is_propagate_version_set() const noexcept {
            return is_set(command_setting::propagate_version);
        }

        /** \brief Reports clapp::command_builder::hide(). */
        [[nodiscard]] constexpr bool is_hide_set() const noexcept {
            return is_set(command_setting::hidden);
        }

        /** \brief Reports clapp::command_builder::hide_possible_values(). */
        [[nodiscard]] constexpr bool is_hide_possible_values_set() const noexcept {
            return is_set(command_setting::hide_possible_values);
        }

        /** \brief Reports clapp::command_builder::help_expected(). */
        [[nodiscard]] constexpr bool is_help_expected_set() const noexcept {
            return is_set(command_setting::help_expected);
        }

        /** \brief Reports clapp::command_builder::no_binary_name(). */
        [[nodiscard]] constexpr bool is_no_binary_name_set() const noexcept {
            return is_set(command_setting::no_binary_name);
        }

        /**
         * \brief Equality by content (recursive; external_parser via type_name/values).
         */
        [[nodiscard]] constexpr bool operator==(const command_spec &other) const noexcept {
            return name.name() == other.name.name() && bin_name.name() == other.bin_name.name() &&
                   display_name.name() == other.display_name.name() &&
                   short_flag == other.short_flag && long_flag.name() == other.long_flag.name() &&
                   detail::spans_equal(get_all_aliases(), other.get_all_aliases()) &&
                   detail::spans_equal(get_all_short_flag_aliases(),
                                       other.get_all_short_flag_aliases()) &&
                   detail::spans_equal(get_all_long_flag_aliases(),
                                       other.get_all_long_flag_aliases()) &&
                   version.name() == other.version.name() &&
                   long_version.name() == other.long_version.name() &&
                   detail::spans_equal(get_arguments(), other.get_arguments()) &&
                   detail::spans_equal(get_groups(), other.get_groups()) &&
                   detail::spans_equal(get_subcommands(), other.get_subcommands()) &&
                   external_parser_present == other.external_parser_present &&
                   (!external_parser_present ||
                    (external_parser->type_name() == other.external_parser->type_name() &&
                     detail::spans_equal(external_parser->possible_values(),
                                         other.external_parser->possible_values()))) &&
                   get_author() == other.get_author() && get_about() == other.get_about() &&
                   get_long_about() == other.get_long_about() &&
                   get_before_help() == other.get_before_help() &&
                   get_before_long_help() == other.get_before_long_help() &&
                   get_after_help() == other.get_after_help() &&
                   get_after_long_help() == other.get_after_long_help() &&
                   get_override_usage() == other.get_override_usage() &&
                   get_override_help() == other.get_override_help() &&
                   get_help_template() == other.get_help_template() &&
                   get_next_help_heading() == other.get_next_help_heading() &&
                   get_subcommand_help_heading() == other.get_subcommand_help_heading() &&
                   subcommand_value_name.name() == other.subcommand_value_name.name() &&
                   display_order == other.display_order && term_width == other.term_width &&
                   max_term_width == other.max_term_width && get_styles() == other.get_styles() &&
                   settings == other.settings && global_settings == other.global_settings;
        }
    };

    // -----------------------------------------------------------------------------
    // command_builder — the builder
    // -----------------------------------------------------------------------------

    /**
     * \brief Chainable description of a command tree (clap Command). Owns strings.
     * \warning Never `static constexpr` (holds vectors). Build in consteval and freeze().
     * \warning Never `c = std::move(c).about("...");` — self-move can empty vectors.
     */
    class command_builder {
    public:
        /**
         * \brief Create a command called \p name.
         * \param name The command's name, which doubles as its id. Copied.
         * \note `constexpr`, not `consteval`: freeze() is the immediate function, and
         *       keeping the constructor `constexpr` lets the runtime half of the unit
         *       tests exercise the builder directly.
         */
        constexpr explicit command_builder(std::string_view name) : name_(name) {
        }

        /**
         * \name Identity
         * \{
         */

        /** \brief Rename the command. \param text The new name and id. */
        constexpr command_builder &&name(std::string_view text) && {
            name_ = std::string{text};
            return std::move(*this);
        }

        /**
         * \brief Set the executable name shown in usage.
         * \param text The name; an empty view resets it.
         */
        constexpr command_builder &&bin_name(std::string_view text) && {
            bin_name_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Set the name help displays, when it differs from the command's id.
         * \param text The display name; an empty view resets it.
         */
        constexpr command_builder &&display_name(std::string_view text) && {
            display_name_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Set the `-V` version text. \param text The version; empty resets. */
        constexpr command_builder &&version(std::string_view text) && {
            version_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Set the `--version` text, when it is longer than the `-V` one.
         * \param text The long version; an empty view resets it.
         */
        constexpr command_builder &&long_version(std::string_view text) && {
            long_version_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Set the author line. \param text The author; empty resets. */
        constexpr command_builder &&author(std::string_view text) && {
            author_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Set the one-line description. \param text The blurb; empty resets. */
        constexpr command_builder &&about(std::string_view text) && {
            about_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Set the `--help` description. \param text The prose; empty resets. */
        constexpr command_builder &&long_about(std::string_view text) && {
            long_about_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Set the `-C`-style flag that selects this subcommand.
         * \param letter The letter; `'\0'` removes it.
         */
        constexpr command_builder &&short_flag(char letter) && {
            short_flag_ = letter == '\0' ? std::nullopt : std::optional<char>{letter};
            return std::move(*this);
        }

        /**
         * \brief Set the `--config`-style flag that selects this subcommand.
         * \param text The spelling without `--`; an empty view removes it.
         */
        constexpr command_builder &&long_flag(std::string_view text) && {
            long_flag_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Add a hidden alternative command name. */
        constexpr command_builder &&alias(std::string_view text) && {
            aliases_.push_back(arg_alias{.name = std::string{text}, .visible = false});
            return std::move(*this);
        }

        /** \brief Add a visible alternative command name — listed in help. */
        constexpr command_builder &&visible_alias(std::string_view text) && {
            aliases_.push_back(arg_alias{.name = std::string{text}, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several hidden command names. */
        template<string_view_range R>
        constexpr command_builder &&aliases(R &&names) && {
            for (const std::string_view text: names)
                aliases_.push_back(arg_alias{.name = std::string{text}, .visible = false});
            return std::move(*this);
        }

        /** \brief Add several hidden command names from a braced list. */
        constexpr command_builder &&aliases(std::initializer_list<std::string_view> names) && {
            return std::move(*this).aliases<std::initializer_list<std::string_view> &>(names);
        }

        /** \brief Add several visible command names. */
        template<string_view_range R>
        constexpr command_builder &&visible_aliases(R &&names) && {
            for (const std::string_view text: names)
                aliases_.push_back(arg_alias{.name = std::string{text}, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several visible command names from a braced list. */
        constexpr command_builder &&
        visible_aliases(std::initializer_list<std::string_view> names) && {
            return std::move(*this).visible_aliases<std::initializer_list<std::string_view> &>(
                names);
        }

        /** \brief Add a hidden short-flag alias. */
        constexpr command_builder &&short_flag_alias(char letter) && {
            short_flag_aliases_.push_back(arg_short_alias{.name = letter, .visible = false});
            return std::move(*this);
        }

        /** \brief Add a visible short-flag alias. */
        constexpr command_builder &&visible_short_flag_alias(char letter) && {
            short_flag_aliases_.push_back(arg_short_alias{.name = letter, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several hidden short-flag aliases. */
        template<detail::char_range R>
        constexpr command_builder &&short_flag_aliases(R &&letters) && {
            for (const char letter: letters)
                short_flag_aliases_.push_back(arg_short_alias{.name = letter, .visible = false});
            return std::move(*this);
        }

        /** \brief Add several hidden short-flag aliases from a braced list. */
        constexpr command_builder &&short_flag_aliases(std::initializer_list<char> letters) && {
            return std::move(*this).short_flag_aliases<std::initializer_list<char> &>(letters);
        }

        /** \brief Add several visible short-flag aliases. */
        template<detail::char_range R>
        constexpr command_builder &&visible_short_flag_aliases(R &&letters) && {
            for (const char letter: letters)
                short_flag_aliases_.push_back(arg_short_alias{.name = letter, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several visible short-flag aliases from a braced list. */
        constexpr command_builder &&
        visible_short_flag_aliases(std::initializer_list<char> letters) && {
            return std::move(*this).visible_short_flag_aliases<std::initializer_list<char> &>(
                letters);
        }

        /** \brief Add a hidden long-flag alias. */
        constexpr command_builder &&long_flag_alias(std::string_view text) && {
            long_flag_aliases_.push_back(arg_alias{.name = std::string{text}, .visible = false});
            return std::move(*this);
        }

        /** \brief Add a visible long-flag alias. */
        constexpr command_builder &&visible_long_flag_alias(std::string_view text) && {
            long_flag_aliases_.push_back(arg_alias{.name = std::string{text}, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several hidden long-flag aliases. */
        template<string_view_range R>
        constexpr command_builder &&long_flag_aliases(R &&names) && {
            for (const std::string_view text: names)
                long_flag_aliases_.push_back(
                    arg_alias{.name = std::string{text}, .visible = false});
            return std::move(*this);
        }

        /** \brief Add several hidden long-flag aliases from a braced list. */
        constexpr command_builder &&
        long_flag_aliases(std::initializer_list<std::string_view> names) && {
            return std::move(*this).long_flag_aliases<std::initializer_list<std::string_view> &>(
                names);
        }

        /** \brief Add several visible long-flag aliases. */
        template<string_view_range R>
        constexpr command_builder &&visible_long_flag_aliases(R &&names) && {
            for (const std::string_view text: names)
                long_flag_aliases_.push_back(arg_alias{.name = std::string{text}, .visible = true});
            return std::move(*this);
        }

        /** \brief Add several visible long-flag aliases from a braced list. */
        constexpr command_builder &&
        visible_long_flag_aliases(std::initializer_list<std::string_view> names) && {
            return std::move(*this)
                    .visible_long_flag_aliases<std::initializer_list<std::string_view> &>(names);
        }

        /** \} */

        /**
         * \name Composition
         * \{
         */

        /**
         * \brief Add one argument (applies next_help_heading / next_display_order if unset).
         * \param argument Moved in.
         * \warning Unset display_order uses get_configured_display_order(), not "== 999"
         *          (999 is a legal explicit key).
         */
        constexpr command_builder &&arg(arg_builder argument) && {
            if (help_heading_.has_value() && !argument.get_help_heading().has_value())
                std::move(argument).help_heading(*help_heading_);
            if (disp_ord_cursor_.has_value() && !argument.is_positional()) {
                // The cursor advances for every non-positional argument, even one that
                // brought its own sort key — clap's `arg_internal` puts the increment
                // outside the `get_or_insert`, so an explicitly ordered argument still
                // occupies a place in the sequence.
                if (!argument.get_configured_display_order().has_value())
                    std::move(argument).display_order(*disp_ord_cursor_);
                disp_ord_cursor_ = *disp_ord_cursor_ + 1;
            }
            args_.push_back(std::move(argument));
            return std::move(*this);
        }

        /** \brief Add several arguments, in order. */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, const arg_builder &>
        constexpr command_builder &&args(R &&arguments) && {
            for (const arg_builder &argument: arguments) std::move(*this).arg(argument);
            return std::move(*this);
        }

        /** \brief Add several arguments from a braced list. */
        constexpr command_builder &&args(std::initializer_list<arg_builder> arguments) && {
            for (const arg_builder &argument: arguments) std::move(*this).arg(argument);
            return std::move(*this);
        }

        /** \brief Add one argument group. */
        constexpr command_builder &&group(group_builder definition) && {
            groups_.push_back(std::move(definition));
            return std::move(*this);
        }

        /** \brief Add several argument groups, in order. */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, const group_builder &>
        constexpr command_builder &&groups(R &&definitions) && {
            for (const group_builder &definition: definitions) groups_.push_back(definition);
            return std::move(*this);
        }

        /** \brief Add several argument groups from a braced list. */
        constexpr command_builder &&groups(std::initializer_list<group_builder> definitions) && {
            for (const group_builder &definition: definitions) groups_.push_back(definition);
            return std::move(*this);
        }

        /**
         * \brief Add one subcommand (shares next_display_order cursor with arguments).
         * \note Injected help stays at 999 (bypasses this path).
         */
        constexpr command_builder &&subcommand(command_builder child) && {
            if (disp_ord_cursor_.has_value()) {
                if (!child.disp_ord_.has_value()) child.disp_ord_ = disp_ord_cursor_;
                disp_ord_cursor_ = *disp_ord_cursor_ + 1;
            }
            subcommands_.push_back(std::move(child));
            return std::move(*this);
        }

        /** \brief Add several subcommands, in order. */
        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, const command_builder &>
        constexpr command_builder &&subcommands(R &&children) && {
            // Routed through subcommand() rather than pushed, so that the display-order
            // cursor advances. Pushing directly is how the args()/arg() pair used to
            // disagree with itself.
            for (const command_builder &child: children) std::move(*this).subcommand(child);
            return std::move(*this);
        }

        /** \brief Add several subcommands from a braced list. */
        constexpr command_builder &&
        subcommands(std::initializer_list<command_builder> children) && {
            for (const command_builder &child: children) subcommands_.push_back(child);
            return std::move(*this);
        }

        /**
         * \brief Register a transformation applied at the start of freeze().
         *
         * \param transform A function taking and returning a `command_builder`.
         *
         * \note clap's `Command::defer` exists to avoid building an expensive subtree
         *       until the parser actually needs it. clapp has no such saving to make —
         *       the whole tree is materialised during constant evaluation either way —
         *       so the value here is purely ordering: \p transform sees the command
         *       after every other setter has run, which is what a ported clap
         *       `defer(...)` closure expects.
         *
         * \note A plain function pointer rather than a template parameter, so the
         *       builder stays one type. A capture-less lambda converts implicitly.
         *
         * \note Stored in a `std::optional`, not as a bare pointer that prepare()
         *       tests against null: `deferred_ != nullptr` is not a constant
         *       expression under `-fsanitize=null` once a transform really is set
         *       (see the length-sentinel note on clapp::arg_id), and freeze() runs
         *       entirely inside constant evaluation.
         */
        constexpr command_builder &&defer(command_builder (*transform)(command_builder)) && {
            deferred_ = transform;
            return std::move(*this);
        }

        /**
         * \brief Replace the argument with id \p id by transform(that argument).
         * \tparam F Callable arg_builder → arg_builder.
         * \param id Argument to rewrite; unknown id fails here (not in freeze).
         * \param transform The rewrite.
         * \warning Must reject unknown ids here — freeze cannot catch a missed mut_arg
         *          (prepare may inject help/version after the fact).
         */
        template<class F>
        constexpr command_builder &&mut_arg(std::string_view id, F &&transform) && {
            const auto found = std::ranges::find_if(
                args_, [id](const arg_builder &candidate) { return candidate.get_id() == id; });
            if (found == args_.end())
                detail::fail({
                    "clapp::command_builder::mut_arg: command '",
                    name_,
                    "': argument '",
                    id,
                    "' is undefined"
                });

            arg_builder current = std::move(*found);
            args_.erase(found);
            args_.push_back(std::invoke(std::forward<F>(transform), std::move(current)));
            return std::move(*this);
        }

        /**
         * \brief Apply \p transform to every argument.
         * \tparam F Callable taking an `arg_builder` by value and returning one.
         */
        template<class F>
        constexpr command_builder &&mut_args(F &&transform) && {
            for (arg_builder &candidate: args_) candidate = transform(std::move(candidate));
            return std::move(*this);
        }

        /**
         * \brief Replace the group with id \p id by transform(that group).
         * \tparam F Callable group_builder → group_builder.
         * \warning Implicit groups from arg_builder::group() are not materialised yet;
         *          naming one is rejected (same as clap mut_group).
         */
        template<class F>
        constexpr command_builder &&mut_group(std::string_view id, F &&transform) && {
            const auto found = std::ranges::find_if(groups_, [id](const group_builder &candidate) {
                return candidate.get_id() == id;
            });
            if (found == groups_.end())
                detail::fail({
                    "clapp::command_builder::mut_group: command '",
                    name_,
                    "': group '",
                    id,
                    "' is undefined"
                });

            group_builder current = std::move(*found);
            groups_.erase(found);
            groups_.push_back(std::invoke(std::forward<F>(transform), std::move(current)));
            return std::move(*this);
        }

        /**
         * \brief Replace the subcommand called \p name by `transform(that command)`.
         * \tparam F Callable taking a `command_builder` by value and returning one.
         *
         * \note Rejects an unknown name, like #mut_arg. The injected `help` subcommand
         *       is added in freeze()'s prepare() step and so is not visible here —
         *       same lifecycle point as clap's `Command::mut_subcommand`.
         */
        template<class F>
        constexpr command_builder &&mut_subcommand(std::string_view sub_name, F &&transform) && {
            const auto found = std::ranges::find_if(subcommands_,
                                                    [sub_name](const command_builder &candidate) {
                                                        return candidate.get_name() == sub_name;
                                                    });
            if (found == subcommands_.end())
                detail::fail({
                    "clapp::command_builder::mut_subcommand: command '",
                    name_,
                    "': subcommand '",
                    sub_name,
                    "' is undefined"
                });

            command_builder current = std::move(*found);
            subcommands_.erase(found);
            subcommands_.push_back(std::invoke(std::forward<F>(transform), std::move(current)));
            return std::move(*this);
        }

        /**
         * \brief Apply \p transform to every subcommand.
         * \tparam F Callable taking a `command_builder` by value and returning one.
         */
        template<class F>
        constexpr command_builder &&mut_subcommands(F &&transform) && {
            for (command_builder &candidate: subcommands_)
                candidate = transform(std::move(candidate));
            return std::move(*this);
        }

        /**
         * \brief Parse the arguments of a captured external subcommand as \p T.
         * \tparam T The value type; also switches allow_external_subcommands() on.
         */
        template<erasable_parsable T>
        constexpr command_builder &&external_subcommand_value_parser() && {
            external_parser_ = parser_for<T>();
            return std::move(*this);
        }

        /**
         * \brief Parse external subcommand args with \p table (nullptr overload resets).
         * \param table From parser_for; must not be null at runtime.
         * \warning Runtime null is stored (no null-guard under ubsan consteval) and
         *          operator== will dereference it. Use the nullptr overload to clear.
         */
        constexpr command_builder &&
        external_subcommand_value_parser(const parser_vtable *table) && {
            external_parser_ = table;
            return std::move(*this);
        }

        /**
         * \brief Drop the external-subcommand parser.
         * \note A separate overload rather than a null check; see the warning above.
         * \note This restores the *default*, which is `std::string` and therefore
         *       UTF-8-validating — not raw bytes. See
         *       command_spec::get_external_subcommand_value_parser().
         */
        constexpr command_builder &&external_subcommand_value_parser(std::nullptr_t) && {
            external_parser_.reset();
            return std::move(*this);
        }

        /** \} */

        /**
         * \name Help layout
         * \{
         */

        /**
         * \brief Replace the generated usage line.
         * \param text The replacement; an empty view resets it.
         */
        constexpr command_builder &&override_usage(std::string_view text) && {
            override_usage_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Replace the entire generated help page.
         * \param text The replacement; an empty view resets it.
         */
        constexpr command_builder &&override_help(std::string_view text) && {
            override_help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Set the help layout template.
         * \param text The template; an empty view resets it.
         */
        constexpr command_builder &&help_template(std::string_view text) && {
            help_template_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Text printed before the help body. */
        constexpr command_builder &&before_help(std::string_view text) && {
            before_help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Text printed before the long help body. */
        constexpr command_builder &&before_long_help(std::string_view text) && {
            before_long_help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Text printed after the help body. */
        constexpr command_builder &&after_help(std::string_view text) && {
            after_help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Text printed after the long help body. */
        constexpr command_builder &&after_long_help(std::string_view text) && {
            after_long_help_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Set the section heading arguments added from now on inherit.
         * \param text The heading; an empty view stops the inheritance.
         * \warning Positional in effect: it applies to arguments added **after** this
         *          call, not to the ones already present. That is clap's behaviour and
         *          it is easy to misread as a whole-command setting.
         */
        constexpr command_builder &&next_help_heading(std::string_view text) && {
            help_heading_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Start numbering the display order of subsequent arguments at \p order.
         * \param order The first order value; each named argument added afterwards
         *        takes the next one.
         * \note Only arguments whose display order is still the default 999 are
         *       numbered, so an explicit `display_order(999)` is indistinguishable
         *       from "unset" and will be overwritten. clap distinguishes the two; the
         *       cost of matching it would be a second sentinel in clapp::arg_builder.
         */
        constexpr command_builder &&next_display_order(std::size_t order) && {
            disp_ord_cursor_ = order;
            return std::move(*this);
        }

        /**
         * \brief Stop numbering, so subsequent arguments and subcommands sort by name.
         *        clap's `Command::next_display_order(None)`.
         *
         * \return `*this`, for chaining.
         *
         * \note The counterpart of the overload above, and the reason the cursor is a
         *       `std::optional` rather than a `std::size_t`. Without a cursor every
         *       argument keeps the default display order, so the `(display_order, name)`
         *       sort collapses onto the name — which is how clap spells "alphabetical
         *       help". The injected `--help` and `--version` keep the same default, so
         *       they sort into the list rather than onto the end of it: clap's
         *       `no_derive_order` expects `--flag_b`, `-h, --help`, `--option_a`,
         *       `-V, --version` interleaved, and that is the observable difference.
         *
         * \note Spelled as an overload on `std::nullopt_t` rather than as a separate
         *       `clear_display_order()` so that the call site reads like clap's and so
         *       that the two spellings cannot drift apart. `std::nullopt` is the only
         *       value it accepts.
         */
        constexpr command_builder &&next_display_order(std::nullopt_t) && {
            disp_ord_cursor_ = std::nullopt;
            return std::move(*this);
        }

        /** \brief Set this command's sort key among its siblings. */
        constexpr command_builder &&display_order(std::size_t order) && {
            disp_ord_ = order;
            return std::move(*this);
        }

        /**
         * \brief Wrap help at exactly \p width columns.
         * \param width The width; 0 resets to "detect the terminal".
         */
        constexpr command_builder &&term_width(std::size_t width) && {
            term_width_ = width;
            return std::move(*this);
        }

        /**
         * \brief Never wrap help wider than \p width columns.
         * \param width The bound; 0 resets to "no bound".
         */
        constexpr command_builder &&max_term_width(std::size_t width) && {
            max_term_width_ = width;
            return std::move(*this);
        }

        /** \brief Set the placeholder used for the subcommand in usage. */
        constexpr command_builder &&subcommand_value_name(std::string_view text) && {
            subcommand_value_name_ = text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /** \brief Set the heading printed above the subcommand list. */
        constexpr command_builder &&subcommand_help_heading(std::string_view text) && {
            subcommand_help_heading_ =
                    text.empty() ? std::nullopt : std::optional<std::string>{text};
            return std::move(*this);
        }

        /**
         * \brief Choose the colour policy.
         * \param choice When to emit escape sequences.
         * \note Stored as the three colour bits, exactly as clap does, so
         *       clapp::command_spec::get_color() reads it back off the flag word.
         * \note **Global**, like clap's: the choice reaches every subcommand, because
         *       a CLI that honours `--color=never` only at the root is worse than one
         *       that does not honour it at all.
         */
        constexpr command_builder &&color(color_choice choice) && {
            std::move(*this).unset_global_setting(command_setting::color_auto);
            std::move(*this).unset_global_setting(command_setting::color_always);
            std::move(*this).unset_global_setting(command_setting::color_never);
            switch (choice) {
                case color_choice::auto_:
                    return std::move(*this).global_setting(command_setting::color_auto);
                case color_choice::always:
                    return std::move(*this).global_setting(command_setting::color_always);
                case color_choice::never:
                    return std::move(*this).global_setting(command_setting::color_never);
            }
            return std::move(*this);
        }

        /** \brief Set the palette help and errors are rendered with. */
        constexpr command_builder &&styles(clapp::styles palette) && {
            styles_ = palette;
            return std::move(*this);
        }

        /** \} */

        /**
         * \name Settings
         * \{
         */

        /**
         * \brief argv does not begin with the program name.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&no_binary_name(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::no_binary_name, yes);
        }

        /**
         * \brief Keep parsing after an error instead of returning at once.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&ignore_errors(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::ignore_errors, yes);
        }

        /**
         * \brief A repeated argument silently replaces its earlier occurrence.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&args_override_self(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::all_args_override_self, yes);
        }

        /**
         * \brief Do not split delimited values that follow `--`.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&dont_delimit_trailing_values(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::dont_delimit_trailing_values, yes);
        }

        /**
         * \brief Do not inject `-V` / `--version`.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&disable_version_flag(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::disable_version_flag, yes);
        }

        /**
         * \brief Subcommands inherit version() and long_version().
         * \note Global; see #global_flag(). Without that, the version stops one level
         *       down: `root -> mid -> leaf` would give `mid` a version and `leaf` none.
         */
        constexpr command_builder &&propagate_version(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::propagate_version, yes);
        }

        /**
         * \brief Every argument's help starts on its own line.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&next_line_help(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::next_line_help, yes);
        }

        /**
         * \brief Do not inject `-h` / `--help`.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&disable_help_flag(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::disable_help_flag, yes);
        }

        /**
         * \brief Do not inject the `help` subcommand.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&disable_help_subcommand(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::disable_help_subcommand, yes);
        }

        /**
         * \brief Render help without styling.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&disable_colored_help(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::disable_colored_help, yes);
        }

        /**
         * \brief Require every argument to carry help text.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&help_expected(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::help_expected, yes);
        }

        /**
         * \brief Suppress `[possible values: ...]` on every argument.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&hide_possible_values(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::hide_possible_values, yes);
        }

        /**
         * \brief `--verb` may abbreviate `--verbose`.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&infer_long_args(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::infer_long_args, yes);
        }

        /**
         * \brief `cl` may abbreviate the subcommand `clone`.
         * \note Global; see #global_flag().
         */
        constexpr command_builder &&infer_subcommands(bool yes = true) && {
            return std::move(*this).global_flag(command_setting::infer_subcommands, yes);
        }

        /** \brief Render subcommand help inline instead of as a separate page. */
        constexpr command_builder &&flatten_help(bool yes = true) && {
            return std::move(*this).setting(command_setting::flatten_help, yes);
        }

        /** \brief An empty command line prints help instead of parsing. */
        constexpr command_builder &&arg_required_else_help(bool yes = true) && {
            return std::move(*this).setting(command_setting::arg_required_else_help, yes);
        }

        /** \brief Every value-taking argument accepts values starting with `-`. */
        constexpr command_builder &&allow_hyphen_values(bool yes = true) && {
            return std::move(*this).setting(command_setting::allow_hyphen_values, yes);
        }

        /** \brief Every value-taking argument accepts negative numbers. */
        constexpr command_builder &&allow_negative_numbers(bool yes = true) && {
            return std::move(*this).setting(command_setting::allow_negative_numbers, yes);
        }

        /** \brief The final positional swallows everything that follows. */
        constexpr command_builder &&trailing_var_arg(bool yes = true) && {
            return std::move(*this).setting(command_setting::trailing_var_arg, yes);
        }

        /** \brief `prog [opt] <req>` may skip the optional positional. */
        constexpr command_builder &&allow_missing_positional(bool yes = true) && {
            return std::move(*this).setting(command_setting::allow_missing_positional, yes);
        }

        /** \brief Omit this subcommand from the parent's help. */
        constexpr command_builder &&hide(bool yes = true) && {
            return std::move(*this).setting(command_setting::hidden, yes);
        }

        /** \brief A subcommand must be given. */
        constexpr command_builder &&subcommand_required(bool yes = true) && {
            return std::move(*this).setting(command_setting::subcommand_required, yes);
        }

        /** \brief Capture unknown subcommands instead of rejecting them. */
        constexpr command_builder &&allow_external_subcommands(bool yes = true) && {
            return std::move(*this).setting(command_setting::allow_external_subcommands, yes);
        }

        /**
         * \brief Using an argument excuses the subcommand requirement.
         * \note Implies subcommand_negates_reqs(), which freeze() sets for you.
         */
        constexpr command_builder &&args_conflicts_with_subcommands(bool yes = true) && {
            return std::move(*this).setting(command_setting::args_negate_subcommands, yes);
        }

        /** \brief A subcommand name wins over a value the parser was still collecting. */
        constexpr command_builder &&subcommand_precedence_over_arg(bool yes = true) && {
            return std::move(*this).setting(command_setting::subcommand_precedence_over_arg, yes);
        }

        /** \brief Using a subcommand excuses this command's required arguments. */
        constexpr command_builder &&subcommand_negates_reqs(bool yes = true) && {
            return std::move(*this).setting(command_setting::subcommands_negate_reqs, yes);
        }

        /**
         * \brief argv[0] selects the subcommand, busybox style.
         * \note Implies subcommand_required(), disable_help_flag() and
         *       disable_version_flag(), which freeze() sets for you.
         */
        constexpr command_builder &&multicall(bool yes = true) && {
            return std::move(*this).setting(command_setting::multicall, yes);
        }

        /** \brief Turn \p setting on. */
        constexpr command_builder &&setting(command_setting setting) && {
            settings_.set(setting);
            return std::move(*this);
        }

        /** \brief Turn \p setting on or off. */
        constexpr command_builder &&setting(command_setting setting, bool enable) && {
            settings_.set(setting, enable);
            return std::move(*this);
        }

        /** \brief Turn \p setting off. */
        constexpr command_builder &&unset_setting(command_setting setting) && {
            settings_.unset(setting);
            return std::move(*this);
        }

        /** \brief Turn \p setting on here **and** in every subcommand. */
        constexpr command_builder &&global_setting(command_setting setting) && {
            settings_.set(setting);
            global_settings_.set(setting);
            return std::move(*this);
        }

        /** \brief Stop \p setting from being inherited, and turn it off here. */
        constexpr command_builder &&unset_global_setting(command_setting setting) && {
            settings_.unset(setting);
            global_settings_.unset(setting);
            return std::move(*this);
        }

        /**
         * \brief Set or clear \p setting for this command and its whole subtree.
         * \param setting Knob. \param enable true to set+inherit, false to clear.
         * \warning Use this (not #setting) for clap's global convenience setters —
         *          #setting alone would not propagate to subcommands.
         */
        constexpr command_builder &&global_flag(command_setting setting, bool enable) && {
            return enable
                       ? std::move(*this).global_setting(setting)
                       : std::move(*this).unset_global_setting(setting);
        }

        /** \} */

        /**
         * \name Reflection
         * \{
         */

        /** \brief The command's name. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::string_view get_name() const noexcept { return name_; }

        /** \brief The executable name, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_bin_name() const noexcept {
            if (!bin_name_.has_value()) return std::nullopt;
            return std::string_view{*bin_name_};
        }

        /** \brief The display name, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_display_name() const noexcept {
            if (!display_name_.has_value()) return std::nullopt;
            return std::string_view{*display_name_};
        }

        /** \brief The `-V` text, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_version() const noexcept {
            if (!version_.has_value()) return std::nullopt;
            return std::string_view{*version_};
        }

        /** \brief The `--version` text, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_version() const noexcept {
            if (!long_version_.has_value()) return std::nullopt;
            return std::string_view{*long_version_};
        }

        /** \brief The author line, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_author() const noexcept {
            if (!author_.has_value()) return std::nullopt;
            return std::string_view{*author_};
        }

        /** \brief The one-line description, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_about() const noexcept {
            if (!about_.has_value()) return std::nullopt;
            return std::string_view{*about_};
        }

        /** \brief The long description, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_about() const noexcept {
            if (!long_about_.has_value()) return std::nullopt;
            return std::string_view{*long_about_};
        }

        /** \brief The `-C`-style subcommand flag, if set. */
        [[nodiscard]] constexpr std::optional<char> get_short_flag() const noexcept {
            return short_flag_;
        }

        /** \brief The `--config`-style subcommand flag, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_long_flag() const noexcept {
            if (!long_flag_.has_value()) return std::nullopt;
            return std::string_view{*long_flag_};
        }

        /** \brief Every command-name alias. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_alias> get_all_aliases() const noexcept {
            return std::span<const arg_alias>{aliases_};
        }

        /** \brief Every short-flag alias. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_short_alias>
        get_all_short_flag_aliases() const noexcept {
            return std::span<const arg_short_alias>{short_flag_aliases_};
        }

        /** \brief Every long-flag alias. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const arg_alias>
        get_all_long_flag_aliases() const noexcept {
            return std::span<const arg_alias>{long_flag_aliases_};
        }

        /**
         * \brief The arguments added so far, **before** freeze() injects any.
         * \warning Borrows `*this`.
         */
        [[nodiscard]] constexpr std::span<const arg_builder> get_arguments() const noexcept {
            return std::span<const arg_builder>{args_};
        }

        /** \brief The groups added so far. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const group_builder> get_groups() const noexcept {
            return std::span<const group_builder>{groups_};
        }

        /** \brief The subcommands added so far. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const command_builder> get_subcommands() const noexcept {
            return std::span<const command_builder>{subcommands_};
        }

        /** \brief Whether any subcommand has been added. */
        [[nodiscard]] constexpr bool has_subcommands() const noexcept {
            return !subcommands_.empty();
        }

        /** \brief The argument with id \p id, or `nullptr`. \warning Borrows `*this`. */
        [[nodiscard]] constexpr const arg_builder *find_arg(std::string_view id) const noexcept {
            for (const arg_builder &candidate: args_) {
                if (candidate.get_id() == id) return &candidate;
            }
            return nullptr;
        }

        /** \brief The group with id \p id, or `nullptr`. \warning Borrows `*this`. */
        [[nodiscard]] constexpr const group_builder *
        find_group(std::string_view id) const noexcept {
            for (const group_builder &candidate: groups_) {
                if (candidate.get_id() == id) return &candidate;
            }
            return nullptr;
        }

        /**
         * \brief The subcommand named \p sub_name, or `nullptr`.
         * \warning Borrows `*this`.
         */
        [[nodiscard]] constexpr const command_builder *
        find_subcommand(std::string_view sub_name) const noexcept {
            for (const command_builder &candidate: subcommands_) {
                if (candidate.name_ == sub_name) return &candidate;
                for (const arg_alias &alias: candidate.aliases_) {
                    if (alias.name == sub_name) return &candidate;
                }
            }
            return nullptr;
        }

        /**
         * \brief Whether an argument with id \p id has been added.
         *
         * \note The predicate half of find_arg(), for the same reason
         *       clapp::command_spec::has_arg() exists: `find_arg(id) != nullptr` is not
         *       a constant expression under `-fsanitize=null` once the argument is
         *       found, and every interesting call here is inside a `consteval`
         *       freeze(). See the length-sentinel note on clapp::arg_id.
         */
        [[nodiscard]] constexpr bool has_arg(std::string_view id) const noexcept {
            return std::ranges::any_of(args_,
                                       [id](const arg_builder &a) { return a.get_id() == id; });
        }

        /** \brief Whether a group with id \p id has been added. \see has_arg() */
        [[nodiscard]] constexpr bool has_group(std::string_view id) const noexcept {
            return std::ranges::any_of(groups_,
                                       [id](const group_builder &g) { return g.get_id() == id; });
        }

        /**
         * \brief Whether a subcommand answers to \p sub_name, name or alias.
         * \see has_arg()
         */
        [[nodiscard]] constexpr bool has_subcommand(std::string_view sub_name) const noexcept {
            return std::ranges::any_of(subcommands_, [sub_name](const command_builder &c) {
                return c.name_ == sub_name ||
                       std::ranges::any_of(c.aliases_, [sub_name](const arg_alias &alias) {
                           return alias.name == sub_name;
                       });
            });
        }

        /** \brief The generated usage override, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_override_usage() const noexcept {
            if (!override_usage_.has_value()) return std::nullopt;
            return std::string_view{*override_usage_};
        }

        /** \brief The help-page override, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_override_help() const noexcept {
            if (!override_help_.has_value()) return std::nullopt;
            return std::string_view{*override_help_};
        }

        /** \brief The help template, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_help_template() const noexcept {
            if (!help_template_.has_value()) return std::nullopt;
            return std::string_view{*help_template_};
        }

        /** \brief Text before the help body, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_before_help() const noexcept {
            if (!before_help_.has_value()) return std::nullopt;
            return std::string_view{*before_help_};
        }

        /** \brief Text before the long help body, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_before_long_help() const noexcept {
            if (!before_long_help_.has_value()) return std::nullopt;
            return std::string_view{*before_long_help_};
        }

        /** \brief Text after the help body, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view> get_after_help() const noexcept {
            if (!after_help_.has_value()) return std::nullopt;
            return std::string_view{*after_help_};
        }

        /** \brief Text after the long help body, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_after_long_help() const noexcept {
            if (!after_long_help_.has_value()) return std::nullopt;
            return std::string_view{*after_long_help_};
        }

        /**
         * \brief The heading arguments added from now on inherit, if set.
         * \warning Borrows `*this`.
         */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_next_help_heading() const noexcept {
            if (!help_heading_.has_value()) return std::nullopt;
            return std::string_view{*help_heading_};
        }

        /** \brief The subcommand-list heading, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_subcommand_help_heading() const noexcept {
            if (!subcommand_help_heading_.has_value()) return std::nullopt;
            return std::string_view{*subcommand_help_heading_};
        }

        /** \brief The subcommand usage placeholder, if set. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::optional<std::string_view>
        get_subcommand_value_name() const noexcept {
            if (!subcommand_value_name_.has_value()) return std::nullopt;
            return std::string_view{*subcommand_value_name_};
        }

        /** \brief This command's sort key among siblings; 999 when unset, as in clap. */
        [[nodiscard]] constexpr std::size_t get_display_order() const noexcept {
            return disp_ord_.value_or(default_display_order);
        }

        /** \brief The fixed wrap width, if set. */
        [[nodiscard]] constexpr std::optional<std::size_t> get_term_width() const noexcept {
            return term_width_ == 0 ? std::nullopt : std::optional<std::size_t>{term_width_};
        }

        /** \brief The wrap-width bound, if set. */
        [[nodiscard]] constexpr std::optional<std::size_t> get_max_term_width() const noexcept {
            return max_term_width_ == 0
                       ? std::nullopt
                       : std::optional<std::size_t>{max_term_width_};
        }

        /** \brief The colour policy, decoded from the three colour bits. */
        [[nodiscard]] constexpr color_choice get_color() const noexcept {
            if (settings_.is_set(command_setting::color_never)) return color_choice::never;
            if (settings_.is_set(command_setting::color_always)) return color_choice::always;
            return color_choice::auto_;
        }

        /** \brief The palette, or clapp's default when none was chosen. */
        [[nodiscard]] constexpr clapp::styles get_styles() const noexcept {
            return styles_.value_or(detail::default_styles);
        }

        /** \brief The external-subcommand parser, or `nullptr`. */
        [[nodiscard]] constexpr const parser_vtable *
        get_external_subcommand_value_parser() const noexcept {
            return external_parser_.value_or(nullptr);
        }

        /** \brief Whether \p setting is on. */
        [[nodiscard]] constexpr bool is_set(command_setting setting) const noexcept {
            return settings_.is_set(setting);
        }

        /** \brief The whole flag word. */
        [[nodiscard]] constexpr command_flags get_settings() const noexcept { return settings_; }

        /** \brief The knobs declared global(), which subcommands will inherit. */
        [[nodiscard]] constexpr command_flags get_global_settings() const noexcept {
            return global_settings_;
        }

        /** \brief Reports ignore_errors(). */
        [[nodiscard]] constexpr bool is_ignore_errors_set() const noexcept {
            return is_set(command_setting::ignore_errors);
        }

        /** \brief Reports allow_hyphen_values(). */
        [[nodiscard]] constexpr bool is_allow_hyphen_values_set() const noexcept {
            return is_set(command_setting::allow_hyphen_values);
        }

        /** \brief Reports allow_negative_numbers(). */
        [[nodiscard]] constexpr bool is_allow_negative_numbers_set() const noexcept {
            return is_set(command_setting::allow_negative_numbers);
        }

        /** \brief Reports args_override_self(). */
        [[nodiscard]] constexpr bool is_args_override_self() const noexcept {
            return is_set(command_setting::all_args_override_self);
        }

        /** \brief Reports allow_missing_positional(). */
        [[nodiscard]] constexpr bool is_allow_missing_positional_set() const noexcept {
            return is_set(command_setting::allow_missing_positional);
        }

        /** \brief Reports trailing_var_arg(). */
        [[nodiscard]] constexpr bool is_trailing_var_arg_set() const noexcept {
            return is_set(command_setting::trailing_var_arg);
        }

        /** \brief Reports dont_delimit_trailing_values(). */
        [[nodiscard]] constexpr bool is_dont_delimit_trailing_values_set() const noexcept {
            return is_set(command_setting::dont_delimit_trailing_values);
        }

        /** \brief Reports infer_long_args(). */
        [[nodiscard]] constexpr bool is_infer_long_args_set() const noexcept {
            return is_set(command_setting::infer_long_args);
        }

        /** \brief Reports infer_subcommands(). */
        [[nodiscard]] constexpr bool is_infer_subcommands_set() const noexcept {
            return is_set(command_setting::infer_subcommands);
        }

        /** \brief Reports subcommand_required(). */
        [[nodiscard]] constexpr bool is_subcommand_required_set() const noexcept {
            return is_set(command_setting::subcommand_required);
        }

        /** \brief Reports allow_external_subcommands(). */
        [[nodiscard]] constexpr bool is_allow_external_subcommands_set() const noexcept {
            return is_set(command_setting::allow_external_subcommands);
        }

        /** \brief Reports multicall(). */
        [[nodiscard]] constexpr bool is_multicall_set() const noexcept {
            return is_set(command_setting::multicall);
        }

        /** \brief Reports subcommand_negates_reqs(). */
        [[nodiscard]] constexpr bool is_subcommand_negates_reqs_set() const noexcept {
            return is_set(command_setting::subcommands_negate_reqs);
        }

        /** \brief Reports args_conflicts_with_subcommands(). */
        [[nodiscard]] constexpr bool is_args_conflicts_with_subcommands_set() const noexcept {
            return is_set(command_setting::args_negate_subcommands);
        }

        /** \brief Reports subcommand_precedence_over_arg(). */
        [[nodiscard]] constexpr bool is_subcommand_precedence_over_arg_set() const noexcept {
            return is_set(command_setting::subcommand_precedence_over_arg);
        }

        /** \brief Reports flatten_help(). */
        [[nodiscard]] constexpr bool is_flatten_help_set() const noexcept {
            return is_set(command_setting::flatten_help);
        }

        /** \brief Reports arg_required_else_help(). */
        [[nodiscard]] constexpr bool is_arg_required_else_help_set() const noexcept {
            return is_set(command_setting::arg_required_else_help);
        }

        /** \brief Reports next_line_help(). */
        [[nodiscard]] constexpr bool is_next_line_help_set() const noexcept {
            return is_set(command_setting::next_line_help);
        }

        /** \brief Reports disable_colored_help(). */
        [[nodiscard]] constexpr bool is_disable_colored_help_set() const noexcept {
            return is_set(command_setting::disable_colored_help);
        }

        /** \brief Reports disable_help_flag(). */
        [[nodiscard]] constexpr bool is_disable_help_flag_set() const noexcept {
            return is_set(command_setting::disable_help_flag);
        }

        /**
         * \brief Reports disable_help_subcommand(), **resolved**.
         * \note A command with no subcommands has nothing for a `help` subcommand to
         *       talk about, so freeze() sets the bit; this getter answers what freeze()
         *       will record rather than what was literally written.
         */
        [[nodiscard]] constexpr bool is_disable_help_subcommand_set() const noexcept {
            return is_set(command_setting::disable_help_subcommand) || subcommands_.empty();
        }

        /**
         * \brief Reports disable_version_flag(), **resolved**.
         * \note There is nothing to print without version() or long_version(), so the
         *       flag is implied — exactly as in clap, whose accessor folds the same
         *       condition in. freeze() writes the resolved answer into the flag word.
         */
        [[nodiscard]] constexpr bool is_disable_version_flag_set() const noexcept {
            return is_set(command_setting::disable_version_flag) ||
                   (!version_.has_value() && !long_version_.has_value());
        }

        /** \brief Reports propagate_version(). */
        [[nodiscard]] constexpr bool is_propagate_version_set() const noexcept {
            return is_set(command_setting::propagate_version);
        }

        /** \brief Reports hide(). */
        [[nodiscard]] constexpr bool is_hide_set() const noexcept {
            return is_set(command_setting::hidden);
        }

        /** \brief Reports hide_possible_values(). */
        [[nodiscard]] constexpr bool is_hide_possible_values_set() const noexcept {
            return is_set(command_setting::hide_possible_values);
        }

        /** \brief Reports help_expected(). */
        [[nodiscard]] constexpr bool is_help_expected_set() const noexcept {
            return is_set(command_setting::help_expected);
        }

        /** \brief Reports no_binary_name(). */
        [[nodiscard]] constexpr bool is_no_binary_name_set() const noexcept {
            return is_set(command_setting::no_binary_name);
        }

        /** \} */

        /**
         * \brief Build, validate, and promote the whole tree into .rodata (clap build).
         *
         * Order: defer → implied settings → propagate to children → inject help/version
         * → global args → materialise groups → number positionals → fold flags onto
         * args → consistency checks (duplicates, missing ids, positional rules, …) →
         * static promotion. Works on a copy (idempotent for the caller).
         *
         * \return Frozen tree; all pointers have static storage duration.
         * \note Constexpr-ops cost scales with promote/freeze work; large trees may need
         *       a higher `-fconstexpr-ops-limit`.
         */
        [[nodiscard]] consteval command_spec freeze() const {
            command_builder built = *this;
            built.prepare();
            built.check();
            return built.promote();
        }

    private:
        /** The sort key clap gives a command or argument that never set one. */
        static constexpr std::size_t default_display_order = 999;

        // -- build: clap's Command::_build_self, minus the recursion ---------------

        /** Resolve defer() and apply every implied setting, mutating `*this`. */
        constexpr void prepare() {
            if (deferred_.has_value()) {
                command_builder (*transform)(command_builder) = *deferred_;
                deferred_.reset();
                *this = transform(std::move(*this));
                deferred_.reset();
            }

            settings_.insert(global_settings_);

            if (settings_.is_set(command_setting::multicall)) {
                settings_.set(command_setting::subcommand_required);
                settings_.set(command_setting::disable_help_flag);
                settings_.set(command_setting::disable_version_flag);
            }
            if (settings_.is_set(command_setting::args_negate_subcommands))
                settings_.set(command_setting::subcommands_negate_reqs);
            if (external_parser_.has_value())
                settings_.set(command_setting::allow_external_subcommands);
            if (subcommands_.empty()) settings_.set(command_setting::disable_help_subcommand);
            // clap folds this into its `is_disable_version_flag_set()` accessor rather
            // than into the flag word. clapp records it, because a frozen command_spec
            // must be readable without re-deriving anything: a command with no version
            // has no `--version` argument, so the bit has to say so.
            if (!version_.has_value() && !long_version_.has_value())
                settings_.set(command_setting::disable_version_flag);

            for (command_builder &child: subcommands_) propagate_into(child);
            inject_help_and_version();
            propagate_global_args();
            materialise_groups();
            number_positionals();
            propagate_command_flags_onto_args();
        }

        /**
         * \brief Inherit version (if propagate_version), width/styles (parent overwrites),
         *        derived display_name, and global settings into \p child.
         * \param child Subcommand filled in place.
         * \note Width/styles: set parent values replace the child's (clap Extensions::update).
         *       Parent width 0 does not overwrite (clapp unset is 0).
         */
        constexpr void propagate_into(command_builder &child) const {
            if (settings_.is_set(command_setting::propagate_version)) {
                if (version_.has_value() && !child.version_.has_value()) child.version_ = version_;
                if (long_version_.has_value() && !child.long_version_.has_value())
                    child.long_version_ = long_version_;
            }

            // clap's `Extensions::update`: the parent's value wins wherever it has one.
            if (term_width_ != 0) child.term_width_ = term_width_;
            if (max_term_width_ != 0) child.max_term_width_ = max_term_width_;
            if (styles_.has_value()) child.styles_ = styles_;

            // clap's `_build_bin_names_internal`, display_name half. Under multicall the
            // prefix is empty rather than the root's name, for the reason
            // clapp::detail::child_base_path() gives about `bin_name`: `argv[0]` names
            // the applet, so `busybox` is not part of any identity a user can see.
            if (!child.display_name_.has_value()) {
                // Written as statements, not as a nested conditional expression: the
                // "no prefix" arm is an empty `std::string` prvalue, and a
                // `std::string_view` bound to it dangles the moment the full expression
                // ends. Neither compiler diagnoses that (CLAUDE.md trap 12).
                std::string_view prefix{};
                if (display_name_.has_value()) {
                    prefix = std::string_view{*display_name_};
                } else if (!settings_.is_set(command_setting::multicall)) {
                    prefix = std::string_view{name_};
                }
                child.display_name_ = detail::join_display_name(prefix, child.name_);
            }

            child.settings_.insert(global_settings_);
            child.global_settings_.insert(global_settings_);
        }

        /**
         * \brief Whether --help has more content than -h (drives injected help text).
         * \return True if long_about / before|after_long_help / arg long_help exist.
         */
        [[nodiscard]] constexpr bool long_help_exists() const noexcept {
            return detail::long_help_exists_over(
                long_about_, before_long_help_, after_long_help_, args_);
        }

        /** clap's `_check_help_and_version`, fast path (no expanded help subtree). */
        constexpr void inject_help_and_version() {
            if (!settings_.is_set(command_setting::disable_help_flag)) {
                arg_builder help_flag("help");
                std::move(help_flag).short_('h').long_("help").action(arg_action::help);
                if (long_help_exists()) {
                    std::move(help_flag)
                            .help("Print help (see more with '--help')")
                            .long_help("Print help (see a summary with '-h')");
                } else {
                    std::move(help_flag).help("Print help");
                }
                // Pushed directly rather than through arg(): clap deliberately keeps the
                // injected flags out of next_help_heading() / next_display_order().
                args_.push_back(std::move(help_flag));
            }
            if (!settings_.is_set(command_setting::disable_version_flag)) {
                arg_builder version_flag("version");
                std::move(version_flag)
                        .short_('V')
                        .long_("version")
                        .action(arg_action::version)
                        .help("Print version");
                args_.push_back(std::move(version_flag));
            }
            if (!settings_.is_set(command_setting::disable_help_subcommand)) {
                arg_builder topic("subcommand");
                std::move(topic)
                        .action(arg_action::append)
                        .num_args(value_range::full())
                        .value_name("COMMAND")
                        .help("Print help for the subcommand(s)");

                command_builder help_command{detail::default_help_subcommand_name};
                std::move(help_command)
                        .about("Print this message or the help of the given subcommand(s)")
                        .arg(std::move(topic));
                propagate_into(help_command);
                help_command.version_ = std::nullopt;
                help_command.long_version_ = std::nullopt;
                std::move(help_command)
                        .setting(command_setting::disable_help_flag)
                        .setting(command_setting::disable_version_flag)
                        .unset_global_setting(command_setting::propagate_version);
                subcommands_.push_back(std::move(help_command));
            }
        }

        /**
         * clap's `_propagate_global_args`. The auto-generated `help` subtree is
         * skipped so completions do not offer the parent's options after `help`.
         */
        constexpr void propagate_global_args() {
            const bool generated_help_subcommand =
                    !settings_.is_set(command_setting::disable_help_subcommand);
            for (command_builder &child: subcommands_) {
                if (generated_help_subcommand &&
                    child.name_ == detail::default_help_subcommand_name)
                    continue;
                for (const arg_builder &candidate: args_) {
                    if (!candidate.is_global_set()) continue;
                    if (child.has_arg(candidate.get_id())) continue;
                    child.args_.push_back(candidate);
                }
            }
        }

        /**
         * Turn `arg_builder::group("io")` into membership of a real group, creating
         * the group when no one declared it. clap does the same, except that it pushes
         * unconditionally and would list a member twice when the author *also* wrote
         * `group_builder("io").args({...})`; clapp checks first, because
         * clapp::group_builder::freeze() rejects a duplicated member.
         */
        constexpr void materialise_groups() {
            // Index-based: groups_ is appended to inside the loop, so a reference into
            // it would dangle the moment the vector reallocates.
            for (std::size_t a = 0; a < args_.size(); ++a) {
                for (const std::string &wanted: args_[a].get_groups()) {
                    const std::string_view member = args_[a].get_id();
                    std::size_t slot = groups_.size();
                    for (std::size_t g = 0; g < groups_.size(); ++g) {
                        if (groups_[g].get_id() == wanted) {
                            slot = g;
                            break;
                        }
                    }
                    if (slot == groups_.size()) groups_.emplace_back(wanted);
                    if (!groups_[slot].contains(member)) std::move(groups_[slot]).arg(member);
                }
            }
        }

        /** Assign 1-based slots to positionals that did not choose one, in order. */
        constexpr void number_positionals() {
            std::size_t next = 1;
            for (arg_builder &candidate: args_) {
                if (candidate.is_positional() && !candidate.get_index().has_value()) {
                    std::move(candidate).index(next);
                    ++next;
                }
            }
        }

        /**
         * The command-level knobs that are really shorthand for setting the same knob
         * on every argument. clap applies them at the end of `_build_self`.
         */
        constexpr void propagate_command_flags_onto_args() {
            const bool hide_values = settings_.is_set(command_setting::hide_possible_values);
            const bool hyphens = settings_.is_set(command_setting::allow_hyphen_values);
            const bool negatives = settings_.is_set(command_setting::allow_negative_numbers);
            const bool var_arg = settings_.is_set(command_setting::trailing_var_arg);
            const std::size_t final = highest_index();
            for (arg_builder &candidate: args_) {
                const bool takes_values = candidate.is_takes_value_set();
                if (hide_values && takes_values) std::move(candidate).hide_possible_values();
                if (hyphens && takes_values) std::move(candidate).allow_hyphen_values();
                if (negatives && takes_values) std::move(candidate).allow_negative_numbers();
                if (var_arg && candidate.get_index() == final)
                    std::move(candidate).trailing_var_arg();
            }
        }

        /**
         * The largest positional slot in use, or 0 when there are no positionals.
         *
         * Only *positionals* are counted. An `index()` on something with a short or
         * long option is a mistake clapp::arg_builder::freeze() reports precisely;
         * letting it inflate this number here would replace that diagnostic with a
         * confusing complaint about a gap in the positional indices.
         */
        [[nodiscard]] constexpr std::size_t highest_index() const noexcept {
            auto slots = args_ | std::views::filter(&arg_builder::is_positional) |
                         std::views::transform([](const arg_builder &candidate) {
                             return candidate.get_index().value_or(0);
                         });
            return std::ranges::fold_left(
                slots, std::size_t{0}, [](std::size_t best, std::size_t slot) {
                    return slot > best ? slot : best;
                });
        }

        /**
         * \brief Index in args_ of the positional in slot \p slot, or nullopt.
         * \note Returns index not pointer (ubsan-safe presence test).
         */
        [[nodiscard]] constexpr std::optional<std::size_t>
        positional_at(std::size_t slot) const noexcept {
            for (std::size_t i = 0; i < args_.size(); ++i) {
                if (args_[i].is_positional() && args_[i].get_index() == slot) return i;
            }
            return std::nullopt;
        }

        /** Whether \p id names an argument or a group of this command. */
        [[nodiscard]] constexpr bool id_exists(std::string_view id) const noexcept {
            return has_arg(id) || has_group(id);
        }

        // -- check: clap's debug_asserts.rs, at compile time ------------------------

        /** Fail the build unless \p id names an argument or group of this command. */
        consteval void
        require_id(std::string_view id, std::string_view relation, std::string_view owner) const {
            if (id_exists(id)) return;
            detail::fail({
                "clapp::command_builder::freeze: command '",
                name_,
                "': '",
                relation,
                "' on '",
                owner,
                "' names '",
                id,
                "', which is neither an argument nor a group of this command"
            });
        }

        /**
         * Run every consistency check. Called on the *prepared* copy, so the injected
         * arguments and materialised groups are checked alongside the author's own.
         */
        consteval void check() const {
            if (name_.empty())
                detail::fail({"clapp::command_builder::freeze: a command name must not be empty"});

            check_version();
            check_settings();
            check_arguments();
            check_groups();
            check_subcommands();
            check_identifiers();
            check_flag_namespace();
            // Before check_positionals(), matching clap: a shared slot lives in
            // `assert_app`, and reporting it first keeps the diagnostic precise. The
            // gap rule would otherwise fire on the same input and complain about a
            // "missing" index that is really a duplicated one.
            check_positional_slots();
            check_positionals();
            check_help_expected();
        }

        /**
         * clap's `assert_app_flags`: the one pair of command settings that cannot both
         * be on.
         */
        consteval void check_settings() const {
            if (settings_.is_set(command_setting::multicall) &&
                settings_.is_set(command_setting::no_binary_name)) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': multicall() and no_binary_name() contradict each other; a "
                    "multicall command dispatches on argv[0], which no_binary_name() "
                    "says is not there"
                });
            }
        }

        /**
         * clap's `_panic_on_missing_help`: under help_expected(), every argument must
         * carry help() or long_help().
         *
         * \note Not recursive here, unlike clap's, because help_expected() is a
         *       global setting: every subcommand inherits the bit and runs its own
         *       freeze(), which runs its own copy of this check.
         */
        consteval void check_help_expected() const {
            if (!settings_.is_set(command_setting::help_expected)) return;
            for (const arg_builder &candidate: args_) {
                if (candidate.get_help().has_value() || candidate.get_long_help().has_value())
                    continue;
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': help_expected() is set but the argument '",
                    candidate.get_id(),
                    "' has neither help() nor long_help()"
                });
            }
        }

        /** version()/long_version() must exist before anything can advertise one. */
        consteval void check_version() const {
            if (version_.has_value() || long_version_.has_value()) return;
            if (settings_.is_set(command_setting::propagate_version)) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                        "': propagate_version() has nothing to propagate; call version() ",
                    "or long_version() first"
                });
            }
            for (const arg_builder &candidate: args_) {
                if (candidate.get_action() == arg_action::version) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': the argument '",
                        candidate.get_id(),
                        "' uses arg_action::version, but the command has neither ",
                        "version() nor long_version()"
                    });
                }
            }
        }

        /**
         * \brief Named arg_shape checks (max_num_args, index, value_names) for \p one.
         * \param one Argument to inspect. \param id Its id.
         * \note Duplicates four arg_builder::freeze() rejections so diagnostics can name
         *       the argument (fail() lives here). Keep both copies.
         */
        consteval void check_arg_shape(const arg_builder &one, std::string_view id) const {
            const arg_action act = one.get_action();
            const value_range nargs = one.get_num_args();

            if (!nargs.is_within(max_num_args(act))) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    id,
                    "' has action '",
                    name_of(act),
                    "', which is incompatible with the num_args it was given; that ",
                    "action accepts at most ",
                    detail::spell_number(max_num_args(act).max_values()),
                    " value(s)"
                });
            }
            if (one.get_index().has_value() && !one.is_positional()) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    id,
                    "' has index(), so it is a positional, and a positional cannot ",
                    "also have a short or long option"
                });
            }
            if (one.get_index().has_value() && !nargs.takes_values()) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    id,
                    "' is a positional and must take a value, but its action '",
                    name_of(act),
                    "' takes none"
                });
            }
            if (nargs.takes_values() && nargs.max_values() < one.get_value_names().size()) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    id,
                    "' has ",
                    detail::spell_number(one.get_value_names().size()),
                    " value_name()s but a num_args that can never fill more than ",
                    detail::spell_number(nargs.max_values())
                });
            }
        }

        consteval void check_arguments() const {
            const bool multicall_set = settings_.is_set(command_setting::multicall);
            for (const arg_builder &one: args_) {
                const std::string_view id = one.get_id();

                if (multicall_set) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': a multicall command dispatches on argv[0] and cannot ",
                        "have arguments, but '",
                        id,
                        "' was added"
                    });
                }
                if (const std::optional<std::string_view> spelling = one.get_long()) {
                    if (spelling->starts_with('-')) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': the long option of '",
                            id,
                            "' is '",
                            *spelling,
                            "'; write it without the leading '-', the parser adds it"
                        });
                    }
                }
                check_arg_shape(one, id);
                if (one.is_required_set() && one.is_global_set()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': '",
                        id,
                        "' is both required() and global(); a global argument reaches ",
                        "subcommands that may not need it, so it may not be required"
                    });
                }
                if (one.is_last_set() && !one.is_positional()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': '",
                        id,
                        "' has last() together with a short or long option; last() is ",
                        "for positionals reachable only after '--'"
                    });
                }
                if (one.is_required_set() && !one.get_required_if_eq_any().empty()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': '",
                        id,
                        "' is required() and also required_if_eq(); the condition ",
                        "could never make a difference"
                    });
                }
                if (one.is_required_set() && !one.get_required_if_eq_all().empty()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': '",
                        id,
                        "' is required() and also required_if_eq_all(); the condition ",
                        "could never make a difference"
                    });
                }
                if (one.is_required_set() && !one.get_required_unless_present_any().empty()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': '",
                        id,
                        "' is required() and also required_unless_present(); the ",
                        "exception could never apply"
                    });
                }
                if (one.is_required_set() && !one.get_required_unless_present_all().empty()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': '",
                        id,
                        "' is required() and also required_unless_present_all(); the ",
                        "exception could never apply"
                    });
                }
                if (one.get_value_hint() == clapp::value_hint::command_with_arguments) {
                    if (!one.is_positional()) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            id,
                            "' has value_hint::command_with_arguments, which is only ",
                            "meaningful on a positional"
                        });
                    }
                    if (!one.is_trailing_var_arg_set() && !one.is_last_set()) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            id,
                            "' has value_hint::command_with_arguments, so it must also ",
                            "have trailing_var_arg() or last(); otherwise the nested ",
                            "command's own flags are read as this command's"
                        });
                    }
                }
                for (const std::string &other: one.get_conflicts()) {
                    if (other == id) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            id,
                            "' conflicts with itself"
                        });
                    }
                    require_id(other, "conflicts_with", id);
                }
                for (const std::string &other: one.get_overrides())
                    require_id(other, "overrides_with", id);
                for (const arg_requirement &rule: one.get_requires()) {
                    if (rule.target == id) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            id,
                            "' requires itself"
                        });
                    }
                    require_id(rule.target, "requires", id);
                }
                for (const std::string &other: one.get_required_unless_present_any())
                    require_id(other, "required_unless_present_any", id);
                for (const std::string &other: one.get_required_unless_present_all())
                    require_id(other, "required_unless_present_all", id);
                for (const arg_value_requirement &rule: one.get_required_if_eq_any())
                    require_id(rule.id, "required_if_eq_any", id);
                for (const arg_value_requirement &rule: one.get_required_if_eq_all())
                    require_id(rule.id, "required_if_eq_all", id);
            }
        }

        /** Ids are one namespace shared by arguments and groups: one sorted scan. */
        consteval void check_identifiers() const {
            std::vector<detail::claim<std::string_view> > ids;
            ids.reserve(args_.size() + groups_.size());
            for (std::size_t i = 0; i < args_.size(); ++i) {
                const std::string_view id = args_[i].get_id();
                ids.push_back({id, id, detail::owner_kind::argument, i});
            }
            for (std::size_t i = 0; i < groups_.size(); ++i) {
                const std::string_view id = groups_[i].get_id();
                ids.push_back({id, id, detail::owner_kind::group, i});
            }
            const std::optional clash = detail::find_duplicate(ids);
            if (!clash.has_value()) return;
            const bool same_kind = clash->first.kind == clash->second.kind;
            detail::fail({
                "clapp::command_builder::freeze: command '",
                name_,
                "': the id '",
                clash->first.token,
                "' is claimed ",
                same_kind ? "twice by " : "by both ",
                detail::name_of(clash->first.kind),
                same_kind ? "s" : " and ",
                same_kind ? "" : detail::name_of(clash->second.kind),
                "; arguments and groups share one id namespace, and a match reported ",
                "under an ambiguous name is unusable",
                duplicate_hint(clash->first.token, clash->second.token)
            });
        }

        /**
         * clap's `assert_app` flag tables plus `detect_duplicate_flags`: every claim
         * on `--long` and on `-s`, from all four sources, scanned once each.
         *
         * The namespace is wider than a naive reading suggests. `--x` can be claimed
         * by an argument's long_(), by one of that argument's alias()es, by a
         * subcommand's long_flag(), and by one of that subcommand's
         * long_flag_alias()es. Omitting any of the four does not remove the
         * ambiguity — it only defers discovery to the parser, which will dispatch
         * `--x` to whichever owner it happens to scan first.
         */
        consteval void check_flag_namespace() const {
            std::vector<detail::claim<std::string_view> > longs;
            std::vector<detail::claim<char> > shorts;
            for (std::size_t i = 0; i < args_.size(); ++i) {
                const arg_builder &one = args_[i];
                const std::string_view id = one.get_id();
                if (const std::optional<std::string_view> spelling = one.get_long())
                    longs.push_back({*spelling, id, detail::owner_kind::argument, i});
                for (const arg_alias &alias: one.get_all_aliases())
                    longs.push_back({alias.name, id, detail::owner_kind::argument, i});
                if (const std::optional<char> letter = one.get_short())
                    shorts.push_back({*letter, id, detail::owner_kind::argument, i});
                for (const arg_short_alias &alias: one.get_all_short_aliases())
                    shorts.push_back({alias.name, id, detail::owner_kind::argument, i});
            }
            for (std::size_t i = 0; i < subcommands_.size(); ++i) {
                const command_builder &child = subcommands_[i];
                const std::string_view sub = child.name_;
                if (child.long_flag_.has_value())
                    longs.push_back({*child.long_flag_, sub, detail::owner_kind::subcommand, i});
                for (const arg_alias &alias: child.long_flag_aliases_)
                    longs.push_back({alias.name, sub, detail::owner_kind::subcommand, i});
                if (child.short_flag_.has_value())
                    shorts.push_back({*child.short_flag_, sub, detail::owner_kind::subcommand, i});
                for (const arg_short_alias &alias: child.short_flag_aliases_)
                    shorts.push_back({alias.name, sub, detail::owner_kind::subcommand, i});
            }

            if (const std::optional clash = detail::find_duplicate(longs)) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '--",
                    clash->first.token,
                    "' is claimed by both ",
                    detail::name_of(clash->first.kind),
                    " '",
                    clash->first.owner,
                    "' and ",
                    detail::name_of(clash->second.kind),
                    " '",
                    clash->second.owner,
                    "'",
                    duplicate_hint(clash->first.owner, clash->second.owner)
                });
            }
            if (const std::optional clash = detail::find_duplicate(shorts)) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '-",
                    std::string_view{&clash->first.token, 1},
                    "' is claimed by both ",
                    detail::name_of(clash->first.kind),
                    " '",
                    clash->first.owner,
                    "' and ",
                    detail::name_of(clash->second.kind),
                    " '",
                    clash->second.owner,
                    "'",
                    duplicate_hint(clash->first.owner, clash->second.owner)
                });
            }
        }

        /**
         * The tip clap's `duplicate_tip` appends when the collision involves one of
         * the arguments clapp injected rather than one the author wrote.
         */
        [[nodiscard]] constexpr std::string_view
        duplicate_hint(std::string_view one, std::string_view two) const noexcept {
            if ((one == "help" || two == "help") &&
                !settings_.is_set(command_setting::disable_help_flag))
                return " (call disable_help_flag() to drop the injected '--help')";
            if ((one == "version" || two == "version") &&
                !settings_.is_set(command_setting::disable_version_flag))
                return " (call disable_version_flag() to drop the injected '--version')";
            return "";
        }

        /**
         * Every id a group mentions must exist.
         *
         * \note Group-id uniqueness, and the rule that a group may not shadow an
         *       argument, live in check_identifiers(): both are questions about the
         *       single id namespace, and answering them there costs one sorted scan
         *       instead of two nested loops.
         */
        consteval void check_groups() const {
            for (const group_builder &group: groups_) {
                const std::string_view id = group.get_id();
                for (const std::string &member: group.get_args()) {
                    if (!has_arg(member)) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': the group '",
                            id,
                            "' lists '",
                            member,
                            "', which is not an argument of this command"
                        });
                    }
                }
                for (const std::string &other: group.get_requires())
                    require_id(other, "group requires", id);
                for (const std::string &other: group.get_conflicts())
                    require_id(other, "group conflicts_with", id);
            }
        }

        /**
         * Subcommand names and aliases must be unambiguous, and a subcommand flag must
         * be spelled without its dashes.
         *
         * \note The *flag* collisions — subcommand against argument, and subcommand
         *       against subcommand — belong to check_flag_namespace(), which sees all
         *       four sources of a `--long` claim rather than the two visible here.
         */
        consteval void check_subcommands() const {
            for (std::size_t i = 0; i < subcommands_.size(); ++i) {
                const command_builder &one = subcommands_[i];
                if (one.long_flag_.has_value() && one.long_flag_->starts_with('-')) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': the long flag of the subcommand '",
                        one.name_,
                        "' is '",
                        *one.long_flag_,
                        "'; write it without the leading '-', the parser adds it"
                    });
                }
                for (const arg_alias &alias: one.long_flag_aliases_) {
                    if (!alias.name.starts_with('-')) continue;
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': the long-flag alias '",
                        alias.name,
                        "' of the subcommand '",
                        one.name_,
                        "' starts with '-'; write it without, the parser adds it"
                    });
                }
                for (std::size_t j = i + 1; j < subcommands_.size(); ++j) {
                    const command_builder &two = subcommands_[j];
                    if (two.name_ == one.name_) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': the subcommand name '",
                            one.name_,
                            "' is declared twice"
                        });
                    }
                    if (two.aliases_to_name(one.name_)) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': the subcommand '",
                            two.name_,
                            "' has an alias '",
                            one.name_,
                            "' that is already the name of another ",
                            "subcommand"
                        });
                    }
                    for (const arg_alias &alias: one.aliases_) {
                        if (two.name_ == alias.name || two.aliases_to_name(alias.name)) {
                            detail::fail({
                                "clapp::command_builder::freeze: command '",
                                name_,
                                "': the alias '",
                                alias.name,
                                "' of the subcommand '",
                                one.name_,
                                "' collides with the subcommand '",
                                two.name_,
                                "'"
                            });
                        }
                    }
                }
            }
        }

        /** Whether \p candidate is one of this command's aliases. */
        [[nodiscard]] constexpr bool aliases_to_name(std::string_view candidate) const noexcept {
            return std::ranges::any_of(aliases_,
                                       [&](const arg_alias &a) { return a.name == candidate; });
        }

        /**
         * \brief Positional index gaps, trailing collectors, multi-value placement.
         * \warning is_multiple() (accumulates, includes append) vs is_multiple_values_set()
         *          (one occurrence arity) are not interchangeable — clap uses the first
         *          for trailing/order and the second for unbounded collectors.
         */
        consteval void check_positionals() const {
            const auto positional_count = static_cast<std::size_t>(
                std::ranges::count_if(args_, &arg_builder::is_positional));
            const std::size_t highest = highest_index();
            if (highest != positional_count) {
                // The counts alone say a gap exists but not where, and the author has to
                // read every index back out of the struct to find it. ADR-0008's standard
                // is that a compile-time diagnostic names the offending entity, so the
                // slots are spelled out. Built with push_back for the ubsan reason on
                // detail::fail(); `slots` outlives the call because fail() copies bytes
                // out of its pieces before it throws.
                std::string slots;
                for (const arg_builder &positional: args_) {
                    if (!positional.is_positional()) continue;
                    for (const char byte: std::string_view{slots.empty() ? "" : ", "})
                        slots.push_back(byte);
                    slots.push_back('\'');
                    for (const char byte: positional.get_id()) slots.push_back(byte);
                    for (const char byte: std::string_view{"' at "}) slots.push_back(byte);
                    for (const char byte: detail::spell_number(positional.get_index().value_or(0)))
                        slots.push_back(byte);
                }
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': the highest positional index is ",
                    detail::spell_number(highest),
                    " but there are ",
                    detail::spell_number(positional_count),
                    " positionals (",
                    slots,
                    "); the indices must be 1..n without gaps"
                });
            }

            std::size_t last_count = 0;
            for (const arg_builder &candidate: args_) {
                if (!candidate.is_positional()) continue;
                if (candidate.is_last_set()) ++last_count;
                const bool is_final = candidate.get_index() == highest;
                if (candidate.is_trailing_var_arg_set()) {
                    if (!is_final) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            candidate.get_id(),
                            "' has trailing_var_arg() but is not the last positional"
                        });
                    }
                    if (candidate.is_last_set()) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            candidate.get_id(),
                            "' has both trailing_var_arg() and last(); the first says ",
                            "'swallow the rest', the second 'only after --'"
                        });
                    }
                    if (!candidate.is_multiple()) {
                        detail::fail({
                            "clapp::command_builder::freeze: command '",
                            name_,
                            "': '",
                            candidate.get_id(),
                            "' has trailing_var_arg() but accepts a single value; give ",
                            "it num_args(1..) or action(append)"
                        });
                    }
                }
            }
            if (last_count > 1) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': more than one positional has last(); only one argument can sit "
                    "behind the '--' escape"
                });
            }

            check_required_positional_order(highest);

            if (highest < 2) return;
            const std::optional<std::size_t> last_slot = positional_at(highest);
            const std::optional<std::size_t> prev_slot = positional_at(highest - 1);
            if (!last_slot.has_value() || !prev_slot.has_value()) return;
            const arg_builder &final_positional = args_[*last_slot];
            const arg_builder &penultimate = args_[*prev_slot];

            // clap's `only_highest`: is there an accumulating positional that is not
            // the last one? Everything below is about what that costs.
            //
            // `find_if` plus an end comparison rather than a running `const arg_builder*`
            // that starts null: both iterators come from the same `args_` allocation, so
            // that comparison folds during constant evaluation, while a comparison
            // against `nullptr` does not under `-fsanitize=null` (see clapp::arg_id).
            const auto early = std::ranges::find_if(args_, [highest](const arg_builder &c) {
                return c.is_positional() && c.is_multiple() && c.get_index() != highest;
            });
            if (early == args_.end()) return;

            // (1) The parser needs a wall to stop at.
            const bool guarded =
                    final_positional.is_required_set() || final_positional.is_last_set() ||
                    penultimate.get_value_terminator().has_value() || penultimate.is_last_set();
            if (!guarded) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    early->get_id(),
                    "' takes several values but is not the last positional, so the ",
                    "parser cannot tell where it ends; make '",
                    final_positional.get_id(),
                    "' required(), or give the preceding positional a ",
                    "value_terminator()"
                });
            }

            // (2) And that wall has to be within one slot of the end.
            if (!penultimate.is_multiple() && !final_positional.is_last_set()) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    early->get_id(),
                    "' takes several values but is neither the last positional nor the ",
                    "one before it; only those two slots may accumulate, unless '",
                    final_positional.get_id(),
                    "' has last()"
                });
            }

            // (3) Two unbounded collectors are ambiguous however they are ordered —
            // unless the second one is fenced off behind `--`.
            const auto unbounded = static_cast<std::size_t>(
                std::ranges::count_if(args_, [](const arg_builder &candidate) {
                    return candidate.is_positional() && candidate.is_multiple_values_set() &&
                           !candidate.get_value_terminator().has_value() &&
                           !candidate.get_num_args().is_fixed();
                }));
            const bool splittable = final_positional.is_last_set() &&
                                    final_positional.is_multiple() && penultimate.is_multiple() &&
                                    unbounded == 2;
            if (unbounded > 1 && !splittable) {
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': ",
                    detail::spell_number(unbounded),
                    " positionals have an unbounded num_args(1..); at most one may, ",
                    "unless the last one also has last() so '--' separates them"
                });
            }
        }

        /**
         * Two positionals may not claim the same slot: one sorted scan, not a pair
         * loop, for the reason clapp::detail::find_duplicate() gives.
         */
        consteval void check_positional_slots() const {
            std::vector<detail::claim<std::size_t> > slots;
            for (std::size_t i = 0; i < args_.size(); ++i) {
                const arg_builder &candidate = args_[i];
                if (!candidate.is_positional()) continue;
                const std::optional<std::size_t> slot = candidate.get_index();
                if (!slot.has_value()) continue;
                slots.push_back({*slot, candidate.get_id(), detail::owner_kind::argument, i});
            }
            const std::optional clash = detail::find_duplicate(slots);
            if (!clash.has_value()) return;
            detail::fail({
                "clapp::command_builder::freeze: command '",
                name_,
                "': the positionals '",
                clash->first.owner,
                "' and '",
                clash->second.owner,
                "' claim index ",
                detail::spell_number(clash->first.token),
                "; use num_args(1..) if one of them should take several values"
            });
        }

        /**
         * Once a positional is required, every earlier one must be too — otherwise
         * `prog A [B] C` cannot decide whether a lone extra value is `B` or `C`.
         *
         * \param highest The largest positional slot in use.
         *
         * \note Two exemptions, both clap's. A `last()` positional sits behind `--`
         *       and therefore does not constrain what precedes it, and
         *       allow_missing_positional() opts out of the rule entirely because it
         *       exists precisely to allow `prog [opt] <req>`.
         *
         * \note A reverse index scan rather than a range pipeline: the answer depends
         *       on positional *order*, and `positional_at()` is the only thing that
         *       knows it — declaration order and slot order need not agree once an
         *       explicit index() is in play.
         */
        consteval void check_required_positional_order(std::size_t highest) const {
            if (settings_.is_set(command_setting::allow_missing_positional)) return;
            std::string_view culprit;
            for (std::size_t slot = highest; slot >= 1; --slot) {
                const std::optional<std::size_t> at = positional_at(slot);
                if (!at.has_value()) continue;
                const arg_builder &here = args_[*at];
                if (!culprit.empty() && !here.is_required_set()) {
                    detail::fail({
                        "clapp::command_builder::freeze: command '",
                        name_,
                        "': the positional '",
                        here.get_id(),
                        "' is optional but comes before the required positional '",
                        culprit,
                        "'; either make it required() too or set ",
                        "allow_missing_positional()"
                    });
                }
                if (here.is_required_set() && !here.is_last_set()) culprit = here.get_id();
            }
            // A required `last()` positional plus subcommands is ambiguous: the parser
            // cannot both require a value after `--` and dispatch to a child.
            if (subcommands_.empty()) return;
            if (settings_.is_set(command_setting::subcommands_negate_reqs)) return;
            for (const arg_builder &candidate: args_) {
                if (!candidate.is_positional() || !candidate.is_last_set()) continue;
                if (!candidate.is_required_set()) continue;
                detail::fail({
                    "clapp::command_builder::freeze: command '",
                    name_,
                    "': '",
                    candidate.get_id(),
                    "' is a required last() positional and this command also has ",
                    "subcommands; set subcommand_negates_reqs() to say which wins"
                });
            }
        }

        // -- promote: everything into static storage ------------------------------

        /**
         * Freeze the prepared tree. Each subcommand runs its own prepare/check/promote
         * through freeze(), which is what makes the recursion depth-first.
         */
        [[nodiscard]] consteval command_spec promote() const {
            std::vector<arg_spec> frozen_args;
            frozen_args.reserve(args_.size());
            for (const arg_builder &candidate: args_) frozen_args.push_back(candidate.freeze());

            std::vector<group_spec> frozen_groups;
            frozen_groups.reserve(groups_.size());
            for (const group_builder &group: groups_) frozen_groups.push_back(group.freeze());

            std::vector<command_spec> frozen_subs;
            frozen_subs.reserve(subcommands_.size());
            for (const command_builder &child: subcommands_) frozen_subs.push_back(child.freeze());

            const std::span<const arg_spec> args = frozen_args.empty()
                                                       ? std::span<const arg_spec>{}
                                                       : std::define_static_array(frozen_args);
            const std::span<const group_spec> groups =
                    frozen_groups.empty()
                        ? std::span<const group_spec>{}
                        : std::define_static_array(frozen_groups);
            const std::span<const command_spec> subs =
                    frozen_subs.empty()
                        ? std::span<const command_spec>{}
                        : std::define_static_array(frozen_subs);

            const std::span<const alias_spec> aliases = detail::promote_aliases(aliases_);
            const std::span<const short_alias_spec> short_flag_aliases =
                    detail::promote_short_aliases(short_flag_aliases_);
            const std::span<const alias_spec> long_flag_aliases =
                    detail::promote_aliases(long_flag_aliases_);

            const detail::static_text author = detail::promote_text(author_);
            const detail::static_text about = detail::promote_text(about_);
            const detail::static_text long_about = detail::promote_text(long_about_);
            const detail::static_text before_help = detail::promote_text(before_help_);
            const detail::static_text before_long_help = detail::promote_text(before_long_help_);
            const detail::static_text after_help = detail::promote_text(after_help_);
            const detail::static_text after_long_help = detail::promote_text(after_long_help_);
            const detail::static_text override_usage = detail::promote_text(override_usage_);
            const detail::static_text override_help = detail::promote_text(override_help_);
            const detail::static_text help_template = detail::promote_text(help_template_);
            const detail::static_text next_heading = detail::promote_text(help_heading_);
            const detail::static_text sub_heading = detail::promote_text(subcommand_help_heading_);

            // `clapp::` is load-bearing here: inside this class the unqualified name
            // `styles` finds the member function styles(), not the type.
            // Never null: a command that chose no palette points at clapp's default,
            // so command_spec::get_styles() needs no null test — and must not have
            // one, since a pointer comparison is not a constant expression under
            // `-fsanitize=null` (see the length-sentinel note on clapp::arg_id).
            const clapp::styles *palette =
                    styles_.has_value()
                        ? std::define_static_array(std::vector<clapp::styles>{*styles_}).data()
                        : &detail::default_styles;

            return command_spec{
                .name = make_static_id(name_),
                .bin_name = detail::promote_token(bin_name_),
                .display_name = detail::promote_token(display_name_),
                .short_flag = short_flag_.value_or('\0'),
                .external_parser_present = external_parser_.has_value(),
                .long_flag = detail::promote_token(long_flag_),
                .alias_data = aliases.data(),
                .alias_count = aliases.size(),
                .short_flag_alias_data = short_flag_aliases.data(),
                .short_flag_alias_count = short_flag_aliases.size(),
                .long_flag_alias_data = long_flag_aliases.data(),
                .long_flag_alias_count = long_flag_aliases.size(),
                .version = detail::promote_token(version_),
                .long_version = detail::promote_token(long_version_),
                .arg_data = args.data(),
                .arg_count = args.size(),
                .group_data = groups.data(),
                .group_count = groups.size(),
                .sub_data = subs.data(),
                .sub_count = subs.size(),
                .external_parser = external_parser_.value_or(nullptr),
                .author_text = author.data,
                .author_length = author.size,
                .about_text = about.data,
                .about_length = about.size,
                .long_about_text = long_about.data,
                .long_about_length = long_about.size,
                .before_help_text = before_help.data,
                .before_help_length = before_help.size,
                .before_long_help_text = before_long_help.data,
                .before_long_help_length = before_long_help.size,
                .after_help_text = after_help.data,
                .after_help_length = after_help.size,
                .after_long_help_text = after_long_help.data,
                .after_long_help_length = after_long_help.size,
                .override_usage_text = override_usage.data,
                .override_usage_length = override_usage.size,
                .override_help_text = override_help.data,
                .override_help_length = override_help.size,
                .help_template_text = help_template.data,
                .help_template_length = help_template.size,
                .next_help_heading_text = next_heading.data,
                .next_help_heading_length = next_heading.size,
                .subcommand_help_heading_text = sub_heading.data,
                .subcommand_help_heading_length = sub_heading.size,
                .subcommand_value_name = detail::promote_token(subcommand_value_name_),
                .display_order = get_display_order(),
                .term_width = term_width_,
                .max_term_width = max_term_width_,
                .style_data = palette,
                .settings = settings_,
                .global_settings = global_settings_,
            };
        }

        std::string name_;
        std::optional<std::string> bin_name_;
        std::optional<std::string> display_name_;
        std::optional<std::string> version_;
        std::optional<std::string> long_version_;
        std::optional<std::string> author_;
        std::optional<std::string> about_;
        std::optional<std::string> long_about_;
        std::optional<std::string> before_help_;
        std::optional<std::string> before_long_help_;
        std::optional<std::string> after_help_;
        std::optional<std::string> after_long_help_;
        std::optional<std::string> override_usage_;
        std::optional<std::string> override_help_;
        std::optional<std::string> help_template_;
        std::optional<std::string> help_heading_;
        std::optional<std::string> subcommand_value_name_;
        std::optional<std::string> subcommand_help_heading_;
        std::optional<char> short_flag_;
        std::optional<std::string> long_flag_;
        std::vector<arg_alias> aliases_;
        std::vector<arg_short_alias> short_flag_aliases_;
        std::vector<arg_alias> long_flag_aliases_;
        std::vector<arg_builder> args_;
        std::vector<group_builder> groups_;
        std::vector<command_builder> subcommands_;
        std::optional<const parser_vtable *> external_parser_;
        std::optional<clapp::styles> styles_;
        std::optional<std::size_t> disp_ord_;
        /**
         * Starts at 0, not empty: clap's `Command::new` sets `current_disp_ord:
         * Some(0)`, so every named argument and every subcommand is numbered in
         * declaration order unless it brought its own key. Leaving it empty made every
         * argument keep the 999 default, which is invisible until something renders
         * help — and then makes `Options:` come out sorted by option letter instead of
         * by declaration order. Measured against clap 4.6's `args_with_last_usage`.
         */
        std::optional<std::size_t> disp_ord_cursor_ = 0;
        std::size_t term_width_ = 0;
        std::size_t max_term_width_ = 0;
        command_flags settings_{};
        command_flags global_settings_{};
        std::optional<command_builder (*)(command_builder)> deferred_;
    };

    namespace detail {
        /**
         * Compile-time contract: a clapp::command_spec must stay structural, or a
         * command tree cannot be promoted into `.rodata` with
         * `std::define_static_array` — which is exactly how a subcommand list is
         * built, so failing this would make the type useless rather than merely
         * inconvenient.
         */
        template<command_flags>
        struct command_flags_probe {
        };

        template<command_spec>
        struct command_spec_probe {
        };

        /** \brief Proof that clapp::command_flags is a structural type. */
        using command_flags_is_structural = command_flags_probe<command_flags{}>;
        /** \brief Proof that clapp::command_spec is a structural type. */
        using command_spec_is_structural = command_spec_probe<command_spec{}>;

        /**
         * The bit layout mirrors clap's `AppSettings`, and a reordered
         * clapp::command_setting would silently change the meaning of a stored word.
         */
        static_assert(command_flags::bit_of(command_setting::ignore_errors) == 1u);
        static_assert(command_flags::bit_of(command_setting::multicall) == 2'048U);
        static_assert(command_flags::bit_of(command_setting::no_binary_name) == 67'108'864U);
        static_assert(command_flags::bit_of(command_setting::color_never) == 536'870'912U);
        static_assert(all_command_settings.size() == command_setting_count);

        /**
         * A default-constructed clapp::command_spec must be the neutral command: no
         * name, nothing set, no children. `command_of<T>()` relies on that when it
         * creates a placeholder before it has walked the reflected type.
         */
        static_assert(command_spec{}.get_name().empty());
        static_assert(!command_spec{}.has_subcommands());
        static_assert(command_spec{}.get_arguments().empty());
        static_assert(command_spec{}.get_display_order() == 999);
        static_assert(command_spec{}.get_color() == color_choice::auto_);
        static_assert(command_spec{}.find_subcommand("anything") == nullptr);
    } // namespace detail
} // namespace clapp
