/**
 * \file
 * \brief clapp::parse() — the command-line parse loop and its private machinery.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/context.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/parsed_arg.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/lex/short_flags.hpp>
#include <clapp/output/help.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/parser/arg_matcher.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/parser/validator.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/any_value.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {
    namespace detail {
        /** \brief Explicitly consume a nodiscard result on an error path. */
        template<class T>
        constexpr void discard_result([[maybe_unused]] T &&result) noexcept {
        }

        /** \brief Keep independent compile-time contract clauses readable to the IDE. */
        template<class... Checks>
        [[nodiscard]] constexpr bool all_true(Checks... checks) noexcept {
            return (... && checks);
        }

        // ===================================================================
        // The two little state machines clap keeps on the stack
        // ===================================================================

        /**
         * \brief How this command level tells the user to ask for help.
         *        clap's `get_help_flag`.
         *
         * Closing `For more information, try '…'.` line; not always `--help`.
         * \param cmd Command level whose error is being rendered.
         * \return How to ask, or nullopt when no help is offered.
         */
        [[nodiscard]] inline std::optional<cow_str> help_flag_of(const command_spec &cmd) {
            if (!cmd.is_disable_help_flag_set()) return cow_str::borrowed("--help");

            // clap's `get_user_help_flag`: the first argument whose action is one of the
            // three help actions, spelled long-first.
            for (const arg_spec &one: cmd.get_arguments()) {
                const arg_action act = one.get_action();
                if (act != arg_action::help && act != arg_action::help_short &&
                    act != arg_action::help_long)
                    continue;
                if (const std::optional<std::string_view> long_ = one.get_long();
                    long_.has_value()) {
                    std::string spelled{"--"};
                    spelled.append(*long_);
                    return cow_str::owned(std::move(spelled));
                }
                if (const std::optional<char> short_ = one.get_short(); short_.has_value()) {
                    std::string spelled{"-"};
                    spelled.push_back(*short_);
                    return cow_str::owned(std::move(spelled));
                }
            }

            if (cmd.has_subcommands() && !cmd.is_disable_help_subcommand_set())
                return cow_str::borrowed("help");
            return std::nullopt;
        }

        /**
         * \brief Sentence a failed value_parser contributes to its error.
         *
         * Ladder: (1) parse_error::reason if non-empty; (2) `"{shown} is not in
         * {domain}"` for range failures; (3) describe(kind). Rung 2 is the only
         * reader of parse_error::domain.
         *
         * \param shown Value as the diagnostic spells it (to_string_lossy of failed bytes).
         * \param why   What the parser said.
         * \return Assembled sentence.
         */
        [[nodiscard]] constexpr std::string value_reason(std::string_view shown,
                                                         const parse_error &why) {
            std::string sentence;
            if (!why.reason.empty()) {
                append_bytes(sentence, why.reason);
                return sentence;
            }
            if (why.kind == parse_error_kind::out_of_range && !why.domain.empty()) {
                append_bytes(sentence, shown);
                append_bytes(sentence, " is not in ");
                append_bytes(sentence, why.domain);
                return sentence;
            }
            append_bytes(sentence, why.message());
            return sentence;
        }

        /** \brief What the previous token left open. clap's `ParseState`. */
        enum class parse_state_kind : unsigned char {
            values_done, /**< Nothing is in flight; the next token starts fresh. */
            opt, /**< A named option is collecting values. */
            pos, /**< A multi-valued positional is collecting values. */
        };

        /**
         * \brief clap's `ParseState`: kind plus the argument it names.
         *
         * Only `opt` makes the *next* token a value; `pos` re-examines tokens as flags first.
         */
        struct parse_state {
            parse_state_kind kind = parse_state_kind::values_done; /**< Which state. */
            arg_id id{}; /**< The argument in flight; empty for `values_done`. */

            /** \brief The neutral state. */
            [[nodiscard]] static constexpr parse_state done() noexcept { return {}; }

            /** \brief "\p which is a named option collecting values." */
            [[nodiscard]] static constexpr parse_state option(arg_id which) noexcept {
                return {.kind = parse_state_kind::opt, .id = which};
            }

            /** \brief "\p which is a positional collecting values." */
            [[nodiscard]] static constexpr parse_state positional(arg_id which) noexcept {
                return {.kind = parse_state_kind::pos, .id = which};
            }

            /**
             * \brief Whether some argument is named by this state.
             * \note Test that decides whether a token is considered as a subcommand name.
             */
            [[nodiscard]] constexpr bool holds_arg() const noexcept {
                return kind != parse_state_kind::values_done;
            }

            /** \brief Member-wise equality. */
            [[nodiscard]] constexpr bool operator==(const parse_state &other) const noexcept {
                return kind == other.kind && id.name() == other.id.name();
            }
        };

        /** \brief The recoverable outcome of examining one token. clap's `ParseResult`. */
        enum class parse_result_kind : unsigned char {
            values_done, /**< The token was consumed; nothing is open. */
            opt, /**< An option was opened and wants values. */
            flag_subcommand, /**< The token selected a `--flag` subcommand. */
            attached_value_not_consumed, /**< `-cu`: `u` is not `c`'s value; keep going. */
            unneeded_attached_value, /**< `--flag=x` where `--flag` takes no value. */
            maybe_hyphen_value, /**< Looks like a flag but may be data. */
            equals_not_provided, /**< `require_equals` without the `=`. */
            no_matching_arg, /**< Nothing in the command answers to it. */
            no_arg, /**< A bare `-`; try the positionals. */
        };

        /**
         * \brief clap's `ParseResult`, flattened into one struct.
         *
         * Loop switches on kind and reads at most two fields (no variant needed).
         */
        struct parse_result {
            parse_result_kind kind = parse_result_kind::values_done; /**< Which outcome. */
            arg_id id{}; /**< opt: the argument opened. */
            std::string text{}; /**< Subcommand name, or argument to quote. */
            std::string extra{}; /**< unneeded_attached_value: the unwanted value. */
            /** unneeded_attached_value: ids forced into smart Usage. */
            std::vector<arg_id> used{};

            /** \brief "Consumed; nothing left open." */
            [[nodiscard]] static constexpr parse_result done() { return {}; }

            /** \brief "A bare `-`; fall through to the positionals." */
            [[nodiscard]] static constexpr parse_result none() {
                return {.kind = parse_result_kind::no_arg};
            }

            /** \brief "This may be data rather than a flag; fall through." */
            [[nodiscard]] static constexpr parse_result hyphen_value() {
                return {.kind = parse_result_kind::maybe_hyphen_value};
            }

            /** \brief "The attached value belongs to a later short flag." */
            [[nodiscard]] static constexpr parse_result attached_not_consumed() {
                return {.kind = parse_result_kind::attached_value_not_consumed};
            }

            /** \brief "\p which is now collecting values." */
            [[nodiscard]] static constexpr parse_result option(arg_id which) {
                return {.kind = parse_result_kind::opt, .id = which};
            }

            /** \brief "The token selected the subcommand \p name." */
            [[nodiscard]] static constexpr parse_result subcommand(std::string name) {
                return {.kind = parse_result_kind::flag_subcommand, .text = std::move(name)};
            }

            /** \brief "\p arg requires `=` and did not get one." */
            [[nodiscard]] static constexpr parse_result needs_equals(std::string arg) {
                return {.kind = parse_result_kind::equals_not_provided, .text = std::move(arg)};
            }

            /** \brief "Nothing answers to \p arg." */
            [[nodiscard]] static constexpr parse_result unmatched(std::string arg) {
                return {.kind = parse_result_kind::no_matching_arg, .text = std::move(arg)};
            }

            /**
             * \brief "\p arg takes no value but was handed \p rest."
             * \param arg  Argument, already rendered for quoting.
             * \param rest Value it did not want.
             * \param used Ids for smart Usage — see #used.
             */
            [[nodiscard]] static constexpr parse_result
            unneeded_value(std::string arg, std::string rest, std::vector<arg_id> used) {
                return {
                    .kind = parse_result_kind::unneeded_attached_value,
                    .text = std::move(arg),
                    .extra = std::move(rest),
                    .used = std::move(used)
                };
            }
        };

        /**
         * \brief "no `--` boundary was recorded", as a `std::size_t`.
         *
         * pending_arg stores optional; engine uses a sentinel because GCC IPA-SRA at
         * `-O3` false-positives on by-value `optional<size_t>` (same as any_value::take).
         */
        inline constexpr std::size_t no_trailing_values = static_cast<std::size_t>(-1);

        /**
         * \brief What the token loop should do next. Not a clap type.
         *
         * Named because a C++ switch-in-while cannot spell `break` out of the match cleanly.
         */
        enum class token_flow : unsigned char {
            fall_through, /**< Keep examining this token further down the loop body. */
            next_token, /**< Done with this token. */
            stop_loop, /**< Leave the loop; a subcommand takes over. */
        };

        // ===================================================================
        // Pure lookups over a frozen command tree
        // ===================================================================

        /**
         * \brief Positional argument occupying slot \p index, or `nullptr`.
         *
         * Slots are 1-based and dense (freeze()). \p index walking upward visits each once.
         * \param cmd   Command being parsed.
         * \param index 1-based positional slot.
         * \return Pointer into `cmd`'s args, or nullptr past the last positional.
         *
         * \warning **Never compare the result to `nullptr` inside a constant
         *          expression.** GCC under `-fsanitize=null` refuses to fold cross-object
         *          pointer comparisons; `== nullptr` on absent quietly folds while the
         *          present half breaks ubsan. Ask has_positional_at() instead. Trap 10.
         */
        [[nodiscard]] constexpr const arg_spec *positional_at(const command_spec &cmd,
                                                              std::size_t index) noexcept {
            for (const arg_spec &candidate: cmd.get_arguments()) {
                if (candidate.get_index() == index) return &candidate;
            }
            return nullptr;
        }

        /**
         * \brief Whether any positional occupies slot \p index.
         * \param cmd   Command being parsed.
         * \param index 1-based positional slot.
         * \return `true` when positional_at() would return a record.
         * \note `static_assert`-safe half of positional_at() (never forms the pointer).
         */
        [[nodiscard]] constexpr bool has_positional_at(const command_spec &cmd,
                                                       std::size_t index) noexcept {
            return std::ranges::any_of(cmd.get_arguments(), [index](const arg_spec &candidate) {
                return candidate.get_index() == index;
            });
        }

        /**
         * \brief Argument answering to `--`\p name, or `nullptr` (aliases via matches_long).
         * \param cmd  Command being parsed.
         * \param name Long spelling without leading dashes.
         * \warning Never compare to `nullptr` in a constant expression; ask has_long_arg().
         */
        [[nodiscard]] constexpr const arg_spec *long_arg_at(const command_spec &cmd,
                                                            std::string_view name) noexcept {
            for (const arg_spec &candidate: cmd.get_arguments()) {
                if (candidate.matches_long(name)) return &candidate;
            }
            return nullptr;
        }

        /**
         * \brief Whether any argument answers to long spelling \p name (aliases count).
         * \param cmd  Command being parsed.
         * \param name Spelling without dashes.
         * \return `true` when long_arg_at() would return a record.
         */
        [[nodiscard]] constexpr bool has_long_arg(const command_spec &cmd,
                                                  std::string_view name) noexcept {
            return std::ranges::any_of(cmd.get_arguments(), [name](const arg_spec &candidate) {
                return candidate.matches_long(name);
            });
        }

        /**
         * \brief Argument answering to `-`\p letter, or `nullptr`.
         * \param cmd    Command being parsed.
         * \param letter Short spelling without leading dash.
         * \warning Never compare to `nullptr` in a constant expression; ask has_short_arg().
         */
        [[nodiscard]] constexpr const arg_spec *short_arg_at(const command_spec &cmd,
                                                             char letter) noexcept {
            for (const arg_spec &candidate: cmd.get_arguments()) {
                if (candidate.matches_short(letter)) return &candidate;
            }
            return nullptr;
        }

        /**
         * \brief Whether any argument answers to short spelling \p letter.
         * \param cmd    Command being parsed.
         * \param letter Letter without dash. Short aliases count.
         * \return `true` when short_arg_at() would return a record.
         */
        [[nodiscard]] constexpr bool has_short_arg(const command_spec &cmd, char letter) noexcept {
            return std::ranges::any_of(cmd.get_arguments(), [letter](const arg_spec &candidate) {
                return candidate.matches_short(letter);
            });
        }

        /** \brief How many positional slots the command has. */
        [[nodiscard]] constexpr std::size_t positional_count(const command_spec &cmd) noexcept {
            std::size_t total = 0;
            for (const arg_spec &candidate: cmd.get_arguments()) {
                if (candidate.is_positional()) ++total;
            }
            return total;
        }

        /** \brief Whether any argument declared `last(true)`. clap's `contains_last`. */
        [[nodiscard]] constexpr bool has_last_positional(const command_spec &cmd) noexcept {
            return std::ranges::any_of(cmd.get_arguments(),
                                       [](const arg_spec &a) { return a.is_last_set(); });
        }

        /** \brief The highest-indexed positional, or `nullptr` when there is none. */
        [[nodiscard]] constexpr const arg_spec *last_positional(const command_spec &cmd) noexcept {
            const arg_spec *found = nullptr;
            for (const arg_spec &candidate: cmd.get_arguments()) {
                if (candidate.is_positional()) found = &candidate;
            }
            return found;
        }

        /**
         * \brief Whether \p next starts a new arg vs continuing \p current_positional.
         *        clap's `Parser::is_new_arg`.
         *
         * \param next               Token about to be read.
         * \param current_positional Positional that would otherwise take it.
         * \return `true` when \p next must be treated as a flag.
         *
         * \note Test order is behaviour: `allow_hyphen_values` wins over everything;
         *       `allow_negative_numbers` only for number-shaped tokens. Bare `-` is a
         *       value (Unix stdin convention).
         */
        [[nodiscard]] constexpr bool is_new_arg(const parsed_arg &next,
                                                const arg_spec &current_positional) noexcept {
            if (current_positional.is_allow_hyphen_values_set()) return false;
            if (current_positional.is_allow_negative_numbers_set() && next.is_negative_number())
                return false;
            if (next.is_long()) return true;
            if (next.is_short()) return true;
            return false;
        }

        /**
         * \brief Whether `--help` / `--version` should render the long form.
         * \param ident How the argument was spelled, if at all.
         * \return `false` only for arg_identifier::short_.
         * \note Only explicit `-h` asks for short; positional/env/default ask for long.
         */
        [[nodiscard]] constexpr bool
        prefers_long_form(std::optional<arg_identifier> ident) noexcept {
            return ident != std::optional<arg_identifier>{arg_identifier::short_};
        }

        /**
         * \brief Whether \p value is \p arg's `value_terminator`. clap's `check_terminator`.
         * \param arg   Argument now collecting values.
         * \param value Token about to be read.
         * \return `true` when the token closes the argument instead of feeding it.
         */
        [[nodiscard]] constexpr bool check_terminator(const arg_spec &arg, os_str value) noexcept {
            const std::optional<std::string_view> terminator = arg.get_value_terminator();
            return terminator.has_value() && value == os_str{*terminator};
        }

        // ===================================================================
        // Rendering the fragments diagnostics quote
        // ===================================================================

        /**
         * \brief Append the UTF-8 encoding of \p code_point to \p out.
         * \param out        String to grow.
         * \param code_point Scalar; surrogates/out-of-range encoded as written (quoting).
         * \note `push_back` only — trap 10: ubsan will not fold `_M_mutate`/`append`.
         */
        constexpr void append_utf8(std::string &out, char32_t code_point) {
            const char32_t scalar = code_point;
            if (scalar < 0x80U) {
                out.push_back(static_cast<char>(scalar));
            } else if (scalar < 0x800U) {
                out.push_back(static_cast<char>(0xC0U + scalar / 0x40U));
                out.push_back(static_cast<char>(0x80U + scalar % 0x40U));
            } else if (scalar < 0x10000U) {
                out.push_back(static_cast<char>(0xE0U + scalar / 0x1000U));
                out.push_back(static_cast<char>(0x80U + scalar / 0x40U % 0x40U));
                out.push_back(static_cast<char>(0x80U + scalar % 0x40U));
            } else {
                out.push_back(static_cast<char>(0xF0U + scalar / 0x40000U));
                out.push_back(static_cast<char>(0x80U + scalar / 0x1000U % 0x40U));
                out.push_back(static_cast<char>(0x80U + scalar / 0x40U % 0x40U));
                out.push_back(static_cast<char>(0x80U + scalar % 0x40U));
            }
        }

        /**
         * \brief How an argument is spelled in a diagnostic. clap's plain `Display for Arg`.
         *
         * \param arg Argument to name.
         * \return Freshly built string; nothing borrows.
         * \note Named args show **long** when present, short otherwise (not both).
         */
        [[nodiscard]] constexpr std::string arg_display(const arg_spec &arg) {
            std::string out;
            if (const std::optional<std::string_view> long_name = arg.get_long();
                long_name.has_value()) {
                append_bytes(out, "--");
                append_bytes(out, *long_name);
            } else if (const std::optional<char> short_name = arg.get_short();
                short_name.has_value()) {
                out.push_back('-');
                out.push_back(*short_name);
            }

            bool need_closing_bracket = false;
            if (arg.is_takes_value_set() && !arg.is_positional()) {
                const bool optional_val = arg.get_min_vals() == 0;
                if (arg.is_require_equals_set()) {
                    if (optional_val) {
                        need_closing_bracket = true;
                        append_bytes(out, "[=");
                    } else {
                        out.push_back('=');
                    }
                } else if (optional_val) {
                    need_closing_bracket = true;
                    append_bytes(out, " [");
                } else {
                    out.push_back(' ');
                }
            }
            if (arg.is_takes_value_set() || arg.is_positional()) {
                append_bytes(out, render_arg_values(arg, arg.is_required_set()));
            } else if (arg.get_action() == arg_action::count) {
                append_bytes(out, "...");
            }
            if (need_closing_bracket) out.push_back(']');
            return out;
        }

        /**
         * \brief Values \p arg would have accepted, for an `invalid_value` error.
         * \param arg Argument whose parser enumerates its domain.
         * \return Visible possible_value names, or empty.
         */
        [[nodiscard]] inline std::vector<cow_str> accepted_values(const arg_spec &arg) {
            std::vector<cow_str> names = arg.get_possible_values() |
                                         std::views::filter([](const possible_value &one) {
                                             return !one.is_hide_set();
                                         }) |
                                         std::views::transform([](const possible_value &one) {
                                             return cow_str::owned(one.get_name());
                                         }) |
                                         std::ranges::to<std::vector>();
            return names;
        }

        /**
         * \brief clap's `Command::_render_version`, as styled_str.
         * \param cmd      Command whose version is wanted.
         * \param use_long `--version` vs `-V`; each falls back to the other when unset.
         * \note Forwarder to clapp::render_version().
         */
        [[nodiscard]] inline styled_str render_version_text(const command_spec &cmd,
                                                            bool use_long) {
            return clapp::render_version(cmd, use_long);
        }

        /**
         * \brief Text a `--help` error carries.
         *
         * \param cmd        Command to describe.
         * \param use_long   `--help` (long) vs `-h` (short).
         * \param usage_name What `Usage:` calls \p cmd (full path); empty asks \p cmd.
         * \return Full help screen via render_help_for_terminal().
         *
         * \warning **`use_long` is collapsed against long_help_exists() here.** clap's
         *          `write_help_err` does the same; without it, `--help` on a command with
         *          no long form prints every description on its own line.
         */
        [[nodiscard]] inline styled_str
        render_help_text(const command_spec &cmd, bool use_long, std::string_view usage_name = {}) {
            return clapp::render_help_for_terminal(
                cmd,
                help_style{
                    .use_long = use_long && clapp::long_help_exists(cmd),
                    .usage_name = usage_name
                });
        }

        /**
         * \brief Applet name a `multicall` command starts from. Rust's `Path::file_stem`.
         * \param path `argv[0]` as the OS handed it over.
         * \return Last path component with final extension removed, or empty.
         * \note Honours both `/` and `\\`.
         */
        [[nodiscard]] constexpr std::string_view file_stem(std::string_view path) noexcept {
            std::size_t start = 0;
            for (std::size_t i = 0; i < path.size(); ++i) {
                if (path[i] == '/' || path[i] == '\\') start = i + 1;
            }
            path.remove_prefix(start);
            for (std::size_t i = path.size(); i > 1; --i) {
                if (path[i - 1] == '.') {
                    path.remove_suffix(path.size() - i + 1);
                    return path;
                }
            }
            return path;
        }

        /**
         * \brief Plain path to a child command. clap's `Command::bin_name` for a sub.
         * \param parent_bin_path Parent path, e.g. `test` or `test sub`.
         * \param sub             The child.
         * \return `<parent_bin_path> <sub name>`, or just the child name if parent empty.
         * \note Plain (no flag spellings / requirement fragments) — those are usage_name.
         */
        [[nodiscard]] inline std::string child_bin_path(std::string_view parent_bin_path,
                                                        const command_spec &sub) {
            std::string out;
            append_bytes(out, parent_bin_path);
            if (!out.empty()) out.push_back(' ');
            append_bytes(out, sub.get_name());
            return out;
        }

        /**
         * \brief What a `Usage:` line under \p sub calls it. clap's `usage_name`.
         * \param parent          Command \p sub was found under.
         * \param parent_bin_path Parent's plain path.
         * \param sub             The child.
         * \return clap's `<parent bin_name><mid_string><sc_names>`.
         * \see usage_renderer::subcommand_usage_name()
         */
        [[nodiscard]] inline std::string child_usage_name(const command_spec &parent,
                                                          std::string_view parent_bin_path,
                                                          const command_spec &sub) {
            return usage_renderer{parent, required_graph(parent)}.subcommand_usage_name(
                sub, parent_bin_path);
        }

        /**
         * \brief Prefix a child of \p parent inherits. clap's `self_bin_name`.
         *
         * \param parent          Command the child was found under.
         * \param parent_bin_path What the loop calls \p parent.
         * \return \p parent_bin_path, except under `multicall` (explicit bin_name or empty).
         *
         * \warning **Only what a *child* inherits is emptied.** The multicall level's
         *          own messages keep bin_path; emptying both yields bare `Usage: <COMMAND>`.
         */
        [[nodiscard]] inline std::string_view child_base_path(const command_spec &parent,
                                                              std::string_view parent_bin_path) {
            if (!parent.is_multicall_set()) return parent_bin_path;
            return parent.get_bin_name().value_or(std::string_view{});
        }

        // ===================================================================
        // The engine
        // ===================================================================

        /**
         * \brief One command level's parse loop. clap's `Parser`.
         *
         * Constructed per command (root and each running subcommand). Not public;
         * clapp::parse() is.
         */
        class parse_engine {
        public:
            /**
             * \brief Engine for \p cmd (root: seeds paths from \p cmd itself).
             * \param cmd Command level; must outlive the engine.
             */
            explicit parse_engine(const command_spec &cmd)
                : cmd_(&cmd),
                  bin_path_(std::string{cmd.get_bin_name().value_or(cmd.get_name())}),
                  usage_name_(bin_path_) {
            }

            /**
             * \brief Engine for a subcommand, told what to call it.
             * \param cmd        Child command level; must outlive the engine.
             * \param bin_path   Full space-joined path (clap's `bin_name`).
             * \param usage_name What `Usage:` calls \p cmd (clap's `usage_name`).
             * \note Frozen tree cannot stamp paths (ADR-0005); rebuilt on each descent.
             */
            parse_engine(const command_spec &cmd, std::string bin_path, std::string usage_name)
                : cmd_(&cmd), bin_path_(std::move(bin_path)), usage_name_(std::move(usage_name)) {
            }

            // ---------------------------------------------------------------
            // The whole of one command level
            // ---------------------------------------------------------------

            /**
             * \brief Parse, resolve, then apply env and defaults. clap's `get_matches_with`.
             *
             * \param matcher Accumulator to fill.
             * \param raw     Token stream.
             * \param cursor  Where to start.
             * \return Nothing, or the first error.
             *
             * \note On `ignore_errors` failure, env and defaults still apply first.
             * \note Validator runs **after** env/defaults — load-bearing: defaulted
             *       `required` must count as provided. Each level validates itself.
             */
            std::expected<void, error>
            get_matches_with(arg_matcher &matcher, const raw_args &raw, arg_cursor cursor) {
                std::expected<void, error> outcome = run_and_validate(matcher, raw, cursor);
                // clap's `Error::format(cmd)`, which every error passes through on its
                // way out of the level that raised it. The innermost level wins: a
                // subcommand's error already carries its own flag by the time the parent
                // sees it, and overwriting it would point the user at the wrong `--help`.
                //
                // The command asked is normally this one, because it is this one whose
                // `Usage:` line the error carries. help_subcommand_error() is the single
                // exception — it reports on a level it walked down to without entering,
                // so it leaves that level in #error_help_level_ and it is answered here.
                // Asking the wrong command is silent whenever the two agree, which is
                // every case except the one that matters; see help_subcommand_error().
                if (!outcome.has_value() && !outcome.error().has_help_flag()) {
                    const command_spec &about =
                            error_help_level_.has_value() ? *error_help_level_.value() : *cmd_;
                    if (std::optional<cow_str> flag = help_flag_of(about); flag.has_value())
                        outcome.error().set_help_flag(std::move(*flag));
                }
                return outcome;
            }

        private:
            /** \brief get_matches_with() without the error decoration. */
            std::expected<void, error>
            run_and_validate(arg_matcher &matcher, const raw_args &raw, arg_cursor cursor) {
                std::expected<void, error> parsed = run(matcher, raw, cursor);
                if (!parsed.has_value()) {
                    if (cmd_->is_ignore_errors_set()) {
                        discard_result(add_env(matcher));
                        discard_result(add_defaults(matcher));
                    }
                    return std::unexpected(std::move(parsed.error()));
                }
                if (std::expected<void, error> resolved = resolve_pending(matcher);
                    !resolved.has_value())
                    return resolved;
                if (std::expected<void, error> from_env = add_env(matcher); !from_env.has_value())
                    return from_env;
                if (std::expected<void, error> defaulted = add_defaults(matcher);
                    !defaulted.has_value())
                    return defaulted;
                return validate(*cmd_, matcher, usage_name_, bin_path_);
            }

        public:
            /**
             * \brief Token loop proper. clap's `Parser::parse`.
             * \param matcher Accumulator to fill.
             * \param raw     Token stream.
             * \param cursor  Start position; advanced as tokens are consumed.
             * \return Nothing, or the first error.
             */
            std::expected<void, error>
            run(arg_matcher &matcher, const raw_args &raw, arg_cursor cursor) {
                std::optional<std::string> subcmd_name;
                bool keep_state = false;
                parse_state state = parse_state::done();
                std::size_t pos_counter = 1;
                bool valid_arg_found = false;
                bool trailing_values = false;

                const std::size_t slots = positional_count(*cmd_);
                const bool contains_last = has_last_positional(*cmd_);

                // A raw `while`, deliberately: this is a state machine, not a
                // transformation. Each iteration may consume a variable number of further
                // tokens, may rewind, and mutates five locals; a ranges pipeline can
                // express none of that.
                while (std::optional<parsed_arg> token = raw.next(cursor)) {
                    token_flow flow = token_flow::fall_through;

                    if (!trailing_values) {
                        // -- does the token name a subcommand? ----------------
                        if (cmd_->is_subcommand_precedence_over_arg_set() || !state.holds_arg()) {
                            const std::optional<std::string_view> sc =
                                    possible_subcommand(token->to_value(), valid_arg_found);
                            if (sc.has_value()) {
                                if (*sc == "help" && !cmd_->is_disable_help_subcommand_set())
                                    return std::unexpected(help_subcommand_error(raw, cursor));
                                subcmd_name = std::string{*sc};
                                break;
                            }
                        }

                        // -- `--`, `--long`, `-short` -------------------------
                        if (token->is_escape()) {
                            if (!state_allows_hyphen_values(state)) {
                                const arg_spec *here = positional_at(*cmd_, pos_counter);
                                if (here != nullptr &&
                                    check_terminator(*here, token->to_value_os()))
                                    ++pos_counter;
                                trailing_values = true;
                                matcher.start_trailing();
                                continue;
                            }
                        } else if (const std::optional<parsed_arg::long_flag> as_long =
                                    token->to_long();
                            as_long.has_value()) {
                            std::expected<parse_result, error> outcome =
                                    parse_long_arg(matcher,
                                                   as_long->first,
                                                   as_long->second,
                                                   state,
                                                   pos_counter,
                                                   valid_arg_found);
                            if (!outcome.has_value())
                                return std::unexpected(std::move(outcome.error()));
                            std::expected<token_flow, error> next =
                                    apply_long_result(matcher,
                                                      raw,
                                                      cursor,
                                                      *outcome,
                                                      state,
                                                      subcmd_name,
                                                      trailing_values);
                            if (!next.has_value()) return std::unexpected(std::move(next.error()));
                            flow = *next;
                        } else if (std::optional<short_flags> as_short = token->to_short();
                            as_short.has_value()) {
                            std::expected<parse_result, error> outcome = parse_short_arg(
                                matcher, *as_short, state, pos_counter, valid_arg_found);
                            if (!outcome.has_value())
                                return std::unexpected(std::move(outcome.error()));
                            std::expected<token_flow, error> next =
                                    apply_short_result(matcher,
                                                       raw,
                                                       cursor,
                                                       *outcome,
                                                       state,
                                                       subcmd_name,
                                                       keep_state,
                                                       trailing_values);
                            if (!next.has_value()) return std::unexpected(std::move(next.error()));
                            flow = *next;
                        }

                        if (flow == token_flow::next_token) continue;
                        if (flow == token_flow::stop_loop) break;

                        // -- another value for the option in flight -----------
                        if (state.kind == parse_state_kind::opt) {
                            const arg_spec *open = cmd_->find_arg(state.id.name());
                            if (open != nullptr) {
                                if (check_terminator(*open, token->to_value_os())) {
                                    state = parse_state::done();
                                } else {
                                    matcher.push_pending_value(state.id,
                                                               os_string{token->to_value_os()},
                                                               std::nullopt,
                                                               false);
                                    state = matcher.needs_more_vals(*open)
                                                ? parse_state::option(open->get_id())
                                                : parse_state::done();
                                }
                                continue;
                            }
                        }
                    }

                    // -- which positional slot is this? -----------------------
                    pos_counter = correct_positional_counter(raw,
                                                             cursor,
                                                             pos_counter,
                                                             slots,
                                                             contains_last,
                                                             trailing_values,
                                                             valid_arg_found);

                    if (const arg_spec *here = positional_at(*cmd_, pos_counter); here != nullptr) {
                        if (here->is_last_set() && !trailing_values) {
                            discard_result(resolve_pending(matcher));
                            // Already a positional; suggesting `--` would be nonsense.
                            return std::unexpected(
                                error::unknown_argument(cow_str{token->display()},
                                                        std::nullopt,
                                                        std::nullopt,
                                                        false,
                                                        usage()));
                        }
                        if (here->is_trailing_var_arg_set()) trailing_values = true;
                        if (!matcher.pending_is(here->get_id().name()) ||
                            !here->is_multiple_values_set()) {
                            if (std::expected<void, error> resolved = resolve_pending(matcher);
                                !resolved.has_value())
                                return resolved;
                        }
                        if (check_terminator(*here, token->to_value_os())) {
                            ++pos_counter;
                            state = parse_state::done();
                        } else {
                            matcher.push_pending_value(here->get_id(),
                                                       os_string{token->to_value_os()},
                                                       arg_identifier::index,
                                                       trailing_values);
                            if (!here->is_multiple()) {
                                ++pos_counter;
                                state = parse_state::done();
                            } else {
                                state = parse_state::positional(here->get_id());
                            }
                        }
                        valid_arg_found = true;
                    } else if (cmd_->is_allow_external_subcommands_set()) {
                        return capture_external_subcommand(matcher, raw, cursor, *token);
                    } else {
                        discard_result(resolve_pending(matcher));
                        return std::unexpected(
                            match_arg_error(*token, valid_arg_found, trailing_values, matcher));
                    }
                }

                if (subcmd_name.has_value()) {
                    if (cmd_->is_args_conflicts_with_subcommands_set() && valid_arg_found)
                        return std::unexpected(error::subcommand_conflict(
                            cow_str{*subcmd_name}, used_arg_names(matcher), usage()));
                    const command_spec *sub = cmd_->find_subcommand(*subcmd_name);
                    if (sub != nullptr)
                        return parse_subcommand(*sub, matcher, raw, cursor, keep_state);
                }
                return {};
            }

        private:
            // ---------------------------------------------------------------
            // Small queries the loop leans on
            // ---------------------------------------------------------------

            /**
             * \brief Whether the argument named by \p state accepts hyphenated values.
             *
             * clap's `matches!(state, Opt(o) | Pos(o) if cmd[o].is_allow_hyphen_values_set())`,
             * which is what stops `--` and `--anything` from being read as flags while
             * such an argument is collecting.
             */
            [[nodiscard]] bool state_allows_hyphen_values(const parse_state &state) const noexcept {
                if (!state.holds_arg()) return false;
                const arg_spec *open = cmd_->find_arg(state.id.name());
                return open != nullptr && open->is_allow_hyphen_values_set();
            }

            /**
             * \brief state_allows_hyphen_values(), plus the negative-number escape.
             *
             * \param state    The current state.
             * \param negative Whether the token reads as a negative number.
             *
             * \note Only the short-flag path consults this: `--3` is not a number, but
             *       `-3` is, and an argument declared `allow_negative_numbers` must get
             *       it rather than have `-3` parsed as the flag `-3`.
             */
            [[nodiscard]] bool state_allows_hyphen_values(const parse_state &state,
                                                          bool negative) const noexcept {
                if (!state.holds_arg()) return false;
                const arg_spec *open = cmd_->find_arg(state.id.name());
                if (open == nullptr) return false;
                return open->is_allow_hyphen_values_set() ||
                       (open->is_allow_negative_numbers_set() && negative);
            }

            /**
             * \brief clap's `matcher.arg_ids().filter_map(|id| cmd.find(id))`, rendered.
             * \param matcher The accumulator to read.
             * \return One clapp::detail::arg_display() per recorded id that names a real
             *         argument; group ids are skipped, exactly as clap skips them.
             */
            [[nodiscard]] std::vector<cow_str> used_arg_names(const arg_matcher &matcher) const {
                std::vector<cow_str> used;
                for (const arg_id &id: matcher.arg_ids()) {
                    const arg_spec *found = cmd_->find_arg(id.name());
                    if (found != nullptr) used.push_back(cow_str{arg_display(*found)});
                }
                return used;
            }

            // ---------------------------------------------------------------
            // Subcommand recognition
            // ---------------------------------------------------------------

            /**
             * \brief Whether \p value names a subcommand. clap's `possible_subcommand`.
             * \param value           Token, or non-UTF-8 bytes.
             * \param valid_arg_found Whether an argument of this command already matched.
             * \return Subcommand name **or the alias that matched**.
             * \note infer_subcommands: unique by *matches*, not subcommands.
             * \note Exact match runs after inference and always (so `te` beats ambiguous prefix).
             */
            [[nodiscard]] std::optional<std::string_view>
            possible_subcommand(const std::expected<std::string_view, os_str> &value,
                                bool valid_arg_found) const {
                if (!value.has_value()) return std::nullopt;
                const std::string_view text = *value;

                if (cmd_->is_args_conflicts_with_subcommands_set() && valid_arg_found)
                    return std::nullopt;

                if (cmd_->is_infer_subcommands_set()) {
                    std::optional<std::string_view> only;
                    bool ambiguous = false;
                    for (const command_spec &sub: cmd_->get_subcommands()) {
                        std::optional<std::string_view> hit;
                        if (sub.get_name().starts_with(text)) {
                            hit = sub.get_name();
                        } else {
                            for (const alias_spec &alias: sub.get_all_aliases()) {
                                if (alias.name.name().starts_with(text)) {
                                    hit = alias.name.name();
                                    break;
                                }
                            }
                        }
                        if (!hit.has_value()) continue;
                        if (only.has_value()) {
                            ambiguous = true;
                            break;
                        }
                        only = hit;
                    }
                    if (only.has_value() && !ambiguous) return only;
                }

                const command_spec *exact = cmd_->find_subcommand(text);
                if (exact != nullptr) return exact->get_name();
                return std::nullopt;
            }

            /**
             * \brief Whether `--`\p text selects a subcommand.
             *        clap's `possible_long_flag_subcommand`.
             * \param text The long spelling, without its dashes.
             */
            [[nodiscard]] std::optional<std::string_view>
            possible_long_flag_subcommand(std::string_view text) const {
                if (cmd_->is_infer_subcommands_set()) {
                    std::optional<std::string_view> only;
                    bool ambiguous = false;
                    for (const command_spec &sub: cmd_->get_subcommands()) {
                        bool hit = false;
                        const std::optional<std::string_view> flag = sub.get_long_flag();
                        if (flag.has_value() && flag->starts_with(text)) {
                            hit = true;
                        } else {
                            for (const alias_spec &alias: sub.get_all_long_flag_aliases()) {
                                if (alias.name.name().starts_with(text)) {
                                    hit = true;
                                    break;
                                }
                            }
                        }
                        if (!hit) continue;
                        if (only.has_value()) {
                            ambiguous = true;
                            break;
                        }
                        only = sub.get_name();
                    }
                    if (only.has_value() && !ambiguous) return only;
                }
                const command_spec *exact = cmd_->find_long_subcommand(text);
                if (exact != nullptr) return exact->get_name();
                return std::nullopt;
            }

            /**
             * \brief Walk `help sub sub…` and produce that level's help error.
             *        clap's `parse_help_subcommand`.
             * \param raw    Token stream.
             * \param cursor Positioned just after the `help` token.
             *
             * \note **Not `const`:** records the deepest recognized level in
             *       #error_help_level_ so get_matches_with() picks the right help footer.
             *       Wrong level can advertise a `--help` the user cannot type there.
             */
            [[nodiscard]] error help_subcommand_error(const raw_args &raw, arg_cursor cursor) {
                const command_spec *level = cmd_;
                std::string bin_path = bin_path_;
                std::string usage_name = usage_name_;
                for (const os_str name: raw.remaining(cursor)) {
                    const command_spec *next = level->find_subcommand(name.chars());
                    if (next == nullptr) {
                        error_help_level_ = level;
                        return error::unrecognized_subcommand(
                            cow_str::owned(name.to_string_lossy()),
                            usage_renderer{*level, required_graph(*level), usage_name}
                            .create_usage_with_title(std::span<const arg_id>{}));
                    }
                    const std::string_view base = child_base_path(*level, bin_path);
                    usage_name = child_usage_name(*level, base, *next);
                    bin_path = child_bin_path(base, *next);
                    level = next;
                }
                return error::display_help(render_help_text(*level, true, usage_name));
            }

            /**
             * \brief Hand the rest of the tokens to \p sub. clap's `parse_subcommand`.
             * \param sub        Child command.
             * \param matcher    Parent accumulator (receives child result).
             * \param raw        Token stream.
             * \param cursor     Where the child should start.
             * \param keep_state Whether the child continues a half-read short cluster.
             */
            std::expected<void, error> parse_subcommand(const command_spec &sub,
                                                        arg_matcher &matcher,
                                                        const raw_args &raw,
                                                        arg_cursor cursor,
                                                        bool keep_state) const {
                const bool partial = cmd_->is_ignore_errors_set();
                arg_matcher sub_matcher{sub};
                // clap's `_build_subcommand` runs here and stamps the child's bin_name
                // and usage_name. The frozen tree cannot be stamped, so the two paths are
                // computed and handed to the child engine instead — see the engine's
                // three-argument constructor.
                const std::string_view base = child_base_path(*cmd_, bin_path_);
                parse_engine child{
                    sub, child_bin_path(base, sub), child_usage_name(*cmd_, base, sub)
                };
                if (keep_state) {
                    child.cur_idx_ = cur_idx_;
                    child.flag_subcmd_at_ = flag_subcmd_at_;
                    child.flag_subcmd_skip_ = flag_subcmd_skip_;
                }
                std::expected<void, error> outcome =
                        child.get_matches_with(sub_matcher, raw, cursor);
                if (!outcome.has_value()) {
                    if (!(partial && outcome.error().use_stderr()))
                        return std::unexpected(std::move(outcome.error()));
                }
                matcher.set_subcommand(std::string{sub.get_name()},
                                       std::move(sub_matcher).into_inner());
                return {};
            }

            /**
             * \brief Collect everything left as an external subcommand's arguments.
             * \param matcher Accumulator that receives the child result.
             * \param raw     Token stream.
             * \param cursor  Positioned at the subcommand name.
             * \param token   That name, already read.
             *
             * \note Stored under external_id (empty name); child matches mark that id valid.
             * \warning **Default external parser is `std::string` (strict UTF-8), not
             *          clap's `OsString`.** Non-UTF-8 is rejected here; clap accepts it.
             *          Use `external_subcommand_value_parser<os_string>()` for clap's
             *          behaviour.
             */
            std::expected<void, error> capture_external_subcommand(arg_matcher &matcher,
                                                                   const raw_args &raw,
                                                                   arg_cursor cursor,
                                                                   const parsed_arg &token) {
                const std::expected<std::string_view, os_str> name = token.to_value();
                if (!name.has_value()) {
                    discard_result(resolve_pending(matcher));
                    // The one value_validation that DOES carry a usage line. It stands in
                    // for clap's `Error::invalid_utf8` (the documented kind substitution
                    // on clapp::error_kind), and clap's `invalid_utf8` takes a usage where
                    // its `value_validation` does not — so the usage is attached here
                    // rather than inside the named constructor.
                    error utf8{
                        error::value_validation(
                            cow_str::borrowed("<subcommand>"),
                            cow_str::owned(token.to_value_os().to_string_lossy()),
                            cow_str::borrowed("invalid UTF-8 in a subcommand name"))
                    };
                    if (std::optional<styled_str> line = usage(); line.has_value())
                        utf8.insert(context_kind::usage, context_value::styled(std::move(*line)));
                    return std::unexpected(std::move(utf8));
                }

                const parser_vtable *external =
                        cmd_->has_external_subcommand_value_parser()
                            ? cmd_->get_external_subcommand_value_parser()
                            : parser_for<std::string>();

                arg_matcher child;
                std::vector<arg_id> valid_ids;
                valid_ids.reserve(cmd_->get_arguments().size() + cmd_->get_groups().size() + 1U);
                valid_ids.push_back(external_id);
                for (const arg_spec &one: cmd_->get_arguments()) valid_ids.push_back(one.get_id());
                for (const group_spec &one: cmd_->get_groups()) valid_ids.push_back(one.get_id());
                child.matches().set_valid_ids(std::move(valid_ids));
                child.start_occurrence_of_external();

                for (const os_str value: raw.remaining(cursor)) {
                    std::expected<any_value, parse_error> parsed = external->parse(value, false);
                    if (!parsed.has_value()) {
                        discard_result(resolve_pending(matcher));
                        return std::unexpected(external_value_error(value, parsed.error()));
                    }
                    child.add_val_to(external_id, std::move(*parsed), os_string{value});
                }

                matcher.set_subcommand(std::string{*name}, std::move(child).into_inner());
                return {};
            }

            // ---------------------------------------------------------------
            // Long options
            // ---------------------------------------------------------------

            /**
             * \brief Examine one `--long` token. clap's `parse_long_arg`.
             *
             * \param matcher         The accumulator.
             * \param name            The spelling, or the bytes that were not UTF-8.
             * \param attached        The `=value` part, when there was one.
             * \param state           What the previous token left open.
             * \param pos_counter     The positional slot currently in play.
             * \param valid_arg_found Set when the token matched; read by the subcommand
             *                        test on later tokens.
             */
            std::expected<parse_result, error>
            parse_long_arg(arg_matcher &matcher,
                           const std::expected<std::string_view, os_str> &name,
                           const std::optional<os_str> &attached,
                           const parse_state &state,
                           std::size_t pos_counter,
                           bool &valid_arg_found) {
                if (state_allows_hyphen_values(state)) return parse_result::hyphen_value();

                if (!name.has_value())
                    return parse_result::unmatched(name.error().to_string_lossy());
                const std::string_view spelling = *name;

                const arg_spec *found = long_arg_at(*cmd_, spelling);
                if (found == nullptr && cmd_->is_infer_long_args_set())
                    found = infer_long_arg(spelling);

                if (found != nullptr) {
                    valid_arg_found = true;
                    if (found->is_takes_value_set())
                        return parse_opt_value(arg_identifier::long_,
                                               attached,
                                               *found,
                                               matcher,
                                               attached.has_value());
                    if (attached.has_value())
                        return parse_result::unneeded_value(arg_display(*found),
                                                            attached->to_string_lossy(),
                                                            smart_usage_ids(matcher, found));
                    return react(arg_identifier::long_,
                                 clapp::value_source::command_line,
                                 *found,
                                 {},
                                 no_trailing_values,
                                 matcher);
                }

                if (const std::optional<std::string_view> sub =
                            possible_long_flag_subcommand(spelling);
                    sub.has_value())
                    return parse_result::subcommand(std::string{*sub});

                const arg_spec *here = positional_at(*cmd_, pos_counter);
                if (here != nullptr && here->is_allow_hyphen_values_set() && !here->is_last_set())
                    return parse_result::hyphen_value();

                return parse_result::unmatched(std::string{spelling});
            }

            /**
             * \brief The unique argument whose long spelling starts with \p prefix.
             *
             * clap's `infer_long_args` arm. Aliases participate, and ambiguity means no
             * match rather than the first match — inferring `--ver` to `--verbose` when
             * `--version` also exists would be a silently wrong parse.
             */
            [[nodiscard]] const arg_spec *infer_long_arg(std::string_view prefix) const noexcept {
                const arg_spec *only = nullptr;
                bool ambiguous = false;
                for (const arg_spec &candidate: cmd_->get_arguments()) {
                    bool hit = false;
                    if (const std::optional<std::string_view> spelling = candidate.get_long();
                        spelling.has_value() && spelling->starts_with(prefix)) {
                        hit = true;
                    } else {
                        for (const alias_spec &alias: candidate.get_all_aliases()) {
                            if (alias.name.name().starts_with(prefix)) {
                                hit = true;
                                break;
                            }
                        }
                    }
                    if (!hit) continue;
                    if (only != nullptr) {
                        ambiguous = true;
                        break;
                    }
                    only = &candidate;
                }
                return ambiguous ? nullptr : only;
            }

            /**
             * \brief Turn a long-option outcome into the loop's next move.
             *
             * The body of clap's `match parse_result` for the long branch, lifted out so
             * the loop stays readable.
             */
            std::expected<token_flow, error>
            apply_long_result(arg_matcher &matcher,
                              const raw_args &raw,
                              arg_cursor &cursor,
                              parse_result &outcome,
                              parse_state &state,
                              std::optional<std::string> &subcmd_name,
                              bool trailing_values) {
                switch (outcome.kind) {
                    case parse_result_kind::no_arg:
                    case parse_result_kind::values_done:
                        state = parse_state::done();
                        return token_flow::next_token;
                    case parse_result_kind::opt:
                        state = parse_state::option(outcome.id);
                        return token_flow::next_token;
                    case parse_result_kind::flag_subcommand:
                        subcmd_name = std::move(outcome.text);
                        return token_flow::stop_loop;
                    case parse_result_kind::equals_not_provided:
                        discard_result(resolve_pending(matcher));
                        return std::unexpected(
                            error::no_equals(cow_str{std::move(outcome.text)}, usage()));
                    case parse_result_kind::no_matching_arg: {
                        discard_result(resolve_pending(matcher));
                        std::vector<os_str> rest;
                        for (const os_str one: raw.remaining(cursor)) rest.push_back(one);
                        return std::unexpected(
                            did_you_mean_error(outcome.text, matcher, rest, trailing_values));
                    }
                    case parse_result_kind::unneeded_attached_value:
                        discard_result(resolve_pending(matcher));
                        return std::unexpected(error::too_many_values(cow_str{std::move(outcome.text)},
                                                                      cow_str{std::move(outcome.extra)},
                                                                      smart_usage(outcome.used)));
                    case parse_result_kind::maybe_hyphen_value:
                    case parse_result_kind::attached_value_not_consumed:
                        break;
                }
                return token_flow::fall_through;
            }

            // ---------------------------------------------------------------
            // Short options
            // ---------------------------------------------------------------

            /**
             * \brief Examine one `-abc` cluster. clap's `parse_short_arg`.
             *
             * \param matcher         The accumulator.
             * \param cluster         The flags, by value: the trailing-value probe below
             *                        needs a rollback point, which is clap's `.clone()`.
             * \param state           What the previous token left open.
             * \param pos_counter     The positional slot currently in play.
             * \param valid_arg_found Set when any flag in the cluster matched.
             *
             * \note A cluster is examined flag by flag, and the first one that takes a
             *       value swallows the remainder — `-vo file` is `-v -o file`, but
             *       `-vofile` is `-v -o file` too. The rollback matters for
             *       `require_equals` with `min_values == 0`, where the remainder is
             *       *not* the value and parsing continues through the cluster.
             */
            std::expected<parse_result, error> parse_short_arg(arg_matcher &matcher,
                                                               short_flags cluster,
                                                               const parse_state &state,
                                                               std::size_t pos_counter,
                                                               bool &valid_arg_found) {
                if (state_allows_hyphen_values(state, cluster.is_negative_number()))
                    return parse_result::hyphen_value();

                const arg_spec *here = positional_at(*cmd_, pos_counter);
                if (here != nullptr && here->is_allow_negative_numbers_set() &&
                    cluster.is_negative_number())
                    return parse_result::hyphen_value();
                if (here != nullptr && here->is_allow_hyphen_values_set() && !here->is_last_set() &&
                    cluster_has_unknown_flag(cluster))
                    return parse_result::hyphen_value();

                parse_result ret = parse_result::none();

                const std::size_t skip = flag_subcmd_skip_;
                flag_subcmd_skip_ = 0;
                discard_result(cluster.advance_by(skip));

                while (const std::optional<short_flags::flag_result> letter = cluster.next_flag()) {
                    if (!letter->has_value()) {
                        std::string spelling;
                        spelling.push_back('-');
                        append_bytes(spelling, letter->error().to_string_lossy());
                        return parse_result::unmatched(std::move(spelling));
                    }
                    const char32_t code = **letter;

                    if (const arg_spec *found = short_arg_at_code(code); found != nullptr) {
                        valid_arg_found = true;
                        if (!found->is_takes_value_set()) {
                            std::expected<parse_result, error> reacted =
                                    react(arg_identifier::short_,
                                          clapp::value_source::command_line,
                                          *found,
                                          {},
                                          no_trailing_values,
                                          matcher);
                            if (!reacted.has_value())
                                return std::unexpected(std::move(reacted.error()));
                            ret = std::move(*reacted);
                            continue;
                        }

                        short_flags probe = cluster;
                        std::optional<os_str> attached = probe.next_value_os();
                        if (attached.has_value() && attached->empty()) attached.reset();
                        bool has_eq = false;
                        if (attached.has_value()) {
                            if (const std::optional<os_str> after_eq =
                                        attached->strip_prefix(os_str{"="});
                                after_eq.has_value()) {
                                attached = after_eq;
                                has_eq = true;
                            }
                        }

                        std::expected<parse_result, error> outcome = parse_opt_value(
                            arg_identifier::short_, attached, *found, matcher, has_eq);
                        if (!outcome.has_value())
                            return std::unexpected(std::move(outcome.error()));
                        if (outcome->kind == parse_result_kind::attached_value_not_consumed)
                            continue;
                        return outcome;
                    }

                    if (const command_spec *sub = short_subcommand_at_code(code); sub != nullptr) {
                        if (std::expected<void, error> resolved = resolve_pending(matcher);
                            !resolved.has_value())
                            return std::unexpected(std::move(resolved.error()));
                        ++cur_idx_;
                        if (!flag_subcmd_at_.has_value()) flag_subcmd_at_ = cur_idx_;
                        if (cluster.is_empty()) flag_subcmd_at_.reset();
                        return parse_result::subcommand(std::string{sub->get_name()});
                    }

                    std::string spelling;
                    spelling.push_back('-');
                    append_utf8(spelling, code);
                    return parse_result::unmatched(std::move(spelling));
                }
                return ret;
            }

            /**
             * \brief Whether any flag in \p cluster is unknown to this command.
             *
             * clap's `short_arg.clone().any(|c| !cmd.contains_short(c))`, which is what
             * lets `-3.5` reach a positional that declared `allow_hyphen_values` while
             * `-v` still reaches the flag `-v`.
             */
            [[nodiscard]] bool cluster_has_unknown_flag(short_flags cluster) const noexcept {
                while (const std::optional<short_flags::flag_result> letter = cluster.next_flag()) {
                    if (!letter->has_value()) return true;
                    if (short_arg_at_code(**letter) == nullptr) return true;
                }
                return false;
            }

            /**
             * \brief short_arg_at() for a decoded code point.
             * \note clapp::short_flags decodes UTF-8, so a flag can be any scalar value;
             *       clapp::arg_spec::matches_short() takes a `char`. Non-ASCII therefore
             *       never matches, which is correct — clapp::arg_builder::short_() takes
             *       a `char` too, so no such argument can exist.
             */
            [[nodiscard]] const arg_spec *short_arg_at_code(char32_t code) const noexcept {
                if (code > 0x7FU) return nullptr;
                return short_arg_at(*cmd_, static_cast<char>(code));
            }

            /** \brief clapp::command_spec::find_short_subcommand() for a code point. */
            [[nodiscard]] const command_spec *
            short_subcommand_at_code(char32_t code) const noexcept {
                if (code > 0x7FU) return nullptr;
                return cmd_->find_short_subcommand(static_cast<char>(code));
            }

            /**
             * \brief Turn a short-option outcome into the loop's next move.
             *
             * The body of clap's `match parse_result` for the short branch. The
             * parse_result_kind::flag_subcommand arm carries the cluster-resumption
             * hack: if flags remain after the subcommand letter, the cursor steps back
             * one so the child re-reads the same token, and parser::flag_subcmd_skip_ records
             * how many letters it must skip on the way in.
             */
            std::expected<token_flow, error>
            apply_short_result(arg_matcher &matcher,
                               const raw_args &raw,
                               arg_cursor &cursor,
                               parse_result &outcome,
                               parse_state &state,
                               std::optional<std::string> &subcmd_name,
                               bool &keep_state,
                               bool trailing_values) {
                switch (outcome.kind) {
                    case parse_result_kind::no_arg:
                        // A bare `-`; try the positionals.
                        break;
                    case parse_result_kind::values_done:
                        state = parse_state::done();
                        return token_flow::next_token;
                    case parse_result_kind::opt:
                        state = parse_state::option(outcome.id);
                        return token_flow::next_token;
                    case parse_result_kind::flag_subcommand:
                        if (flag_subcmd_at_.has_value()) {
                            raw.seek(cursor, seek_from::current(-1));
                            flag_subcmd_skip_ = cur_idx_ - *flag_subcmd_at_ + 1;
                            keep_state = true;
                        }
                        subcmd_name = std::move(outcome.text);
                        return token_flow::stop_loop;
                    case parse_result_kind::equals_not_provided:
                        discard_result(resolve_pending(matcher));
                        return std::unexpected(
                            error::no_equals(cow_str{std::move(outcome.text)}, usage()));
                    case parse_result_kind::no_matching_arg: {
                        discard_result(resolve_pending(matcher));
                        // It already looks like a flag, so no fuzzy search — clap only
                        // spells "did you mean" for longs.
                        const bool suggested_trailing_arg = !trailing_values && cmd_->has_positionals();
                        return std::unexpected(error::unknown_argument(cow_str{std::move(outcome.text)},
                                                                       std::nullopt,
                                                                       std::nullopt,
                                                                       suggested_trailing_arg,
                                                                       usage()));
                    }
                    case parse_result_kind::maybe_hyphen_value:
                    case parse_result_kind::unneeded_attached_value:
                    case parse_result_kind::attached_value_not_consumed:
                        break;
                }
                return token_flow::fall_through;
            }

            // ---------------------------------------------------------------
            // Values
            // ---------------------------------------------------------------

            /**
             * \brief Decide what an option does with the value attached to it.
             *        clap's `parse_opt_value`.
             *
             * \param ident    How the option was spelled.
             * \param attached The `=value` or `-ovalue` part, when there was one.
             * \param arg      The option.
             * \param matcher  The accumulator.
             * \param has_eq   Whether an `=` was actually present.
             *
             * \note `require_equals` with `min_values() == 0` is the subtle case: `-c`
             *       fires the option with no values, and any attached text is handed
             *       back to the cluster as more flags rather than eaten as a value.
             */
            std::expected<parse_result, error>
            parse_opt_value(arg_identifier ident,
                            const std::optional<os_str> &attached,
                            const arg_spec &arg,
                            arg_matcher &matcher,
                            bool has_eq) {
                if (arg.is_require_equals_set() && !has_eq) {
                    if (arg.get_min_vals() == 0) {
                        std::expected<parse_result, error> reacted =
                                react(ident,
                                      clapp::value_source::command_line,
                                      arg,
                                      {},
                                      no_trailing_values,
                                      matcher);
                        if (!reacted.has_value())
                            return std::unexpected(std::move(reacted.error()));
                        return attached.has_value()
                                   ? parse_result::attached_not_consumed()
                                   : parse_result::done();
                    }
                    return parse_result::needs_equals(arg_display(arg));
                }

                if (attached.has_value()) {
                    std::vector<os_string> values;
                    values.emplace_back(*attached);
                    std::expected<parse_result, error> reacted =
                            react(ident,
                                  clapp::value_source::command_line,
                                  arg,
                                  std::move(values),
                                  no_trailing_values,
                                  matcher);
                    if (!reacted.has_value()) return std::unexpected(std::move(reacted.error()));
                    return parse_result::done(); // an attached value is always complete
                }

                if (std::expected<void, error> resolved = resolve_pending(matcher);
                    !resolved.has_value())
                    return std::unexpected(std::move(resolved.error()));
                // Open an empty pending: that is how the next token is read as this
                // option's value rather than as a positional.
                matcher.pending_values(arg.get_id(), ident, false);
                return parse_result::option(arg.get_id());
            }

            /**
             * \brief Parse and commit \p raw_values to \p arg. clap's `push_arg_values`.
             * \param arg        Argument they belong to.
             * \param raw_values Bytes, moved from.
             * \param matcher    Accumulator.
             * \note cur_idx_ increments **per value** (not per occurrence) so indices interleave.
             */
            std::expected<void, error> push_arg_values(const arg_spec &arg,
                                                       std::vector<os_string> raw_values,
                                                       arg_matcher &matcher) {
                for (os_string &raw_value: raw_values) {
                    ++cur_idx_;
                    std::expected<any_value, parse_error> parsed = arg.get_value_parser()->parse(
                        raw_value.view(), arg.is_ignore_case_set());
                    if (!parsed.has_value())
                        return std::unexpected(value_error(arg, raw_value.view(), parsed.error()));
                    matcher.add_val_to(arg.get_id(), std::move(*parsed), std::move(raw_value));
                    matcher.add_index_to(arg.get_id(), cur_idx_);
                }
                return {};
            }

            /**
             * \brief The error a failed value_parser becomes.
             * \param arg   Argument whose parser rejected the bytes.
             * \param value Those bytes.
             * \param why   What the parser said.
             *
             * \note Enumerated domain → invalid_value with accepted names; else
             *       value_validation via value_reason().
             *
             * \warning **empty_value is a policy, not an observation.** PathBuf /
             *          NonEmptyString treat empty as no value; numeric/char report
             *          conversion failure → value_validation (`--port ""` did supply a
             *          value). clap: `--port ""` is ValueValidation; `--path ""` is
             *          InvalidValue.
             */
            [[nodiscard]] static error value_error(const arg_spec &arg,
                                                   os_str value,
                                                   const parse_error &why) {
                const std::string shown = value.to_string_lossy();
                if (why.has_possible_values()) {
                    std::vector<cow_str> accepted;
                    for (const std::string_view name: why.visible_values())
                        accepted.push_back(cow_str::owned(name));
                    return error::invalid_value(
                        cow_str{arg_display(arg)}, cow_str::owned(shown), std::move(accepted));
                }
                if (why.kind == parse_error_kind::empty_value)
                    return error::empty_value(cow_str{arg_display(arg)}, accepted_values(arg));
                return error::value_validation(cow_str{arg_display(arg)},
                                               cow_str::owned(shown),
                                               cow_str{value_reason(shown, why)});
            }

            /**
             * \brief value_error() for the external subcommand parser, which has no
             *        clapp::arg_spec to name.
             */
            [[nodiscard]] static error external_value_error(os_str value,
                                                            const parse_error &why) {
                const std::string shown = value.to_string_lossy();
                return error::value_validation(cow_str::borrowed("<subcommand argument>"),
                                               cow_str::owned(shown),
                                               cow_str{value_reason(shown, why)});
            }

            // ---------------------------------------------------------------
            // Committing an occurrence
            // ---------------------------------------------------------------

            /**
             * \brief Close the argument in flight, if any. clap's `resolve_pending`.
             *
             * \param matcher The accumulator.
             * \return Nothing, or whatever react() reported.
             *
             * \note **Call this before opening a second argument.** It is the discipline
             *       clapp::detail::arg_matcher::pending_values() aborts to enforce, and
             *       every path in this class that recognizes a new argument goes through
             *       react(), which begins with exactly this call.
             */
            std::expected<void, error> resolve_pending(arg_matcher &matcher) {
                std::optional<pending_arg> pending = matcher.take_pending();
                if (!pending.has_value()) return {};

                const arg_spec *arg = cmd_->find_arg(pending->id.name());
                if (arg == nullptr) return {}; // clap: unreachable, `expect(INTERNAL_ERROR_MSG)`

                std::expected<parse_result, error> reacted =
                        react(pending->ident,
                              clapp::value_source::command_line,
                              *arg,
                              std::move(pending->raw_values),
                              pending->trailing_index.value_or(no_trailing_values),
                              matcher);
                if (!reacted.has_value()) return std::unexpected(std::move(reacted.error()));
                return {};
            }

            /**
             * \brief Turn one complete match into matcher state. clap's `react`.
             *
             * Resolve pending → verify arity → default_missing_value → delimiter split
             * → dispatch on arg_action.
             *
             * \param ident        How spelled, if at all.
             * \param source       Command line, environment, or default.
             * \param arg          The argument.
             * \param raw_values   Bytes it received.
             * \param trailing_idx Where trailing values begin, or no_trailing_values.
             * \param matcher      Accumulator.
             * \return values_done, or an error (including display_help / display_version).
             *
             * \note Arity checked **before** default_missing_value (`--flag=` still errors).
             */
            std::expected<parse_result, error> react(std::optional<arg_identifier> ident,
                                                     clapp::value_source source,
                                                     const arg_spec &arg,
                                                     std::vector<os_string> raw_values,
                                                     std::size_t trailing_idx,
                                                     arg_matcher &matcher) {
                if (std::expected<void, error> resolved = resolve_pending(matcher);
                    !resolved.has_value())
                    return std::unexpected(std::move(resolved.error()));

                if (source == clapp::value_source::command_line) {
                    if (std::expected<void, error> checked = verify_num_args(arg, raw_values);
                        !checked.has_value())
                        return std::unexpected(std::move(checked.error()));
                }

                if (raw_values.empty()) {
                    const std::span<const arg_id> missing = arg.get_default_missing_values();
                    if (!missing.empty()) {
                        trailing_idx = no_trailing_values;
                        for (const arg_id &one: missing)
                            raw_values.emplace_back(std::string{one.name()});
                    }
                }

                if (const std::optional<char> delimiter = arg.get_value_delimiter();
                    delimiter.has_value())
                    raw_values =
                            split_on_delimiter(std::move(raw_values), *delimiter, trailing_idx);

                switch (arg.get_action()) {
                    case arg_action::set: {
                        bump_index_for_flag(source, ident);
                        if (matcher.remove(arg.get_id().name()) && !overrides_itself(arg))
                            return std::unexpected(self_conflict(arg));
                        start_custom_arg(matcher, arg, source);
                        if (std::expected<void, error> pushed =
                                    push_arg_values(arg, std::move(raw_values), matcher);
                            !pushed.has_value())
                            return std::unexpected(std::move(pushed.error()));
                        return parse_result::done();
                    }
                    case arg_action::append: {
                        bump_index_for_flag(source, ident);
                        start_custom_arg(matcher, arg, source);
                        if (std::expected<void, error> pushed =
                                    push_arg_values(arg, std::move(raw_values), matcher);
                            !pushed.has_value())
                            return std::unexpected(std::move(pushed.error()));
                        return parse_result::done();
                    }
                    case arg_action::set_true:
                    case arg_action::set_false: {
                        if (raw_values.empty())
                            raw_values.emplace_back(arg.get_action() == arg_action::set_true
                                                        ? std::string{"true"}
                                                        : std::string{"false"});
                        if (matcher.remove(arg.get_id().name()) && !overrides_itself(arg))
                            return std::unexpected(self_conflict(arg));
                        start_custom_arg(matcher, arg, source);
                        if (std::expected<void, error> pushed =
                                    push_arg_values(arg, std::move(raw_values), matcher);
                            !pushed.has_value())
                            return std::unexpected(std::move(pushed.error()));
                        return parse_result::done();
                    }
                    case arg_action::count: {
                        if (raw_values.empty())
                            raw_values.emplace_back(spell_number(next_count(matcher, arg)));
                        static_cast<void>(matcher.remove(arg.get_id().name()));
                        start_custom_arg(matcher, arg, source);
                        if (std::expected<void, error> pushed =
                                    push_arg_values(arg, std::move(raw_values), matcher);
                            !pushed.has_value())
                            return std::unexpected(std::move(pushed.error()));
                        return parse_result::done();
                    }
                    case arg_action::help:
                        return std::unexpected(help_err(prefers_long_form(ident)));
                    case arg_action::help_short:
                        return std::unexpected(help_err(false));
                    case arg_action::help_long:
                        return std::unexpected(help_err(true));
                    case arg_action::version:
                        return std::unexpected(version_err(prefers_long_form(ident)));
                    case arg_action::infer:
                        break;
                }
                // `infer` never survives clapp::command_builder::freeze(); treat a
                // hand-built spec that carries it as `set`.
                bump_index_for_flag(source, ident);
                start_custom_arg(matcher, arg, source);
                if (std::expected<void, error> pushed =
                            push_arg_values(arg, std::move(raw_values), matcher);
                    !pushed.has_value())
                    return std::unexpected(std::move(pushed.error()));
                return parse_result::done();
            }

            /**
             * \brief Whether \p arg lists itself among its `overrides_with`.
             * \note Together with `args_override_self` this is what turns a repeated
             *       `--set` from an error into a last-one-wins assignment.
             */
            [[nodiscard]] bool overrides_itself(const arg_spec &arg) const noexcept {
                if (cmd_->is_args_override_self()) return true;
                return std::ranges::any_of(arg.get_overrides(), [&](const arg_id &id) {
                    return id.name() == arg.get_id().name();
                });
            }

            /** \brief The error a repeated non-overriding `set` produces. */
            [[nodiscard]] error self_conflict(const arg_spec &arg) const {
                std::vector<cow_str> others;
                others.push_back(cow_str{arg_display(arg)});
                return error::argument_conflict(
                    cow_str{arg_display(arg)}, std::move(others), usage());
            }

            /**
             * \brief Give the flag itself an index, as clap does for `set` and `append`.
             * \note Only explicitly spelled command-line flags; env/default have no position.
             */
            void bump_index_for_flag(clapp::value_source source,
                                     std::optional<arg_identifier> ident) noexcept {
                if (source != clapp::value_source::command_line) return;
                if (!ident.has_value()) return;
                if (!is_named(*ident)) return;
                ++cur_idx_;
            }

            /**
             * \brief The value a `count` occurrence should store.
             * \return The previous count plus one, saturating at
             *         `std::numeric_limits<clapp::count_type>::max()`.
             */
            [[nodiscard]] static std::size_t next_count(const arg_matcher &matcher,
                                                        const arg_spec &arg) {
                count_type existing = 0;
                if (const matched_arg *found = matcher.get(arg.get_id().name()); found != nullptr) {
                    const std::span<const any_value> values = found->values();
                    if (!values.empty()) {
                        if (const count_type *stored = values.front().try_get<count_type>();
                            stored != nullptr)
                            existing = *stored;
                    }
                }
                if (existing == std::numeric_limits<count_type>::max())
                    return static_cast<std::size_t>(existing);
                return static_cast<std::size_t>(existing) + 1U;
            }

            /**
             * \brief Split every value on \p delimiter. clap's value-delimiter arm.
             * \param values       Values, moved from.
             * \param delimiter    Byte to split on.
             * \param trailing_idx Where trailing values begin, or no_trailing_values.
             * \return Split values.
             * \note `dont_delimit_trailing_values` suppresses split at/after `--`
             *       (clap exempts only the first trailing index; asymmetry kept).
             */
            [[nodiscard]] std::vector<os_string> split_on_delimiter(
                std::vector<os_string> values, char delimiter, std::size_t trailing_idx) const {
                const bool keep_trailing = cmd_->is_dont_delimit_trailing_values_set();
                if (keep_trailing && trailing_idx == 0) return values;

                const char separator_byte = delimiter;
                const std::string_view separator{&separator_byte, 1};

                std::vector<os_string> split;
                split.reserve(values.size());
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if ((keep_trailing && trailing_idx == i) ||
                        !values[i].view().contains(separator_byte)) {
                        split.push_back(std::move(values[i]));
                        continue;
                    }
                    for (const os_str piece: values[i].view().split(os_str{separator}))
                        split.emplace_back(piece);
                }
                return split;
            }

            /**
             * \brief Check value count against `num_args`. clap's `verify_num_args`.
             * \param arg        The argument.
             * \param raw_values What it received.
             * \note Zero values when ≥1 required is empty_value(), not too_few_values.
             */
            [[nodiscard]] std::expected<void, error>
            verify_num_args(const arg_spec &arg, std::span<const os_string> raw_values) const {
                if (cmd_->is_ignore_errors_set()) return {};

                const std::size_t actual = raw_values.size();
                const value_range expected =
                        arg.get_num_args().resolve_or(default_num_args(arg.get_action()));

                if (expected.min_values() > 0 && actual == 0)
                    return std::unexpected(
                        error::empty_value(cow_str{arg_display(arg)}, accepted_values(arg)));

                if (const std::optional<std::size_t> exact = expected.num_values();
                    exact.has_value()) {
                    if (*exact != actual)
                        return std::unexpected(error::wrong_number_of_values(
                            cow_str{arg_display(arg)}, *exact, actual, usage()));
                } else if (actual < expected.min_values()) {
                    return std::unexpected(error::too_few_values(
                        cow_str{arg_display(arg)}, expected.min_values(), actual, usage()));
                } else if (expected.max_values() < actual) {
                    return std::unexpected(error::too_many_values(
                        cow_str{arg_display(arg)},
                        cow_str::owned(raw_values.back().view().to_string_lossy()),
                        usage()));
                }
                return {};
            }

            /**
             * \brief Drop everything \p arg overrides, and everything that overrides it.
             *        clap's `remove_overrides`.
             * \note Second half makes `--a --b` and `--b --a` agree on the survivor.
             */
            void remove_overrides(const arg_spec &arg, arg_matcher &matcher) const {
                for (const arg_id &id: arg.get_overrides())
                    static_cast<void>(matcher.remove(id.name()));

                std::vector<std::string_view> transitive;
                for (const arg_id &id: matcher.arg_ids()) {
                    const arg_spec *overrider = cmd_->find_arg(id.name());
                    if (overrider == nullptr) continue;
                    const bool overrides_us =
                            std::ranges::any_of(overrider->get_overrides(), [&](const arg_id &one) {
                                return one.name() == arg.get_id().name();
                            });
                    if (overrides_us) transitive.push_back(id.name());
                }
                for (const std::string_view id: transitive) static_cast<void>(matcher.remove(id));
            }

            /**
             * \brief Open an occurrence and record the groups it fills.
             *        clap's `Parser::start_custom_arg`.
             * \note Group entry stores member arg_id as value (which members matched).
             * \note Only explicit sources fill groups (defaults do not occupy).
             */
            void start_custom_arg(arg_matcher &matcher,
                                  const arg_spec &arg,
                                  clapp::value_source source) const {
                if (source == clapp::value_source::command_line) remove_overrides(arg, matcher);
                matcher.start_custom_arg(arg, source);
                if (!is_explicit(source)) return;

                for (const std::string_view name: cmd_->groups_for_arg(arg.get_id().name())) {
                    const group_spec *group = cmd_->find_group(name);
                    if (group == nullptr) continue;
                    matcher.start_custom_group(group->get_id(), source);
                    matcher.add_val_to(group->get_id(),
                                       any_value(std::in_place_type<arg_id>, arg.get_id()),
                                       os_string{std::string{arg.get_id().name()}});
                }
            }

            // ---------------------------------------------------------------
            // The later waves: environment, then defaults
            // ---------------------------------------------------------------

            /**
             * \brief Apply every `env()` the command line did not set. clap's `add_env`.
             * \note Read at parse time (tree is frozen at compile time).
             * \note `std::getenv` is not thread-safe against `setenv` (same as Rust).
             */
            std::expected<void, error> add_env(arg_matcher &matcher) {
                for (const arg_spec &arg: cmd_->get_arguments()) {
                    if (matcher.contains(arg.get_id().name())) continue;
                    const std::optional<std::string_view> variable = arg.get_env();
                    if (!variable.has_value()) continue;

                    const std::string name{*variable};
                    const char *const found = std::getenv(name.c_str());
                    if (found == nullptr) continue;

                    std::vector<os_string> values;
                    values.emplace_back(std::string{found});
                    std::expected<parse_result, error> reacted =
                            react(std::nullopt,
                                  clapp::value_source::env_variable,
                                  arg,
                                  std::move(values),
                                  no_trailing_values,
                                  matcher);
                    if (!reacted.has_value()) return std::unexpected(std::move(reacted.error()));
                }
                return {};
            }

            /** \brief Apply every default. clap's `add_defaults`. */
            std::expected<void, error> add_defaults(arg_matcher &matcher) {
                for (const arg_spec &arg: cmd_->get_arguments()) {
                    if (std::expected<void, error> applied = add_default_value(arg, matcher);
                        !applied.has_value())
                        return applied;
                }
                return {};
            }

            /**
             * \brief Apply \p arg's defaults. clap's `add_default_value`.
             * \note Conditional defaults: first matching rule wins (including empty
             *       values that suppress unconditional default_value).
             */
            std::expected<void, error> add_default_value(const arg_spec &arg,
                                                         arg_matcher &matcher) {
                const std::span<const default_value_spec> rules = arg.get_default_values_ifs();
                if (!rules.empty() && !matcher.contains(arg.get_id().name())) {
                    for (const default_value_spec &rule: rules) {
                        const matched_arg *watched = matcher.get(rule.id.name());
                        if (watched == nullptr) continue;

                        bool fires = rule.when.is_present_only();
                        if (!fires) {
                            for (const os_string &seen: watched->raw_values()) {
                                if (rule.when.matches(seen.view())) {
                                    fires = true;
                                    break;
                                }
                            }
                        }
                        if (!fires) continue;

                        if (rule.has_values()) {
                            std::vector<os_string> values;
                            for (const arg_id &one: rule.values())
                                values.emplace_back(std::string{one.name()});
                            std::expected<parse_result, error> reacted =
                                    react(std::nullopt,
                                          clapp::value_source::default_value,
                                          arg,
                                          std::move(values),
                                          no_trailing_values,
                                          matcher);
                            if (!reacted.has_value())
                                return std::unexpected(std::move(reacted.error()));
                        }
                        return {};
                    }
                }

                const std::span<const arg_id> defaults = arg.get_default_values();
                if (defaults.empty()) return {};
                if (matcher.contains(arg.get_id().name())) return {};

                std::vector<os_string> values;
                for (const arg_id &one: defaults) values.emplace_back(std::string{one.name()});
                std::expected<parse_result, error> reacted =
                        react(std::nullopt,
                              clapp::value_source::default_value,
                              arg,
                              std::move(values),
                              no_trailing_values,
                              matcher);
                if (!reacted.has_value()) return std::unexpected(std::move(reacted.error()));
                return {};
            }

            // ---------------------------------------------------------------
            // Diagnostics
            // ---------------------------------------------------------------

            /**
             * \brief Advance `pos_counter` past slots this token cannot belong to.
             *
             * clap's inline `pos_counter = { ... }` block, which is where
             * `allow_missing_positional` and "low index multiples" live.
             *
             * \param raw             The token stream, for the one-token lookahead.
             * \param cursor          Positioned just after the current token.
             * \param pos_counter     The slot currently in play.
             * \param slots           How many positional slots exist.
             * \param contains_last   Whether any argument declared `last(true)`.
             * \param trailing_values Whether a `--` has been seen.
             * \param valid_arg_found Whether an argument already matched.
             * \return The corrected slot.
             *
             * \note "Low index multiples" is clap's name for `a... b`: the
             *       multi-valued positional is *not* last, so the parser must look ahead
             *       to decide whether the current token is the last value for `a` or the
             *       only value for `b`.
             */
            [[nodiscard]] std::size_t correct_positional_counter(const raw_args &raw,
                                                                 const arg_cursor &cursor,
                                                                 std::size_t pos_counter,
                                                                 std::size_t slots,
                                                                 bool contains_last,
                                                                 bool trailing_values,
                                                                 bool valid_arg_found) const {
                const bool second_to_last = (pos_counter + 1 == slots);

                bool low_index_mults = false;
                if (second_to_last) {
                    const bool any_multiple =
                            std::ranges::any_of(cmd_->get_positionals(), [&](const arg_spec &a) {
                                return a.is_multiple() && slots != a.get_index().value_or(0);
                            });
                    const arg_spec *last = last_positional(*cmd_);
                    low_index_mults = any_multiple && last != nullptr && !last->is_last_set();
                }

                const arg_spec *here = positional_at(*cmd_, pos_counter);
                const bool terminated = here != nullptr && here->get_value_terminator().has_value();
                const bool missing_pos = cmd_->is_allow_missing_positional_set() &&
                                         second_to_last && !trailing_values;

                if ((low_index_mults || missing_pos) && !terminated) {
                    bool skip_current = true;
                    if (const std::optional<parsed_arg> peeked = raw.peek(cursor);
                        peeked.has_value() && here != nullptr) {
                        skip_current = is_new_arg(*peeked, *here) ||
                                       possible_subcommand(peeked->to_value(), valid_arg_found)
                                       .has_value();
                    }
                    return skip_current ? pos_counter + 1 : pos_counter;
                }
                if (trailing_values && (cmd_->is_allow_missing_positional_set() || contains_last))
                    return slots;
                return pos_counter;
            }

            /**
             * \brief Error for a token that matched nothing. clap's `match_arg_error`.
             * \note After `--`, a subcommand name is unnecessary_double_dash, not unknown.
             */
            [[nodiscard]] error match_arg_error(const parsed_arg &token,
                                                bool valid_arg_found,
                                                bool trailing_values,
                                                const arg_matcher &matcher) const {
                const std::string shown = token.display();

                if (trailing_values &&
                    possible_subcommand(token.to_value(), valid_arg_found).has_value())
                    return error::unnecessary_double_dash(cow_str{shown}, usage());

                const bool suggested_trailing_arg = !trailing_values && cmd_->has_positionals() &&
                                                    (token.is_long() || token.is_short());

                if (cmd_->has_subcommands()) {
                    if (cmd_->is_args_conflicts_with_subcommands_set() && valid_arg_found)
                        return error::subcommand_conflict(
                            cow_str{shown}, used_arg_names(matcher), usage());

                    const std::vector<std::string_view> known = cmd_->all_subcommand_names();
                    const std::vector<std::string_view> close = clapp::did_you_mean(shown, known);
                    if (!close.empty()) {
                        std::vector<cow_str> hints;
                        hints.reserve(close.size());
                        for (const std::string_view one: close)
                            hints.push_back(cow_str::owned(one));
                        // Flag-shaped unknowns have already returned unknown_argument;
                        // a positional that accepts hyphen values consumes them. The only
                        // live path here is an ordinary subcommand misspelling, never a
                        // trailing flag that needs a `--` escape hint.
                        return error::invalid_subcommand(cow_str{shown}, std::move(hints), usage());
                    }

                    if (!cmd_->has_positionals() || cmd_->is_infer_subcommands_set())
                        return error::unrecognized_subcommand(cow_str{shown}, usage());
                }

                return error::unknown_argument(cow_str{shown},
                                               std::nullopt,
                                               std::nullopt,
                                               suggested_trailing_arg,
                                               usage());
            }

            /**
             * \brief Error for an unrecognized `--long`, with a suggestion.
             *        clap's `did_you_mean_error`.
             * \param flag            Spelling without dashes.
             * \param matcher         Accumulator (suggested arg may be recorded).
             * \param remaining_args  Tokens after the offender (which sub owns the flag).
             * \param trailing_values Whether a `--` has been seen.
             * \note The `-- VALUE` tip is suppressed when a flag was suggested (typo likelier).
             */
            [[nodiscard]] error did_you_mean_error(std::string_view flag,
                                                   arg_matcher &matcher,
                                                   std::span<const os_str> remaining_args,
                                                   bool trailing_values) const {
                std::vector<std::string_view> longs;
                for (const arg_spec &candidate: cmd_->get_arguments()) {
                    if (const std::optional<std::string_view> spelling = candidate.get_long();
                        spelling.has_value())
                        longs.push_back(*spelling);
                    for (const alias_spec &alias: candidate.get_all_aliases())
                        longs.push_back(alias.name.name());
                }

                std::optional<std::string_view> suggestion = best_match(flag, longs);
                std::optional<std::string_view> from_subcommand;

                if (!suggestion.has_value()) {
                    std::size_t best_position = std::numeric_limits<std::size_t>::max();
                    for (const command_spec &sub: cmd_->get_subcommands()) {
                        std::vector<std::string_view> sub_longs;
                        for (const arg_spec &candidate: sub.get_arguments()) {
                            if (const std::optional<std::string_view> spelling =
                                        candidate.get_long();
                                spelling.has_value())
                                sub_longs.push_back(*spelling);
                            for (const alias_spec &alias: candidate.get_all_aliases())
                                sub_longs.push_back(alias.name.name());
                        }
                        const std::optional<std::string_view> hit = best_match(flag, sub_longs);
                        if (!hit.has_value()) continue;

                        std::size_t position = std::numeric_limits<std::size_t>::max();
                        for (std::size_t i = 0; i < remaining_args.size(); ++i) {
                            if (remaining_args[i] == os_str{sub.get_name()}) {
                                position = i;
                                break;
                            }
                        }
                        if (position == std::numeric_limits<std::size_t>::max()) continue;
                        if (position < best_position) {
                            best_position = position;
                            suggestion = hit;
                            from_subcommand = sub.get_name();
                        }
                    }
                }

                if (suggestion.has_value() && !cmd_->is_ignore_errors_set()) {
                    if (const arg_spec *found = long_arg_at(*cmd_, *suggestion); found != nullptr)
                        start_custom_arg(matcher, *found, clapp::value_source::command_line);
                }

                const bool captures_everything =
                        std::ranges::any_of(cmd_->get_positionals(), [](const arg_spec &a) {
                            return a.is_last_set() || a.is_trailing_var_arg_set();
                        });
                const bool suggested_trailing_arg =
                        (!suggestion.has_value() || captures_everything) && !trailing_values &&
                        cmd_->has_positionals();

                std::string offending;
                append_bytes(offending, "--");
                append_bytes(offending, flag);

                std::optional<cow_str> suggested_flag;
                if (suggestion.has_value()) {
                    std::string spelled;
                    append_bytes(spelled, "--");
                    append_bytes(spelled, *suggestion);
                    suggested_flag = cow_str{std::move(spelled)};
                }
                std::optional<cow_str> suggested_subcommand;
                if (from_subcommand.has_value())
                    suggested_subcommand = cow_str::owned(*from_subcommand);

                return error::unknown_argument(cow_str{std::move(offending)},
                                               std::move(suggested_flag),
                                               std::move(suggested_subcommand),
                                               suggested_trailing_arg,
                                               smart_usage(smart_usage_ids(matcher)));
            }

            /**
             * \brief `Usage:` line for this command level (attached to every loop error).
             * \return The line, or nullopt when nothing to show.
             * \note Empty `used` = describe the command, not smart usage (matcher incomplete).
             */
            [[nodiscard]] std::optional<styled_str> usage() const {
                return usage_renderer{*cmd_, required_graph(*cmd_), usage_name_}
                        .create_usage_with_title(std::span<const arg_id>{});
            }

            /**
             * \brief Smart usage: what THIS command line still needs.
             * \param used Ids to force into the line, in command-line order.
             * \return The line, or nullopt.
             * \note Empty \p used degrades to plain usage(). Used args render as required.
             */
            [[nodiscard]] std::optional<styled_str>
            smart_usage(std::span<const arg_id> used) const {
                return usage_renderer{*cmd_, required_graph(*cmd_), usage_name_}
                        .create_usage_with_title(used);
            }

            /**
             * \brief clap's `used` list: what the user supplied, in order.
             * \param matcher  Accumulator.
             * \param offender If set, append it and drop required-graph ids (UnneededAttachedValue).
             * \return Ids, hidden arguments excluded.
             * \note Order from supplied_in_order() (flat_map is sorted).
             */
            [[nodiscard]] std::vector<arg_id>
            smart_usage_ids(const arg_matcher &matcher,
                            std::optional<const arg_spec *> offender = std::nullopt) const {
                const bool drop_required = offender.has_value();
                const id_set required = drop_required ? required_graph(*cmd_) : id_set{};

                std::vector<arg_id> used;
                for (const arg_id &id: supplied_in_order(*cmd_, matcher)) {
                    // supplied_in_order() has already established has_arg(), so the
                    // record exists; no `!= nullptr` is formed. CLAUDE.md trap 10.
                    if (cmd_->find_arg(id.name())->is_hide_set()) continue;
                    if (drop_required && required.contains(id.name())) continue;
                    used.push_back(id);
                }
                if (offender.has_value()) used.push_back((*offender)->get_id());
                return used;
            }

            /**
             * \brief The `--help` control-flow error. clap's `help_err`.
             * \note Returns rather than prints, per ADR-0001: the top-level entry decides
             *       where the text goes and with which exit code.
             */
            [[nodiscard]] error help_err(bool use_long) const {
                return error::display_help(render_help_text(*cmd_, use_long, usage_name_));
            }

            /** \brief The `--version` control-flow error. clap's `version_err`. */
            [[nodiscard]] error version_err(bool use_long) const {
                return error::display_version(render_version_text(*cmd_, use_long));
            }

            /** The command level being parsed. Never null; borrowed. */
            const command_spec *cmd_ = nullptr;
            /**
             * clap's `Command::bin_name`: the space-joined path from the root, e.g.
             * `test sub deep`. Owned, because the frozen spec cannot hold it.
             */
            std::string bin_path_;
            /**
             * clap's `Command::usage_name`: #bin_path_ with this level's flag spellings
             * and its parent's outstanding requirements folded in.
             */
            std::string usage_name_;
            /** clap's `cur_idx`: the running position stamped onto every value. */
            std::size_t cur_idx_ = 0;
            /** clap's `flag_subcmd_at`: where a flag subcommand was found in a cluster. */
            std::optional<std::size_t> flag_subcmd_at_{};
            /** clap's `flag_subcmd_skip`: how many cluster letters the child must skip. */
            std::size_t flag_subcmd_skip_ = 0;
            /**
             * Which command the error in flight is *about*, when that is not #cmd_.
             *
             * clap passes the command into every `Error::*` constructor, so the two can
             * never drift; clapp's constructors are command-free and get_matches_with()
             * supplies the closing "For more information" line afterwards, which needs
             * this. Only help_subcommand_error() ever sets it, and only on the path
             * where the level it reports on is one it never entered.
             *
             * \note `std::optional<const command_spec*>` rather than a null pointer:
             *       `has_value()` is a `bool`, not a pointer comparison. See CLAUDE.md
             *       trap 10.
             */
            std::optional<const command_spec *> error_help_level_{};
        };

        /**
         * \brief Gather the ids of every `global()` argument along the matched path.
         *        clap's `Command::get_used_global_args`.
         *
         * \param cmd     The command level.
         * \param matches Its result, used to find which subcommand actually ran.
         * \param out     Appended to.
         */
        inline void collect_used_global_args(const command_spec &cmd,
                                             const arg_matches &matches,
                                             std::vector<arg_id> &out) {
            for (const arg_spec &one: cmd.get_arguments()) {
                if (one.is_global_set()) out.push_back(one.get_id());
            }
            const std::optional<std::string_view> name = matches.subcommand_name();
            if (!name.has_value()) return;
            const command_spec *sub = cmd.find_subcommand(*name);
            if (sub == nullptr) return;
            const arg_matches *child = matches.subcommand_matches(*name);
            if (child == nullptr) return;
            collect_used_global_args(*sub, *child, out);
        }
    } // namespace detail

    // =======================================================================
    // The seam
    // =======================================================================

    /**
     * \brief Parse \p raw against \p cmd. Public entry point of the parser module.
     *
     * \param cmd Frozen command tree (`command_of<T>()` or command_builder::freeze()).
     * \param raw Command line, including `argv[0]` unless no_binary_name() was set.
     * \return Matches, or the first error — including display_help / display_version
     *         (control flow, ADR-0001). Use error::use_stderr() / exit_code().
     *
     * \code
     * static constexpr auto spec = clapp::command_of<cli>();
     * const std::expected<clapp::arg_matches, clapp::error> got =
     *     clapp::parse(spec, clapp::raw_args::from_args());
     * \endcode
     *
     * \note **`argv[0]` is consumed by default** (no_binary_name turns that off).
     * \note Validation (required, conflicts, groups, …) runs after env/defaults per level.
     * \note **`multicall` copies \p raw** to re-insert the applet name.
     * \note Deliberate deviation: multicall with empty/separator-only argv0 stem
     *       consumes nothing (clap can drop a second token).
     *
     * \warning Not `constexpr` — result owns any_values (non-constexpr dtor). Boundary
     *          drawn at command_of<T>(): compile-time above, runtime below.
     */
    [[nodiscard]] inline std::expected<arg_matches, error> parse(const command_spec &cmd,
                                                                 const raw_args &raw) {
        arg_cursor cursor = raw.cursor();
        const raw_args *stream = &raw;
        raw_args rewritten;

        if (cmd.is_multicall_set()) {
            if (const std::optional<os_str> argv0 = raw.peek_os(cursor); argv0.has_value()) {
                const std::string_view applet = detail::file_stem(argv0->chars());
                if (!applet.empty()) {
                    static_cast<void>(raw.next_os(cursor));
                    rewritten = raw;
                    rewritten.insert(cursor, {os_str{applet}});
                    stream = &rewritten;
                }
            }
        } else if (!cmd.is_no_binary_name_set()) {
            static_cast<void>(raw.next_os(cursor));
        }

        detail::parse_engine engine{cmd};
        detail::arg_matcher matcher{cmd};

        std::expected<void, error> outcome = engine.get_matches_with(matcher, *stream, cursor);
        if (!outcome.has_value()) {
            if (!(cmd.is_ignore_errors_set() && outcome.error().use_stderr()))
                return std::unexpected(std::move(outcome.error()));
        }

        std::vector<arg_id> globals;
        detail::collect_used_global_args(cmd, matcher.matches(), globals);
        matcher.propagate_globals(globals);

        return std::move(matcher).into_inner();
    }

    namespace detail {
        /**
         * Compile-time contract on the token classifier (is_new_arg).
         * Built from string *literals* (trap 10).
         */
        consteval bool a_plain_word_is_never_a_new_arg() {
            constexpr arg_spec plain{.id = arg_id{"file"}, .index = 1};
            constexpr parsed_arg word{os_str{"notes.txt"}};
            constexpr parsed_arg dash{os_str{"-"}};
            return all_true(!is_new_arg(word, plain), !is_new_arg(dash, plain));
        }

        static_assert(a_plain_word_is_never_a_new_arg());

        /** \brief Verify that both long and short flag spellings start a new argument. */
        consteval bool a_flag_is_a_new_arg() {
            constexpr arg_spec plain{.id = arg_id{"file"}, .index = 1};
            constexpr parsed_arg long_flag{os_str{"--verbose"}};
            constexpr parsed_arg short_flag{os_str{"-v"}};
            return all_true(is_new_arg(long_flag, plain), is_new_arg(short_flag, plain));
        }

        static_assert(a_flag_is_a_new_arg());

        /** \brief Verify that hyphen-value mode treats every flag-shaped token as data. */
        consteval bool allow_hyphen_values_swallows_everything() {
            constexpr arg_spec hyphens{
                .id = arg_id{"rest"},
                .index = 1,
                .settings = arg_flags{}.set(arg_setting::allow_hyphen_values)
            };
            constexpr parsed_arg long_flag{os_str{"--verbose"}};
            constexpr parsed_arg short_flag{os_str{"-v"}};
            constexpr parsed_arg number{os_str{"-3"}};
            return all_true(!is_new_arg(long_flag, hyphens),
                            !is_new_arg(short_flag, hyphens),
                            !is_new_arg(number, hyphens));
        }

        static_assert(allow_hyphen_values_swallows_everything());

        /**
         * Compile-time contract: `allow_negative_numbers` is narrower than
         * `allow_hyphen_values`. Conflating the two makes `-v` data.
         */
        consteval bool allow_negative_numbers_takes_only_numbers() {
            constexpr arg_spec numbers{
                .id = arg_id{"delta"},
                .index = 1,
                .settings =
                arg_flags{}.set(arg_setting::allow_negative_numbers)
            };
            constexpr parsed_arg number{os_str{"-3.5"}};
            constexpr parsed_arg flag{os_str{"-v"}};
            return all_true(!is_new_arg(number, numbers), is_new_arg(flag, numbers));
        }

        static_assert(allow_negative_numbers_takes_only_numbers());

        /**
         * Compile-time contract on the terminator test, which closes a variadic
         * argument without consuming the token as a value.
         */
        consteval bool the_terminator_closes_an_argument() {
            constexpr arg_spec listed{
                .id = arg_id{"args"}, .index = 1, .terminator = arg_id{";"}
            };
            constexpr arg_spec plain{.id = arg_id{"args"}, .index = 1};
            return all_true(check_terminator(listed, os_str{";"}),
                            !check_terminator(listed, os_str{"x"}),
                            !check_terminator(plain, os_str{";"}));
        }

        static_assert(the_terminator_closes_an_argument());

        /**
         * Compile-time contract on the positional slot walk. `positional_at` answering
         * for slot `0` would make every named argument look like a positional.
         */
        consteval bool positional_slots_are_one_based() {
            static constexpr arg_spec table[] = {
                arg_spec{.id = arg_id{"flag"}, .short_ = 'f'},
                arg_spec{.id = arg_id{"src"}, .index = 1},
                arg_spec{.id = arg_id{"dst"}, .index = 2},
            };
            constexpr command_spec cmd{.name = arg_id{"demo"}, .arg_data = table, .arg_count = 3};
            // Through the predicate, never `positional_at(...) == nullptr`: the pointer
            // form folds here only because BOTH operands are null, so the present side
            // — the half that actually matters — could not be asserted at all without
            // breaking the `ubsan` preset. CLAUDE.md trap 10.
            return all_true(!has_positional_at(cmd, 0),
                            !has_positional_at(cmd, 3),
                            has_positional_at(cmd, 1),
                            has_positional_at(cmd, 2),
                            positional_at(cmd, 1)->get_id() == "src",
                            positional_at(cmd, 2)->get_id() == "dst",
                            positional_count(cmd) == 2,
                            last_positional(cmd)->get_id() == "dst",
                            !has_last_positional(cmd));
        }

        static_assert(positional_slots_are_one_based());

        /**
         * Compile-time contract: the name lookups see aliases, because clap's keymap
         * does. An implementation that compared `get_long()` alone would reject every
         * alias with no diagnostic.
         */
        consteval bool lookups_see_aliases() {
            static constexpr alias_spec long_aliases[] = {alias_spec{.name = arg_id{"loud"}}};
            static constexpr short_alias_spec short_aliases[] = {short_alias_spec{.name = 'V'}};
            static constexpr arg_spec table[] = {
                arg_spec{
                    .id = arg_id{"verbose"},
                    .short_ = 'v',
                    .long_ = arg_id{"verbose"},
                    .alias_data = long_aliases,
                    .alias_count = 1,
                    .short_alias_data = short_aliases,
                    .short_alias_count = 1
                },
            };
            constexpr command_spec cmd{.name = arg_id{"demo"}, .arg_data = table, .arg_count = 1};
            // Both sides of every lookup, and through the predicate rather than through
            // `!= nullptr` — see positional_slots_are_one_based() above and trap 10.
            return all_true(has_long_arg(cmd, "verbose"),
                            has_long_arg(cmd, "loud"),
                            !has_long_arg(cmd, "quiet"),
                            has_short_arg(cmd, 'v'),
                            has_short_arg(cmd, 'V'),
                            !has_short_arg(cmd, 'q'),
                            long_arg_at(cmd, "verbose")->get_id() == "verbose",
                            long_arg_at(cmd, "loud")->get_id() == "verbose",
                            short_arg_at(cmd, 'v')->get_id() == "verbose",
                            short_arg_at(cmd, 'V')->get_id() == "verbose");
        }

        static_assert(lookups_see_aliases());

        /**
         * Compile-time contract on clapp::detail::arg_display(), the text every
         * diagnostic quotes. Built with `push_back` throughout so it folds under ubsan.
         */
        consteval bool an_argument_names_itself_the_way_clap_does() {
            constexpr arg_spec flag{
                .id = arg_id{"verbose"},
                .short_ = 'v',
                .long_ = arg_id{"verbose"},
                .num_args = value_range::empty()
            };
            constexpr arg_spec option{
                .id = arg_id{"port"},
                .long_ = arg_id{"port"},
                .num_args = value_range::single()
            };
            constexpr arg_spec short_only{
                .id = arg_id{"jobs"}, .short_ = 'j', .num_args = value_range::single()
            };
            constexpr arg_spec positional{
                .id = arg_id{"file"},
                .index = 1,
                .num_args = value_range::single(),
                .settings = arg_flags{}.set(arg_setting::required)
            };
            return all_true(arg_display(flag) == "--verbose",
                            arg_display(option) == "--port <port>",
                            arg_display(short_only) == "-j <jobs>",
                            arg_display(positional) == "<file>");
        }

        static_assert(an_argument_names_itself_the_way_clap_does());

        /**
         * Compile-time contract: one declared value name is repeated to `min_values`.
         * `num_args(2)` rendering as `<point>` instead of `<point> <point>` is the
         * classic way this reads wrong in a `wrong_number_of_values` message.
         */
        consteval bool a_fixed_arity_repeats_its_placeholder() {
            constexpr arg_spec pair{
                .id = arg_id{"point"},
                .long_ = arg_id{"point"},
                .num_args = value_range::exactly(2)
            };
            constexpr arg_spec many{
                .id = arg_id{"path"},
                .long_ = arg_id{"path"},
                .num_args = value_range::at_least(1)
            };
            return all_true(arg_display(pair) == "--point <point> <point>",
                            arg_display(many) == "--path <path>...");
        }

        static_assert(a_fixed_arity_repeats_its_placeholder());

        /**
         * Compile-time contract on `require_equals`, whose two spellings differ only in
         * whether the value is optional.
         */
        consteval bool require_equals_shows_the_equals_sign() {
            constexpr arg_spec strict{
                .id = arg_id{"color"},
                .long_ = arg_id{"color"},
                .num_args = value_range::single(),
                .settings = arg_flags{}.set(arg_setting::require_equals)
            };
            constexpr arg_spec loose{
                .id = arg_id{"color"},
                .long_ = arg_id{"color"},
                .num_args = value_range::optional(),
                .settings = arg_flags{}.set(arg_setting::require_equals)
            };
            return all_true(arg_display(strict) == "--color=<color>",
                            arg_display(loose) == "--color[=<color>]");
        }

        static_assert(require_equals_shows_the_equals_sign());

        /**
         * Compile-time contract on the parse-state machine. `holds_arg()` is what
         * decides whether a token is even considered as a subcommand name, so a state
         * that forgot to distinguish `values_done` would make `git add commit` treat
         * `commit` as a subcommand mid-argument.
         */
        consteval bool the_parse_state_remembers_what_is_open() {
            constexpr parse_state idle = parse_state::done();
            constexpr parse_state open = parse_state::option(arg_id{"port"});
            constexpr parse_state slot = parse_state::positional(arg_id{"file"});
            return all_true(!idle.holds_arg(),
                            open.holds_arg(),
                            slot.holds_arg(),
                            open.kind == parse_state_kind::opt,
                            slot.kind == parse_state_kind::pos,
                            open.id == "port",
                            slot.id == "file",
                            open != slot,
                            idle.id.empty());
        }

        static_assert(the_parse_state_remembers_what_is_open());

        /**
         * Compile-time contract: only an explicit *short* spelling asks for short help.
         * clap's ladder maps `Index` and `None` to the long form, which is easy to get
         * backwards and produces `-h` output for `--help`.
         */
        consteval bool only_a_short_spelling_asks_for_short_help() {
            return all_true(!prefers_long_form(arg_identifier::short_),
                            prefers_long_form(arg_identifier::long_),
                            prefers_long_form(arg_identifier::index),
                            prefers_long_form(std::nullopt));
        }

        static_assert(only_a_short_spelling_asks_for_short_help());

        /**
         * Compile-time contract on the multicall applet name, which is Rust's
         * `Path::file_stem` and therefore drops the directory *and* the extension.
         */
        consteval bool the_applet_name_is_a_file_stem() {
            return all_true(file_stem("/usr/bin/busybox") == "busybox",
                            file_stem("ls") == "ls",
                            file_stem("./bin/my_prog.exe") == "my_prog",
                            file_stem("C:\\tools\\hush.exe") == "hush",
                            file_stem(".hidden") == ".hidden",
                            file_stem("a.b.c") == "a.b");
        }

        static_assert(the_applet_name_is_a_file_stem());

        /**
         * Compile-time contract on the UTF-8 encoder used to quote an unknown short
         * flag. A one-byte-only implementation turns `-é` into a mangled diagnostic.
         */
        consteval bool an_unknown_short_flag_is_quoted_as_utf8() {
            std::string ascii;
            append_utf8(ascii, U'x');
            std::string two;
            append_utf8(two, U'\u00E9');
            std::string three;
            append_utf8(three, U'\u4E2D');
            std::string four;
            append_utf8(four, U'\U0001F600');
            return all_true(ascii == "x",
                            two.size() == 2,
                            three.size() == 3,
                            four.size() == 4,
                            two == "\xC3\xA9",
                            three == "\xE4\xB8\xAD",
                            four == "\xF0\x9F\x98\x80");
        }

        static_assert(an_unknown_short_flag_is_quoted_as_utf8());
    } // namespace detail
} // namespace clapp
