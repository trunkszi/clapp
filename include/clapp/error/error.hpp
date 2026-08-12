/**
 * \file
 * \brief clapp::error — the value every failed parse, and every --help, travels in.
 */

#pragma once

#include <clapp/error/context.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/output/textwrap.hpp>
#include <clapp/util/flat_map.hpp>
#include <clapp/util/str.hpp>

#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {

    /** \brief How much of an error to render. clap's `ErrorFormatter` implementations. */
    enum class error_format : std::uint8_t {
        /** RichFormatter: structured sentence, suggestions, usage, try-help. Default. */
        rich,
        /** KindFormatter: `error:` plus the kind's one-line description only. */
        kind_only,
    };

    namespace detail {

        /** \brief The two-space indent clap calls `output::TAB`. */
        inline constexpr std::string_view error_indent = "  ";

        /**
         * \brief Whether \p value must be quoted when listed as a possible value.
         * \param value The value to inspect.
         * \return `true` when empty or containing whitespace. clap's `Escape`.
         */
        [[nodiscard]] constexpr bool value_needs_escaping(std::string_view value) noexcept {
            if (value.empty()) return true;
            for (const char byte : value) {
                if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' || byte == '\f' ||
                    byte == '\r')
                    return true;
            }
            return false;
        }

        /**
         * \brief Append \p value to \p out, quoted and escaped when needed.
         * \param out Destination string.
         * \param value The value to write.
         * \note Escapes only quote, backslash, and line-breaking controls; other bytes
         *       pass through (os_str is byte-oriented). Intentional difference from
         *       clap's full Debug escapes.
         */
        constexpr void append_escaped_value(std::string& out, std::string_view value) {
            if (!value_needs_escaping(value)) {
                append_bytes(out, value);
                return;
            }
            out.push_back('"');
            for (const char byte : value) {
                switch (byte) {
                case '"':
                    append_bytes(out, "\\\"");
                    break;
                case '\\':
                    append_bytes(out, "\\\\");
                    break;
                case '\n':
                    append_bytes(out, "\\n");
                    break;
                case '\t':
                    append_bytes(out, "\\t");
                    break;
                case '\r':
                    append_bytes(out, "\\r");
                    break;
                default:
                    out.push_back(byte);
                    break;
                }
            }
            out.push_back('"');
        }

        /**
         * \brief clap's `singular_or_plural`: verb phrase for \p count values.
         * \param count How many values were provided.
         * \return `" were provided"` if count > 1, else `" was provided"`.
         */
        [[nodiscard]] constexpr std::string_view singular_or_plural(std::ptrdiff_t count) noexcept {
            return count > 1 ? " were provided" : " was provided";
        }

    }  // namespace detail

    /**
     * \brief A parse outcome that is not an `arg_matches`.
     *
     * Error half of every `std::expected` in clapp (ADR-0001). Carries error_kind,
     * a context map, and optionally a pre-rendered message (how `--help` travels).
     *
     * \code
     *     const clapp::error err = clapp::error::unknown_argument(
     *             clapp::cow_str::borrowed("--verbose"),
     *             std::nullopt, std::nullopt, false, std::nullopt);
     *     assert(err.kind() == clapp::error_kind::unknown_argument);
     *     assert(err.render().contains("unexpected argument '--verbose' found"));
     * \endcode
     *
     * \note Members are `constexpr` so rendering can be pinned with `static_assert`.
     *       The type itself cannot be a `constexpr` variable (context map allocates).
     */
    class error {
    public:
        /**
         * \brief The context map type. Ordered by clapp::context_kind.
         * \note clap's FlatMap is insertion-ordered; clapp::flat_map sorts by key.
         *       render() looks up by name, so order is not in the message.
         *       context_entries() documents kind order, not insertion order.
         */
        using context_map = flat_map<context_kind, context_value>;

        // -------------------------------------------------------------------
        // Construction
        // -------------------------------------------------------------------

        /**
         * \brief An error of \p what with no context and no message.
         * \param what What happened.
         */
        constexpr explicit error(error_kind what) noexcept : kind_(what) {}

        /**
         * \brief An error whose message is supplied as plain text. clap's `Error::raw`.
         *
         * Rendered with `error:`, usage, and try-help; structured context is ignored.
         *
         * \param what What happened.
         * \param message The sentence to print.
         */
        [[nodiscard]] static constexpr error raw(error_kind what, cow_str message) {
            error result{what};
            result.message_state_ = message_state::raw;
            result.raw_message_   = std::move(message);
            return result;
        }

        /**
         * \brief An already-rendered message that must not be decorated.
         *
         * clap's `Message::Formatted`. How help and version travel: render() returns
         * \p message unchanged, with no `error:` prefix.
         *
         * \param what What happened.
         * \param message The finished message.
         */
        [[nodiscard]] static constexpr error formatted(error_kind what, styled_str message) {
            error result{what};
            result.message_state_     = message_state::formatted;
            result.formatted_message_ = std::move(message);
            return result;
        }

        // -------------------------------------------------------------------
        // The four questions
        // -------------------------------------------------------------------

        /** \brief What happened, for programmatic handling. */
        [[nodiscard]] constexpr error_kind kind() const noexcept { return kind_; }

        /**
         * \brief Whether the message belongs on stderr.
         * \return `false` only for display_help and display_version.
         *         See the \warning on error_kind.hpp.
         */
        [[nodiscard]] constexpr bool use_stderr() const noexcept {
            return clapp::use_stderr(kind_);
        }

        /** \brief Process exit status: `0` when the message goes to stdout, else `2`. */
        [[nodiscard]] constexpr int exit_code() const noexcept { return exit_code_of(kind_); }

        /**
         * \brief The message as semantic spans.
         * \param which How much to render; see clapp::error_format.
         * \return A styled_str with no escape sequences. Terminal bytes are the output edge's job.
         *
         * \warning **No escape sequences is enforced, not assumed.** Messages quote
         *          text from `argv`, so a command line can inject `ESC`. clapp strips
         *          via clapp::strip_escapes() at the producer (clap uses anstream's
         *          StripStream when colour is off) so the guarantee belongs to the
         *          value and render_plain() stays an identity on content.
         *
         * \note A formatted message (help, version, formatted()) is returned unchanged
         *       regardless of \p which — decorating help with `error:` would be a bug.
         *
         * \note Generated messages always end with a newline (clap's try_help, including
         *       the bare `"\n"` fallback). formatted() messages end however output left them.
         *       clapp has no clap two-phase `Error::format(cmd)` step; the newline is
         *       unconditional for generated text.
         */
        [[nodiscard]] constexpr styled_str render(error_format which = error_format::rich) const {
            return strip_escapes(render_undecorated(which));
        }

    private:
        /** \brief render() before the escape strip. See render()'s \warning. */
        [[nodiscard]] constexpr styled_str render_undecorated(error_format which) const {
            if (message_state_ == message_state::formatted) return formatted_message_;

            styled_str out;
            write_error_prefix(out);

            if (message_state_ == message_state::raw) {
                out.push_plain(raw_message_.view());
                write_usage(out);
                write_try_help(out);
                return out;
            }

            if (which == error_format::kind_only) {
                write_fallback_body(out);
                out.push_plain("\n");
                return out;
            }

            if (!write_dynamic_context(out)) write_fallback_body(out);
            write_suggestions(out);
            write_usage(out);
            write_try_help(out);
            return out;
        }

    public:
        // -------------------------------------------------------------------
        // Structured context
        // -------------------------------------------------------------------

        /**
         * \brief Whether \p which is present.
         * \param which The piece of context to look for.
         */
        [[nodiscard]] constexpr bool has_context(context_kind which) const noexcept {
            return context_.contains(which);
        }

        /**
         * \brief The value stored for \p which, if any.
         * \param which The piece of context to fetch.
         * \return A copy, or `nullopt` when absent.
         *
         * \warning **The copy is a temporary, and three of context_value's accessors
         *          borrow from it.** as_string(), as_strings() and as_styled_list()
         *          return views into the object they are called on, so chaining them
         *          onto this function dangles when the full-expression ends:
         *          \code
         *          auto v = err.context(k)->as_string();      // DANGLES
         *          std::printf("%.*s", (int)v->size(), v->data());
         *          \endcode
         *          ASan `stack-use-after-scope`; neither GCC 16 nor clang-p2996 warns.
         *          Bind the copy first, or use context_ref():
         *          \code
         *          const auto held = err.context(k);          // safe: outlives the view
         *          const auto v    = held->as_string();
         *          const auto w    = err.context_ref(k).as_string();   // also safe
         *          \endcode
         *          Value-returning accessors (as_bool, as_number, as_styled, to_string)
         *          are safe to chain.
         */
        [[nodiscard]] constexpr std::optional<context_value> context(context_kind which) const {
            const auto found = context_.find(which);
            if (found == context_.end()) return std::nullopt;
            return found->second;
        }

        /**
         * \brief The value stored for \p which, without copying.
         * \param which The piece of context to fetch.
         * \return Reference to the stored value, or to a shared none when absent.
         *         Pair with has_context() when absent vs present-none must differ.
         *
         * \note A reference, not a pointer: `!= nullptr` fails to fold under
         *       `-fsanitize=null` (trap 10), and render() is reachable from static_assert.
         *
         * \warning **Invalidated by the next insert(), erase_context() or clear.**
         *          The map is a flat_map over a vector, so insert() can move every
         *          stored value and leave this reference — and any view from it —
         *          dangling. ASan `heap-use-after-free`. Named constructors perform
         *          three to five successive insert() calls. Read before mutating, or
         *          take a copy with context().
         */
        [[nodiscard]] constexpr const context_value&
        context_ref(context_kind which) const noexcept {
            const auto found = context_.find(which);
            if (found == context_.end()) return detail::absent_context_value;
            return found->second;
        }

        /**
         * \brief Every piece of context, in clapp::context_kind order.
         * \return Reference to the map (for kind_only renderers and test dumps).
         *
         * \warning Invalidated by the next insert() or erase_context() — same vector
         *          reallocation rule as context_ref().
         */
        [[nodiscard]] constexpr const context_map& context_entries() const noexcept {
            return context_;
        }

        /**
         * \brief Store \p value under \p which, replacing any previous value.
         * \param which The role the value plays.
         * \param value The value.
         * \return `*this`, for chaining.
         */
        constexpr error& insert(context_kind which, context_value value) & {
            context_.insert_or_assign(which, std::move(value));
            return *this;
        }

        /**
         * \brief Rvalue overload of insert(), for building an error in one expression.
         * \param which The role the value plays.
         * \param value The value.
         * \return The error, moved out.
         */
        [[nodiscard]] constexpr error insert(context_kind which, context_value value) && {
            insert(which, std::move(value));
            return std::move(*this);
        }

        /**
         * \brief Drop the value stored under \p which.
         * \param which The piece of context to remove.
         * \return `true` when something was removed.
         */
        constexpr bool erase_context(context_kind which) {
            return context_.erase(which).has_value();
        }

        // -------------------------------------------------------------------
        // Presentation inputs the parser fills in
        // -------------------------------------------------------------------

        /**
         * \brief Attach the underlying cause (clap's `Error::source`).
         *
         * Rendered after a value_validation sentence; whole message for io/format.
         *
         * \param reason One sentence. For conversions, pass parse_error::message()
         *        (static storage) via cow_str::borrowed().
         * \return `*this`, for chaining.
         */
        constexpr error& set_source(cow_str reason) & {
            source_         = std::move(reason);
            source_present_ = true;
            return *this;
        }

        /**
         * \brief Rvalue overload of set_source().
         * \param reason One sentence.
         * \return The error, moved out.
         */
        [[nodiscard]] constexpr error set_source(cow_str reason) && {
            set_source(std::move(reason));
            return std::move(*this);
        }

        /**
         * \brief Whether a cause was attached.
         * \note Explicit `bool`, not an empty-string sentinel: empty cause ≠ no cause.
         */
        [[nodiscard]] constexpr bool has_source() const noexcept { return source_present_; }

        /** \brief The attached cause, if any. */
        [[nodiscard]] constexpr std::optional<std::string_view> source() const noexcept {
            if (!source_present_) return std::nullopt;
            return source_.view();
        }

        /**
         * \brief Set the flag named in the closing "For more information" line.
         *
         * clap's `get_help_flag`: usually `--help`, or the user's help flag / subcommand.
         *
         * \param flag How the user would ask for help, e.g. `"--help"`.
         * \return `*this`, for chaining.
         */
        constexpr error& set_help_flag(cow_str flag) & {
            help_flag_         = std::move(flag);
            help_flag_present_ = true;
            return *this;
        }

        /**
         * \brief Rvalue overload of set_help_flag().
         * \param flag How the user would ask for help.
         * \return The error, moved out.
         */
        [[nodiscard]] constexpr error set_help_flag(cow_str flag) && {
            set_help_flag(std::move(flag));
            return std::move(*this);
        }

        /**
         * \brief Whether a help flag was set (and thus whether render() ends with try-help).
         */
        [[nodiscard]] constexpr bool has_help_flag() const noexcept { return help_flag_present_; }

        /** \brief The help flag, if one was set. */
        [[nodiscard]] constexpr std::optional<std::string_view> help_flag() const noexcept {
            if (!help_flag_present_) return std::nullopt;
            return help_flag_.view();
        }

        /** \brief Whether a message was supplied instead of being derived from context. */
        [[nodiscard]] constexpr bool has_message() const noexcept {
            return message_state_ != message_state::none;
        }

        /** \brief Equality by kind, context, message and presentation inputs. */
        [[nodiscard]] constexpr bool operator==(const error&) const = default;

        // -------------------------------------------------------------------
        // Named constructors — clap's `Error::*` factories
        // -------------------------------------------------------------------

        /**
         * \brief `--help` was used. clap's `Error::display_help`.
         * \param help The rendered help text.
         * \return display_help: stdout, exit 0.
         */
        [[nodiscard]] static constexpr error display_help(styled_str help) {
            return formatted(error_kind::display_help, std::move(help));
        }

        /**
         * \brief Nothing given and `arg_required_else_help` is set.
         * \param help The rendered help text.
         * \return display_help_on_missing_argument_or_subcommand: **stderr, exit 2**.
         */
        [[nodiscard]] static constexpr error display_help_error(styled_str help) {
            return formatted(error_kind::display_help_on_missing_argument_or_subcommand,
                             std::move(help));
        }

        /**
         * \brief `--version` was used. clap's `Error::display_version`.
         * \param version The rendered version text.
         * \return display_version: stdout, exit 0.
         */
        [[nodiscard]] static constexpr error display_version(styled_str version) {
            return formatted(error_kind::display_version, std::move(version));
        }

        /**
         * \brief Two arguments that cannot be used together. clap's `argument_conflict`.
         * \param arg The argument that triggered the conflict.
         * \param others Arguments already present that it collides with. Equal to
         *        \p arg renders as "cannot be used multiple times".
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error argument_conflict(cow_str arg,
                                                               std::vector<cow_str> others,
                                                               std::optional<styled_str> usage) {
            error result{error_kind::argument_conflict};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::prior_arg, collapse(std::move(others)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief A subcommand that conflicts with given arguments.
         * \param sub The subcommand.
         * \param others The arguments it collides with.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error subcommand_conflict(cow_str sub,
                                                                 std::vector<cow_str> others,
                                                                 std::optional<styled_str> usage) {
            error result{error_kind::argument_conflict};
            result.insert(context_kind::invalid_subcommand, context_value::string(std::move(sub)));
            result.insert(context_kind::prior_arg, collapse(std::move(others)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief An option that requires `=` was given a separate value.
         * \param arg The option.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error no_equals(cow_str arg,
                                                       std::optional<styled_str> usage) {
            error result{error_kind::no_equals};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief A value outside the argument's possible values.
         * \param arg The argument.
         * \param bad_value What the user wrote.
         * \param good_values Everything that would have been accepted.
         * \return invalid_value, with suggested_value when best_match finds one.
         *
         * \note The suggestion is *copied* (not borrowed from \p good_values):
         *       best_match returns a view into its input, which is then moved away.
         *
         * \note **No `usage` parameter.** clap's `Error::invalid_value` takes none;
         *       adding one would diverge. Contrast too_many/too_few/wrong_number.
         */
        [[nodiscard]] static constexpr error
        invalid_value(cow_str arg, cow_str bad_value, std::vector<cow_str> good_values) {
            const auto names =
                    good_values | std::views::transform([](const cow_str& v) { return v.view(); });
            const std::optional<std::string_view> suggestion = best_match(bad_value.view(), names);

            error result{error_kind::invalid_value};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::invalid_value, context_value::string(std::move(bad_value)));
            if (suggestion.has_value()) {
                result.insert(context_kind::suggested_value,
                              context_value::string(cow_str::owned(*suggestion)));
            }
            result.insert(context_kind::valid_value,
                          context_value::strings(std::move(good_values)));
            return result;
        }

        /**
         * \brief A required value was empty. invalid_value() with an empty value.
         * \param arg The argument.
         * \param good_values Everything that would have been accepted.
         * \note No `usage` parameter — same as invalid_value().
         */
        [[nodiscard]] static constexpr error empty_value(cow_str arg,
                                                         std::vector<cow_str> good_values) {
            return invalid_value(std::move(arg), cow_str::borrowed(""), std::move(good_values));
        }

        /**
         * \brief An unrecognized subcommand that resembles a real one.
         * \param subcommand What the user wrote.
         * \param did_you_mean Subcommands close enough to suggest.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error invalid_subcommand(cow_str subcommand,
                                                                std::vector<cow_str> did_you_mean,
                                                                std::optional<styled_str> usage) {
            error result{error_kind::invalid_subcommand};
            result.insert(context_kind::invalid_subcommand,
                          context_value::string(std::move(subcommand)));
            result.insert(context_kind::suggested_subcommand,
                          context_value::strings(std::move(did_you_mean)));
            result.insert(context_kind::suggested,
                          context_value::styled_list(std::vector<styled_str>{}));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief An unrecognized subcommand with nothing to suggest.
         * \param subcommand What the user wrote.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error
        unrecognized_subcommand(cow_str subcommand, std::optional<styled_str> usage) {
            error result{error_kind::invalid_subcommand};
            result.insert(context_kind::invalid_subcommand,
                          context_value::string(std::move(subcommand)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief Required arguments were not provided.
         * \param required The arguments, already rendered as the user would write them.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error
        missing_required_argument(std::vector<cow_str> required, std::optional<styled_str> usage) {
            error result{error_kind::missing_required_argument};
            result.insert(context_kind::invalid_arg, context_value::strings(std::move(required)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief A subcommand is required and none was given.
         * \param parent The command that wanted one.
         * \param available The subcommands it accepts.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error missing_subcommand(cow_str parent,
                                                                std::vector<cow_str> available,
                                                                std::optional<styled_str> usage) {
            error result{error_kind::missing_subcommand};
            result.insert(context_kind::invalid_subcommand,
                          context_value::string(std::move(parent)));
            result.insert(context_kind::valid_subcommand,
                          context_value::strings(std::move(available)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief One value too many.
         * \param arg The argument.
         * \param value The value that had no room.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error
        too_many_values(cow_str arg, cow_str value, std::optional<styled_str> usage) {
            error result{error_kind::too_many_values};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::invalid_value, context_value::string(std::move(value)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief Fewer values than the minimum.
         * \param arg The argument.
         * \param min_values The minimum `num_args` allows.
         * \param current_values How many were supplied.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error too_few_values(cow_str arg,
                                                            std::size_t min_values,
                                                            std::size_t current_values,
                                                            std::optional<styled_str> usage) {
            error result{error_kind::too_few_values};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::min_values,
                          context_value::number(static_cast<std::ptrdiff_t>(min_values)));
            result.insert(context_kind::actual_num_values,
                          context_value::number(static_cast<std::ptrdiff_t>(current_values)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief A value the argument's parser rejected.
         * \param arg The argument.
         * \param value What the user wrote.
         * \param reason Why rejected — pass parse_error::message() via cow_str::borrowed().
         *
         * \note Seam for parse_error: takes a string so this header need not include
         *       value_parser.hpp.
         * \note No `usage` parameter (clap's value_validation takes none). The external-
         *       subcommand UTF-8 path inserts context_kind::usage itself.
         */
        [[nodiscard]] static constexpr error
        value_validation(cow_str arg, cow_str value, cow_str reason) {
            error result{error_kind::value_validation};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::invalid_value, context_value::string(std::move(value)));
            result.set_source(std::move(reason));
            return result;
        }

        /**
         * \brief A value count `num_args` does not allow.
         * \param arg The argument.
         * \param expected_values How many were required.
         * \param current_values How many were supplied.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error
        wrong_number_of_values(cow_str arg,
                               std::size_t expected_values,
                               std::size_t current_values,
                               std::optional<styled_str> usage) {
            error result{error_kind::wrong_number_of_values};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::expected_num_values,
                          context_value::number(static_cast<std::ptrdiff_t>(expected_values)));
            result.insert(context_kind::actual_num_values,
                          context_value::number(static_cast<std::ptrdiff_t>(current_values)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief An argument the command does not define.
         * \param arg What the user wrote.
         * \param suggested_flag A defined flag close enough to suggest, if any.
         * \param suggested_subcommand Subcommand under which \p suggested_flag lives;
         *        when set, rendered as a `tip:` rather than suggested_arg.
         * \param trailing_arg Whether the token was plausibly a value (adds the
         *        "use '-- …'" tip).
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error
        unknown_argument(cow_str arg,
                         std::optional<cow_str> suggested_flag,
                         std::optional<cow_str> suggested_subcommand,
                         bool trailing_arg,
                         std::optional<styled_str> usage) {
            std::vector<styled_str> tips;
            if (trailing_arg) {
                styled_str tip;
                tip.push_plain("to pass '")
                        .push(style_class::invalid, arg.view())
                        .push_plain("' as a value, use '")
                        .push(style_class::valid, "-- ")
                        .push(style_class::valid, arg.view())
                        .push_plain("'");
                tips.push_back(std::move(tip));
            }

            error result{error_kind::unknown_argument};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.attach_usage(std::move(usage));

            if (suggested_flag.has_value()) {
                if (suggested_subcommand.has_value()) {
                    styled_str tip;
                    tip.push_plain("'")
                            .push(style_class::valid, suggested_subcommand->view())
                            .push(style_class::valid, " ")
                            .push(style_class::valid, suggested_flag->view())
                            .push_plain("' exists");
                    tips.push_back(std::move(tip));
                } else {
                    result.insert(context_kind::suggested_arg,
                                  context_value::string(std::move(*suggested_flag)));
                }
            }
            if (!tips.empty()) {
                result.insert(context_kind::suggested, context_value::styled_list(std::move(tips)));
            }
            return result;
        }

        /**
         * \brief A subcommand written after `--`.
         * \param arg The token, which names a real subcommand.
         * \param usage The usage line, when available.
         */
        [[nodiscard]] static constexpr error
        unnecessary_double_dash(cow_str arg, std::optional<styled_str> usage) {
            styled_str tip;
            tip.push_plain("subcommand '")
                    .push(style_class::valid, arg.view())
                    .push_plain("' exists; to use it, remove the '")
                    .push(style_class::invalid, "--")
                    .push_plain("' before it");

            std::vector<styled_str> tips;
            tips.push_back(std::move(tip));

            error result{error_kind::unknown_argument};
            result.insert(context_kind::invalid_arg, context_value::string(std::move(arg)));
            result.insert(context_kind::suggested, context_value::styled_list(std::move(tips)));
            result.attach_usage(std::move(usage));
            return result;
        }

        /**
         * \brief An I/O failure. clap's `From<io::Error>`.
         * \param message The system's description.
         */
        [[nodiscard]] static constexpr error io(cow_str message) {
            return error{error_kind::io}.set_source(std::move(message));
        }

        /**
         * \brief A formatting failure. clap's `From<fmt::Error>`.
         * \param message The description.
         */
        [[nodiscard]] static constexpr error format(cow_str message) {
            return error{error_kind::format}.set_source(std::move(message));
        }

    private:
        /**
         * Whether, and how, a message was supplied instead of derived from context.
         * Enum rather than two optionals: states are mutually exclusive.
         */
        enum class message_state : std::uint8_t { none, raw, formatted };

        /** clap's `match others.len()`: no prior arguments, one, or several. */
        [[nodiscard]] static constexpr context_value collapse(std::vector<cow_str> others) {
            if (others.empty()) return context_value::none();
            if (others.size() == 1) return context_value::string(std::move(others.front()));
            return context_value::strings(std::move(others));
        }

        /** Store \p usage under context_kind::usage when there is one. */
        constexpr void attach_usage(std::optional<styled_str> usage) {
            if (!usage.has_value()) return;
            insert(context_kind::usage, context_value::styled(std::move(*usage)));
        }

        // Renderer: plain members (complete-class context). Do not turn into templates
        // without placing definitions above any mem-initializer that reaches them
        // (clang member-template order workaround).

        static constexpr void write_error_prefix(styled_str& out) {
            out.push(style_class::error, "error:").push_plain(" ");
        }

        constexpr void write_fallback_body(styled_str& out) const {
            if (const std::optional<std::string_view> sentence = describe(kind_)) {
                out.push_plain(*sentence);
                return;
            }
            if (source_present_) {
                out.push_plain(source_.view());
                return;
            }
            out.push_plain("unknown cause");
        }

        /** clap's `write_values_list`: `\n  [possible values: a, b]`. */
        constexpr void
        write_values_list(styled_str& out, std::string_view list_name, context_kind which) const {
            const context_value& values = context_ref(which);
            if (values.kind() != context_value_kind::strings) return;
            const std::span<const cow_str> items = values.as_strings();
            if (items.empty()) return;

            out.push_plain("\n").push_plain(detail::error_indent).push_plain("[");
            out.push_plain(list_name);
            out.push_plain(": ");
            bool first = true;
            for (const cow_str& item : items) {
                if (!first) out.push_plain(", ");
                first = false;
                std::string escaped;
                detail::append_escaped_value(escaped, item.view());
                out.push(style_class::valid, escaped);
            }
            out.push_plain("]");
        }

        /** clap's `did_you_mean`: `  tip: a similar argument exists: '--verbose'`. */
        static constexpr void write_did_you_mean(styled_str& out,
                                                 std::string_view noun,
                                                 const context_value& possibles) {
            out.push_plain(detail::error_indent).push(style_class::valid, "tip:");
            if (const std::optional<std::string_view> single = possibles.as_string()) {
                out.push_plain(" a similar ");
                out.push_plain(noun);
                out.push_plain(" exists: '");
                out.push(style_class::valid, *single);
                out.push_plain("'");
                return;
            }
            if (possibles.kind() != context_value_kind::strings) return;
            const std::span<const cow_str> items = possibles.as_strings();
            if (items.size() == 1) {
                out.push_plain(" a similar ");
                out.push_plain(noun);
                out.push_plain(" exists: ");
            } else {
                out.push_plain(" some similar ");
                out.push_plain(noun);
                out.push_plain("s exist: ");
            }
            bool first = true;
            for (const cow_str& item : items) {
                if (!first) out.push_plain(", ");
                first = false;
                out.push_plain("'");
                out.push(style_class::valid, item.view());
                out.push_plain("'");
            }
        }

        constexpr void write_suggestions(styled_str& out) const {
            bool opened                                             = false;
            constexpr std::pair<context_kind, std::string_view> nouns[] = {
                    {context_kind::suggested_subcommand, "subcommand"},
                    {context_kind::suggested_arg, "argument"},
                    {context_kind::suggested_value, "value"},
            };
            for (const auto& [which, noun] : nouns) {
                if (!has_context(which)) continue;
                out.push_plain("\n");
                if (!opened) {
                    out.push_plain("\n");
                    opened = true;
                }
                write_did_you_mean(out, noun, context_ref(which));
            }

            const context_value& tips = context_ref(context_kind::suggested);
            if (tips.kind() != context_value_kind::styled_list) return;
            const std::span<const styled_str> lines = tips.as_styled_list();
            if (!opened && !lines.empty()) out.push_plain("\n");
            for (const styled_str& line : lines) {
                out.push_plain("\n");
                out.push_plain(detail::error_indent);
                out.push(style_class::valid, "tip:");
                out.push_plain(" ");
                out.append(line);
            }
        }

        constexpr void write_usage(styled_str& out) const {
            const context_value& usage = context_ref(context_kind::usage);
            if (usage.kind() != context_value_kind::styled) return;
            out.push_plain("\n\n");
            out.append(*usage.as_styled());
        }

        constexpr void write_try_help(styled_str& out) const {
            if (!help_flag_present_) {
                out.push_plain("\n");
                return;
            }
            out.push_plain("\n\nFor more information, try '");
            out.push(style_class::literal, help_flag_.view());
            out.push_plain("'.\n");
        }

        /**
         * clap's `write_dynamic_context`.
         * \return `true` when context produced a sentence (skip the generic kind description).
         */
        constexpr bool write_dynamic_context(styled_str& out) const {
            switch (kind_) {
            case error_kind::argument_conflict:
                return write_argument_conflict(out);
            case error_kind::no_equals: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                if (!arg.has_value()) return false;
                out.push_plain("equal sign is needed when assigning values to '");
                out.push(style_class::invalid, *arg);
                out.push_plain("'");
                return true;
            }
            case error_kind::invalid_value: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                const std::optional<std::string_view> value =
                        context_ref(context_kind::invalid_value).as_string();
                if (!arg.has_value() || !value.has_value()) return false;
                if (value->empty()) {
                    out.push_plain("a value is required for '");
                    out.push(style_class::invalid, *arg);
                    out.push_plain("' but none was supplied");
                } else {
                    out.push_plain("invalid value '");
                    out.push(style_class::invalid, *value);
                    out.push_plain("' for '");
                    out.push(style_class::literal, *arg);
                    out.push_plain("'");
                }
                write_values_list(out, "possible values", context_kind::valid_value);
                return true;
            }
            case error_kind::invalid_subcommand: {
                const std::optional<std::string_view> sub =
                        context_ref(context_kind::invalid_subcommand).as_string();
                if (!sub.has_value()) return false;
                out.push_plain("unrecognized subcommand '");
                out.push(style_class::invalid, *sub);
                out.push_plain("'");
                return true;
            }
            case error_kind::missing_required_argument: {
                const context_value& required = context_ref(context_kind::invalid_arg);
                if (required.kind() != context_value_kind::strings) return false;
                out.push_plain("the following required arguments were not provided:");
                for (const cow_str& item : required.as_strings()) {
                    out.push_plain("\n");
                    out.push_plain(detail::error_indent);
                    out.push(style_class::valid, item.view());
                }
                return true;
            }
            case error_kind::missing_subcommand: {
                const std::optional<std::string_view> parent =
                        context_ref(context_kind::invalid_subcommand).as_string();
                if (!parent.has_value()) return false;
                out.push_plain("'");
                out.push(style_class::invalid, *parent);
                out.push_plain("' requires a subcommand but one was not provided");
                write_values_list(out, "subcommands", context_kind::valid_subcommand);
                return true;
            }
            case error_kind::too_many_values: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                const std::optional<std::string_view> value =
                        context_ref(context_kind::invalid_value).as_string();
                if (!arg.has_value() || !value.has_value()) return false;
                out.push_plain("unexpected value '");
                out.push(style_class::invalid, *value);
                out.push_plain("' for '");
                out.push(style_class::literal, *arg);
                out.push_plain("' found; no more were expected");
                return true;
            }
            case error_kind::too_few_values: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                const std::optional<std::ptrdiff_t> actual =
                        context_ref(context_kind::actual_num_values).as_number();
                const std::optional<std::ptrdiff_t> minimum =
                        context_ref(context_kind::min_values).as_number();
                if (!arg.has_value() || !actual.has_value() || !minimum.has_value()) return false;
                out.push_decimal(style_class::valid, *minimum);
                out.push_plain(" values required by '");
                out.push(style_class::literal, *arg);
                out.push_plain("'; only ");
                out.push_decimal(style_class::invalid, *actual);
                out.push_plain(detail::singular_or_plural(*actual));
                return true;
            }
            case error_kind::value_validation: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                const std::optional<std::string_view> value =
                        context_ref(context_kind::invalid_value).as_string();
                if (!arg.has_value() || !value.has_value()) return false;
                out.push_plain("invalid value '");
                out.push(style_class::invalid, *value);
                out.push_plain("' for '");
                out.push(style_class::literal, *arg);
                out.push_plain("'");
                if (source_present_) {
                    out.push_plain(": ");
                    out.push_plain(source_.view());
                }
                return true;
            }
            case error_kind::wrong_number_of_values: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                const std::optional<std::ptrdiff_t> actual =
                        context_ref(context_kind::actual_num_values).as_number();
                const std::optional<std::ptrdiff_t> expected =
                        context_ref(context_kind::expected_num_values).as_number();
                if (!arg.has_value() || !actual.has_value() || !expected.has_value()) return false;
                out.push_decimal(style_class::valid, *expected);
                out.push_plain(" values required for '");
                out.push(style_class::literal, *arg);
                out.push_plain("' but ");
                out.push_decimal(style_class::invalid, *actual);
                out.push_plain(detail::singular_or_plural(*actual));
                return true;
            }
            case error_kind::unknown_argument: {
                const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string();
                if (!arg.has_value()) return false;
                out.push_plain("unexpected argument '");
                out.push(style_class::invalid, *arg);
                out.push_plain("' found");
                return true;
            }
            case error_kind::display_help:
            case error_kind::display_help_on_missing_argument_or_subcommand:
            case error_kind::display_version:
            case error_kind::io:
            case error_kind::format:
                return false;
            }
            return false;
        }

        constexpr bool write_argument_conflict(styled_str& out) const {
            bool show_prior            = has_context(context_kind::prior_arg);
            const context_value& prior = context_ref(context_kind::prior_arg);

            if (const std::optional<std::string_view> arg =
                        context_ref(context_kind::invalid_arg).as_string()) {
                const std::optional<std::string_view> prior_single = prior.as_string();
                if (prior_single.has_value() && *prior_single == *arg) {
                    show_prior = false;
                    out.push_plain("the argument '");
                    out.push(style_class::invalid, *arg);
                    out.push_plain("' cannot be used multiple times");
                } else {
                    out.push_plain("the argument '");
                    out.push(style_class::invalid, *arg);
                    out.push_plain("' cannot be used with");
                }
            } else if (const std::optional<std::string_view> sub =
                               context_ref(context_kind::invalid_subcommand).as_string()) {
                out.push_plain("the subcommand '");
                out.push(style_class::invalid, *sub);
                out.push_plain("' cannot be used with");
            } else {
                out.push_plain(*describe(error_kind::argument_conflict));
            }

            if (!show_prior) return true;
            switch (prior.kind()) {
            case context_value_kind::strings:
                out.push_plain(":");
                for (const cow_str& item : prior.as_strings()) {
                    out.push_plain("\n");
                    out.push_plain(detail::error_indent);
                    out.push(style_class::invalid, item.view());
                }
                break;
            case context_value_kind::string:
                out.push_plain(" '");
                out.push(style_class::invalid, *prior.as_string());
                out.push_plain("'");
                break;
            default:
                out.push_plain(" one or more of the other specified arguments");
                break;
            }
            return true;
        }

        error_kind kind_             = error_kind::unknown_argument;
        message_state message_state_ = message_state::none;
        // Explicit presence flags (trap 10): empty cause ≠ no cause.
        bool source_present_    = false;
        bool help_flag_present_ = false;
        context_map context_{};
        cow_str raw_message_{};
        styled_str formatted_message_{};
        cow_str source_{};
        cow_str help_flag_{};
    };

    namespace detail {

        static_assert(error{error_kind::display_help}.exit_code() == 0);
        static_assert(!error{error_kind::display_help}.use_stderr());
        static_assert(error{error_kind::display_version}.exit_code() == 0);
        static_assert(
                error{error_kind::display_help_on_missing_argument_or_subcommand}.exit_code() == 2);
        static_assert(
                error{error_kind::display_help_on_missing_argument_or_subcommand}.use_stderr());
        static_assert(error{error_kind::unknown_argument}.exit_code() == 2);

        /** \brief Verify that display-help payloads bypass diagnostic decoration. */
        consteval bool display_help_renders_verbatim() {
            const styled_str help{style_class::header, "Usage: demo [OPTIONS]"};
            const error err = error::display_help(help);
            return err.render() == help && err.render().span_count() == 1;
        }

        static_assert(display_help_renders_verbatim());

        /** \brief Verify that an unknown-argument diagnostic uses available context. */
        consteval bool unknown_argument_uses_context_when_it_has_it() {
            error with_context{error_kind::unknown_argument};
            with_context.insert(context_kind::invalid_arg,
                                context_value::string(cow_str::borrowed("--verbose")));
            const styled_str rendered = with_context.render();

            const error bare          = error{error_kind::unknown_argument};
            const styled_str fallback = bare.render();

            return rendered.contains("unexpected argument '--verbose' found") &&
                   rendered.text_of(style_class::invalid) == std::string_view{"--verbose"} &&
                   fallback.contains("unexpected argument found") &&
                   !fallback.contains("--verbose");
        }

        static_assert(unknown_argument_uses_context_when_it_has_it());

    }  // namespace detail

}  // namespace clapp
