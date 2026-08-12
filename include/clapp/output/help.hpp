/**
 * \file
 * \brief clapp::render_help() / render_version() — help screen and template engine.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/output/render.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/output/textwrap.hpp>
#include <clapp/output/usage.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {

    // =======================================================================
    // The request
    // =======================================================================

    /**
     * \brief Help-screen request: long/short, usage path, detected width.
     *
     * \code
     *     clapp::render_help(spec, {.use_long = true, .usage_name = "git clone"});
     * \endcode
     */
    struct help_style {
        /**
         * \brief true = `--help` (long about/help, next-line descs, PV list); false = `-h`.
         */
        bool use_long = false;

        /**
         * \brief What Usage: calls this command (full path from root); empty asks cmd.
         *
         * \warning **Borrowed view; must outlive the call.** Dangling yields garbage.
         *          Frozen command_spec has no path (ADR-0005); same rule as render_usage.
         */
        std::string_view usage_name{};

        /**
         * \brief Detected terminal width; nullopt = no TTY → default_terminal_width (100).
         * \note Input not probe so render_help stays constexpr; for_terminal fills it in.
         *       resolve_wrap_width still applies term_width / max_term_width.
         */
        std::optional<std::size_t> detected_width{};

        /** \brief The `-h` request for \p usage_name. */
        [[nodiscard]] static constexpr help_style
        short_form(std::string_view usage_name               = {},
                   std::optional<std::size_t> detected_width = {}) {
            return {.use_long = false, .usage_name = usage_name, .detected_width = detected_width};
        }

        /** \brief The `--help` request for \p usage_name. */
        [[nodiscard]] static constexpr help_style
        long_form(std::string_view usage_name               = {},
                  std::optional<std::size_t> detected_width = {}) {
            return {.use_long = true, .usage_name = usage_name, .detected_width = detected_width};
        }

        /** \brief Compare form, usage name, and detected terminal width. */
        [[nodiscard]] constexpr bool operator==(const help_style&) const noexcept = default;
    };

    namespace detail {

        /** \brief Indent every argument row starts with (clap TAB). */
        inline constexpr std::string_view help_tab = "  ";

        /** \brief Width of help_tab in cells. */
        inline constexpr std::size_t help_tab_width = 2;

        /** \brief Extra indent when a description starts on its own line. */
        inline constexpr std::string_view help_next_line_indent = "        ";

        /**
         * \brief Cells reserved for a `-s, ` prefix (comma and space included).
         * \note Charged for any long option so `      --parent` lines up under `-h, --help`.
         */
        inline constexpr std::size_t help_short_size = 4;

        /** \brief Default heading over the subcommand list. */
        inline constexpr std::string_view default_subcommand_heading = "Commands";

        /** \brief Heading over positional arguments. */
        inline constexpr std::string_view positional_heading = "Arguments";

        /** \brief Heading over named arguments. */
        inline constexpr std::string_view option_heading = "Options";

        /**
         * \brief Default help template when the command has anything to list.
         * \note No trailing spaces after `{after-help}` (Rust line-continuation artefact).
         */
        inline constexpr std::string_view default_help_template =
                "{before-help}{about-with-newline}\n{usage-heading} {usage}\n\n{all-args}"
                "{after-help}";

        /** \brief Default template for a command with nothing to list. */
        inline constexpr std::string_view default_no_args_help_template =
                "{before-help}{about-with-newline}\n{usage-heading} {usage}{after-help}";

        // ===================================================================
        // Small pure helpers
        // ===================================================================

        /**
         * \brief env_lookup that reports every variable unset (`[env: VAR=]`).
         */
        struct no_env {
            /** \brief Report the requested environment variable as unset. */
            [[nodiscard]] constexpr std::optional<std::string_view>
            operator()(std::string_view) const noexcept {
                return std::nullopt;
            }
        };

        static_assert(env_lookup<no_env>);

        /**
         * \brief Offset of first \p byte in \p text at or after \p from.
         * \param text Haystack.
         * \param byte Needle.
         * \param from Start offset.
         * \return Offset, or npos.
         *
         * \warning **Do not replace with `string_view::find`.** libstdc++ `__str_find`
         *          tests the haystack pointer; GCC 16.1.0 under `-fsanitize=null` will
         *          not fold that in a constant expression (trap 10). This path is
         *          reached from static_assert; one find() takes ubsan out.
         */
        [[nodiscard]] constexpr std::size_t
        find_byte(std::string_view text, char byte, std::size_t from = 0) noexcept {
            for (std::size_t i = from; i < text.size(); ++i) {
                if (text[i] == byte) return i;
            }
            return std::string_view::npos;
        }

        /**
         * \brief Expand `{n}` to newline (help-in-attribute escape).
         * \param text Prose to expand.
         * \return Fresh string.
         */
        [[nodiscard]] constexpr std::string replace_newline_var(std::string_view text) {
            std::string out;
            for (std::size_t i = 0; i < text.size(); ++i) {
                // Spelled out rather than `text.substr(i).starts_with("{n}")` for the
                // reason find_byte() gives.
                if (i + 2 < text.size() && text[i] == '{' && text[i + 1] == 'n' &&
                    text[i + 2] == '}') {
                    out.push_back('\n');
                    i += 2;
                    continue;
                }
                out.push_back(text[i]);
            }
            return out;
        }

        /**
         * \brief Whether \p text needs quoting in `[default: …]` / `[possible values: …]`.
         * \note Empty or any of six ASCII spaces; matches possible_value::needs_quoting.
         */
        [[nodiscard]] constexpr bool help_value_needs_quoting(std::string_view text) noexcept {
            if (text.empty()) return true;
            return std::ranges::any_of(text, [](char byte) noexcept {
                return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' ||
                       byte == '\f' || byte == '\v';
            });
        }

        /**
         * \brief Quote/escape \p text for default/PV annotations (`"`, `\`, `\n`/`\t`/`\r`).
         * \param text Value to spell.
         * \return Unchanged when no quoting needed; else double-quoted with escapes.
         * \note Only three controls; Rust Debug also uses `\u{..}` (差异清单).
         */
        [[nodiscard]] constexpr std::string escape_help_value(std::string_view text) {
            std::string out;
            if (!help_value_needs_quoting(text)) {
                append_bytes(out, text);
                return out;
            }
            out.push_back('"');
            for (const char byte : text) {
                switch (byte) {
                case '"':
                    out.push_back('\\');
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    out.push_back('\\');
                    break;
                case '\n':
                    out.push_back('\\');
                    out.push_back('n');
                    break;
                case '\t':
                    out.push_back('\\');
                    out.push_back('t');
                    break;
                case '\r':
                    out.push_back('\\');
                    out.push_back('r');
                    break;
                default:
                    out.push_back(byte);
                    break;
                }
            }
            out.push_back('"');
            return out;
        }

        /**
         * \brief Section heading for \p arg as the renderer sees it.
         * \param arg Argument to place.
         * \return Heading, or nullopt for the default section.
         *
         * \warning **Not `get_help_heading().has_value()`.** Opting out of next_help_heading
         *          yields `optional{""}` here (clap: None); both mean default section.
         *          Using the accessor alone puts the arg under a heading spelled `:`.
         */
        [[nodiscard]] constexpr std::optional<std::string_view>
        effective_help_heading(const arg_spec& arg) noexcept {
            const std::optional<std::string_view> heading = arg.get_help_heading();
            if (!heading.has_value() || heading->empty()) return std::nullopt;
            return heading;
        }

        /**
         * \brief Whether \p arg appears on this help screen.
         * \param use_long Whether `--help` asked.
         * \param arg Argument.
         * \return Whether to list it.
         * \note hide() wins; next_line_help overrides hide_long/short (clap).
         */
        [[nodiscard]] constexpr bool should_show_arg(bool use_long, const arg_spec& arg) noexcept {
            if (arg.is_hide_set()) return false;
            return (!arg.is_hide_long_help_set() && use_long) ||
                   (!arg.is_hide_short_help_set() && !use_long) || arg.is_next_line_help_set();
        }

        /**
         * \brief Whether \p sub appears in a parent's help. clap's
         *        `should_show_subcommand`.
         */
        [[nodiscard]] constexpr bool should_show_subcommand(const command_spec& sub) noexcept {
            return !sub.is_hide_set();
        }

        /**
         * \brief Sort key string for named options (with display_order).
         * \param arg Argument to key.
         * \return Key: short (+0/1 for case), else long, else `'{'` + id.
         */
        [[nodiscard]] constexpr std::string option_sort_key(const arg_spec& arg) {
            std::string key;
            if (const std::optional<char> short_name = arg.get_short(); short_name.has_value()) {
                key.push_back(to_lower(*short_name));
                key.push_back(is_ascii_lower(*short_name) ? '0' : '1');
                return key;
            }
            if (const std::optional<std::string_view> long_name = arg.get_long();
                long_name.has_value()) {
                append_bytes(key, *long_name);
                return key;
            }
            key.push_back('{');
            append_bytes(key, arg.get_id().name());
            return key;
        }

        /** \brief clap's `positional_sort_key`: the declared index, or 0. */
        [[nodiscard]] constexpr std::size_t positional_sort_key(const arg_spec& arg) noexcept {
            return arg.get_index().value_or(0);
        }

        /**
         * \brief Drop the first line of \p text when it is blank (for absent about).
         * \param text Assembled page.
         * \return Text without a leading all-whitespace line (exactly one line, once).
         */
        [[nodiscard]] constexpr styled_str trim_start_lines(const styled_str& text) {
            std::size_t scanned = 0;
            std::size_t cut     = 0;
            bool found          = false;
            bool only_space     = true;
            for (const styled_span& fragment : text.spans()) {
                for (const char byte : fragment.text) {
                    if (byte == '\n') {
                        cut   = scanned + 1;
                        found = true;
                        break;
                    }
                    if (!is_ascii_space(byte)) only_space = false;
                    ++scanned;
                }
                if (found) break;
            }
            if (!found || !only_space) return text;

            styled_str out;
            std::size_t position = 0;
            for (const styled_span& fragment : text.spans()) {
                const std::string_view body{fragment.text};
                if (position + body.size() <= cut) {
                    position += body.size();
                    continue;
                }
                const std::size_t begin = cut > position ? cut - position : 0;
                out.push(fragment.class_, body.substr(begin));
                position += body.size();
            }
            return out;
        }

        // ===================================================================
        // The renderer
        // ===================================================================

        /**
         * \brief Help template engine (internal; use clapp::render_help).
         * \tparam Lookup env_lookup for `[env: VAR=value]`.
         * \note Layout methods take the described entity as a parameter (not #cmd_) so
         *       one renderer can flatten a subcommand tree.
         */
        template<env_lookup Lookup>
        class help_renderer {
        public:
            /**
             * \brief Renderer for \p cmd.
             * \param cmd Command; must outlive the renderer.
             * \param style Request (see help_style::usage_name \warning).
             * \param lookup Environment reader; must outlive the renderer.
             */
            constexpr help_renderer(const command_spec& cmd,
                                    const help_style& style,
                                    const Lookup& lookup)
                : cmd_(&cmd),
                  lookup_(&lookup),
                  usage_name_(style.usage_name),
                  term_w_(resolve_wrap_width(
                          cmd.get_term_width(), cmd.get_max_term_width(), style.detected_width)),
                  use_long_(style.use_long),
                  next_line_help_(cmd.is_next_line_help_set()) {}

            /**
             * \brief Whole page (override_help → template → auto; trim; one trailing newline).
             * \return Rendered help ending in exactly one newline.
             */
            [[nodiscard]] constexpr styled_str render() {
                if (const std::optional<std::string_view> override_help = cmd_->get_override_help();
                    override_help.has_value()) {
                    out_.push_plain(*override_help);
                } else if (const std::optional<std::string_view> tmpl = cmd_->get_help_template();
                           tmpl.has_value()) {
                    write_templated_help(*tmpl);
                } else {
                    write_auto_help();
                }
                out_ = trim_start_lines(out_);
                out_ = clapp::trim_end(out_);
                out_.push_plain("\n");
                return std::move(out_);
            }

            /**
             * \brief Expand \p tmpl placeholders.
             * \param tmpl Template (see command_builder::help_template).
             * \note Unmatched `{` swallows the segment; unknown tags re-emit with braces.
             */
            constexpr void write_templated_help(std::string_view tmpl) {
                std::size_t start = 0;
                bool first_chunk  = true;
                while (true) {
                    const std::size_t brace      = find_byte(tmpl, '{', start);
                    const std::string_view chunk = brace == std::string_view::npos
                                                           ? tmpl.substr(start)
                                                           : tmpl.substr(start, brace - start);
                    if (first_chunk) {
                        out_.push_plain(chunk);
                        first_chunk = false;
                    } else if (const std::size_t close = find_byte(chunk, '}');
                               close != std::string_view::npos) {
                        expand_tag(chunk.substr(0, close));
                        out_.push_plain(chunk.substr(close + 1));
                    }
                    if (brace == std::string_view::npos) break;
                    start = brace + 1;
                }
            }

        private:
            // -- template tags ----------------------------------------------

            /** \brief Pick default vs no-args template and expand. */
            constexpr void write_auto_help() {
                bool positionals = false;
                bool named       = false;
                for (const arg_spec& one : cmd_->get_arguments()) {
                    if (!should_show_arg(use_long_, one)) continue;
                    if (one.is_positional()) {
                        positionals = true;
                    } else {
                        named = true;
                    }
                }
                const bool subcommands = cmd_->has_visible_subcommands();
                write_templated_help(positionals || named || subcommands
                                             ? default_help_template
                                             : default_no_args_help_template);
            }

            /** \brief Expand one template tag; unknown tags round-trip with braces. */
            constexpr void expand_tag(std::string_view tag) {
                if (tag == "name") {
                    write_display_name();
                } else if (tag == "bin") {
                    write_bin_name();
                } else if (tag == "version") {
                    write_version();
                } else if (tag == "author") {
                    write_author(false, false);
                } else if (tag == "author-with-newline") {
                    write_author(false, true);
                } else if (tag == "author-section") {
                    write_author(true, true);
                } else if (tag == "about") {
                    write_about(false, false);
                } else if (tag == "about-with-newline") {
                    write_about(false, true);
                } else if (tag == "about-section") {
                    write_about(true, true);
                } else if (tag == "usage-heading") {
                    out_.push(style_class::usage, "Usage:");
                } else if (tag == "usage") {
                    write_usage_body();
                } else if (tag == "all-args") {
                    write_all_args();
                } else if (tag == "options") {
                    write_args(collect_args(false), false);
                } else if (tag == "positionals") {
                    write_args(collect_args(true), true);
                } else if (tag == "subcommands") {
                    write_subcommands(*cmd_);
                } else if (tag == "tab") {
                    out_.push_plain(help_tab);
                } else if (tag == "after-help") {
                    write_after_help();
                } else if (tag == "before-help") {
                    write_before_help();
                } else {
                    std::string literal;
                    literal.push_back('{');
                    append_bytes(literal, tag);
                    literal.push_back('}');
                    out_.push_plain(literal);
                }
            }

            constexpr void write_display_name() {
                out_.push_plain(clapp::wrap(
                        replace_newline_var(cmd_->get_display_name().value_or(cmd_->get_name())),
                        term_w_));
            }

            /** \brief `{bin}`: multi-word bin_name becomes hyphenated (`git-mv`). */
            constexpr void write_bin_name() {
                if (const std::optional<std::string_view> bin = cmd_->get_bin_name();
                    bin.has_value() && find_byte(*bin, ' ') != std::string_view::npos) {
                    std::string hyphenated;
                    for (const char byte : *bin) hyphenated.push_back(byte == ' ' ? '-' : byte);
                    out_.push_plain(hyphenated);
                    return;
                }
                out_.push_plain(clapp::wrap(replace_newline_var(cmd_->get_name()), term_w_));
            }

            /** \brief `{version}`: prefers short version either way, then long. */
            constexpr void write_version() {
                const std::optional<std::string_view> text = cmd_->get_version().has_value()
                                                                     ? cmd_->get_version()
                                                                     : cmd_->get_long_version();
                if (!text.has_value()) return;
                out_.push_plain(clapp::wrap(*text, term_w_));
            }

            constexpr void write_author(bool before_newline, bool after_newline) {
                const std::optional<std::string_view> author = cmd_->get_author();
                if (!author.has_value()) return;
                if (before_newline) out_.push_plain("\n");
                out_.push_plain(clapp::wrap(*author, term_w_));
                if (after_newline) out_.push_plain("\n");
            }

            /**
             * \brief Write about text.
             * \note Short help uses about only (no long_about fallback); long falls back.
             */
            constexpr void write_about(bool before_newline, bool after_newline) {
                const std::optional<std::string_view> about =
                        use_long_ ? (cmd_->get_long_about().has_value() ? cmd_->get_long_about()
                                                                        : cmd_->get_about())
                                  : cmd_->get_about();
                if (!about.has_value()) return;
                if (before_newline) out_.push_plain("\n");
                out_.push_plain(clapp::wrap(replace_newline_var(*about), term_w_));
                if (after_newline) out_.push_plain("\n");
            }

            constexpr void write_before_help() {
                const std::optional<std::string_view> text =
                        use_long_ ? (cmd_->get_before_long_help().has_value()
                                             ? cmd_->get_before_long_help()
                                             : cmd_->get_before_help())
                                  : cmd_->get_before_help();
                if (!text.has_value()) return;
                out_.push_plain(clapp::wrap(replace_newline_var(*text), term_w_));
                out_.push_plain("\n\n");
            }

            constexpr void write_after_help() {
                const std::optional<std::string_view> text =
                        use_long_ ? (cmd_->get_after_long_help().has_value()
                                             ? cmd_->get_after_long_help()
                                             : cmd_->get_after_help())
                                  : cmd_->get_after_help();
                if (!text.has_value()) return;
                out_.push_plain("\n\n");
                out_.push_plain(clapp::wrap(replace_newline_var(*text), term_w_));
            }

            /** \brief `{usage}` body only (template supplies heading). */
            constexpr void write_usage_body() {
                const std::optional<styled_str> body = render_usage_body(*cmd_, {}, usage_name_);
                if (body.has_value()) out_.append(*body);
            }

            // -- argument sections ------------------------------------------

            /**
             * \brief Args under the default headings.
             * \param positionals Collect positionals vs named.
             */
            [[nodiscard]] constexpr std::vector<const arg_spec*>
            collect_args(bool positionals) const {
                std::vector<const arg_spec*> found;
                for (const arg_spec& one : cmd_->get_arguments()) {
                    if (one.is_positional() != positionals) continue;
                    found.push_back(&one);
                }
                return found;
            }

            /** \brief Write `<heading>:` as one header run, then newline. */
            constexpr void write_heading(std::string_view heading) {
                std::string text;
                append_bytes(text, heading);
                text.push_back(':');
                out_.push(style_class::header, text);
                out_.push_plain("\n");
            }

            /** \brief All sections: Commands, Arguments, Options, then custom headings. */
            constexpr void write_all_args() {
                std::vector<const arg_spec*> positionals;
                std::vector<const arg_spec*> named;
                for (const arg_spec& one : cmd_->get_arguments()) {
                    if (effective_help_heading(one).has_value()) continue;
                    if (!should_show_arg(use_long_, one)) continue;
                    (one.is_positional() ? positionals : named).push_back(&one);
                }

                std::vector<std::string_view> custom_headings;
                for (const arg_spec& one : cmd_->get_arguments()) {
                    const std::optional<std::string_view> heading = effective_help_heading(one);
                    if (!heading.has_value()) continue;
                    if (std::ranges::find(custom_headings, *heading) == custom_headings.end())
                        custom_headings.push_back(*heading);
                }

                const bool subcommands = cmd_->has_visible_subcommands();
                const bool flatten     = cmd_->is_flatten_help_set();
                bool first             = true;

                if (subcommands && !flatten) {
                    first = false;
                    write_heading(cmd_->get_subcommand_help_heading().value_or(
                            default_subcommand_heading));
                    write_subcommands(*cmd_);
                }
                if (!positionals.empty()) {
                    if (!first) out_.push_plain("\n\n");
                    first = false;
                    write_heading(positional_heading);
                    write_args(positionals, true);
                }
                if (!named.empty()) {
                    if (!first) out_.push_plain("\n\n");
                    first = false;
                    write_heading(option_heading);
                    write_args(named, false);
                }
                for (const std::string_view heading : custom_headings) {
                    std::vector<const arg_spec*> section;
                    for (const arg_spec& one : cmd_->get_arguments()) {
                        if (effective_help_heading(one) != std::optional{heading}) continue;
                        if (!should_show_arg(use_long_, one)) continue;
                        section.push_back(&one);
                    }
                    if (section.empty()) continue;
                    if (!first) out_.push_plain("\n\n");
                    first = false;
                    write_heading(heading);
                    write_args(section, false);
                }
                if (subcommands && flatten) write_flat_subcommands(*cmd_, first, self_bin_path());
            }

            /**
             * \brief Measure option column, sort, lay out rows.
             * \param args Candidates (re-filtered for visibility).
             * \param positional_key Sort by index vs option_sort_key.
             */
            constexpr void write_args(std::span<const arg_spec* const> args, bool positional_key) {
                // The shortest an argument can legally be is 2, i.e. `-x`.
                std::size_t longest = 2;

                struct row {
                    std::size_t order = 0;
                    std::string key{};
                    std::size_t seq     = 0;
                    const arg_spec* arg = nullptr;
                };

                std::vector<row> rows;
                for (const arg_spec* one : args) {
                    // Deliberately no width contribution from a hidden argument: clap
                    // skips it here as well as in the layout, so hiding an argument can
                    // narrow the column.
                    if (!should_show_arg(use_long_, *one)) continue;
                    const std::size_t width = display_width(arg_column_text(*one));
                    const std::size_t actual =
                            one->get_long().has_value() ? width + help_short_size : width;
                    if (actual > longest) longest = actual;
                    rows.push_back({.order = positional_key ? positional_sort_key(*one)
                                                            : one->get_display_order(),
                                    .key   = positional_key ? std::string{} : option_sort_key(*one),
                                    .seq   = rows.size(),
                                    .arg   = one});
                }

                // clap keys a BTreeMap on (display_order, key); the `seq` tie-break makes
                // std::ranges::sort stable without needing constexpr std::stable_sort,
                // which C++26 only just gained and neither standard library ships yet.
                std::ranges::sort(rows, [](const row& lhs, const row& rhs) {
                    if (lhs.order != rhs.order) return lhs.order < rhs.order;
                    if (lhs.key != rhs.key) return lhs.key < rhs.key;
                    return lhs.seq < rhs.seq;
                });

                const bool next_line_help = will_args_wrap(args, longest);
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    if (i != 0) {
                        out_.push_plain("\n");
                        if (next_line_help && use_long_) out_.push_plain("\n");
                    }
                    write_arg(*rows[i].arg, next_line_help, longest);
                }
            }

            /** \brief One argument row: tab, short, long, suffix, pad, description. */
            constexpr void
            write_arg(const arg_spec& arg, bool next_line_help, std::size_t longest) {
                const styled_str spec = spec_vals(arg);

                out_.push_plain(help_tab);
                write_short(arg);
                write_long(arg);
                out_.append(stylized_arg_suffix(arg, std::nullopt));
                align_to_about(arg, next_line_help, longest);

                const std::optional<std::string_view> about =
                        use_long_ ? (arg.get_long_help().has_value() ? arg.get_long_help()
                                                                     : arg.get_help())
                                  : (arg.get_help().has_value() ? arg.get_help()
                                                                : arg.get_long_help());

                std::span<const possible_value> listed{};
                if (!arg.is_hide_possible_values_set() && use_long_pv(arg))
                    listed = arg.get_possible_values();

                write_help_body(about.value_or(std::string_view{}),
                                spec,
                                next_line_help,
                                longest,
                                true,
                                listed);
            }

            /** \brief Write short flag, or four spaces when only long is present. */
            constexpr void write_short(const arg_spec& arg) {
                if (const std::optional<char> short_name = arg.get_short();
                    short_name.has_value()) {
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back(*short_name);
                    out_.push(style_class::literal, spelling);
                } else if (arg.get_long().has_value()) {
                    out_.push_plain("    ");
                }
            }

            constexpr void write_long(const arg_spec& arg) {
                const std::optional<std::string_view> long_name = arg.get_long();
                if (!long_name.has_value()) return;
                if (arg.get_short().has_value()) out_.push_plain(", ");
                std::string spelling;
                spelling.push_back('-');
                spelling.push_back('-');
                append_bytes(spelling, *long_name);
                out_.push(style_class::literal, spelling);
            }

            /** \brief Pad from option column to description column. */
            constexpr void
            align_to_about(const arg_spec& arg, bool next_line_help, std::size_t longest) {
                if (use_long_ || next_line_help) return;  // the description is on its own line
                const std::size_t width = display_width(arg_column_text(arg));
                std::size_t padding     = 0;
                if (!arg.is_positional()) {
                    const std::size_t self_len = width + help_short_size;
                    // With a long option the four `-s, ` cells are already inside
                    // `longest`; without one they are not, so they are added here.
                    const std::size_t slack =
                            arg.get_long().has_value() ? help_tab_width : help_tab_width + 4;
                    padding = saturating_sub(longest + slack, self_len);
                } else {
                    padding = saturating_sub(longest + help_tab_width, width);
                }
                out_.push_plain(spaces(padding));
            }

            /**
             * \brief Description column plus long-help blocks (spec tail, possible values).
             * \param about Description text.
             * \param spec `[default:]` / `[env:]` / `[aliases:]` tail.
             * \param next_line_help Description on its own line.
             * \param longest Option column width.
             * \param is_arg Argument row vs subcommand (avoids null arg_spec*; trap 10).
             * \param listed Possible values for one-per-line long help, or empty.
             */
            constexpr void write_help_body(std::string_view about,
                                           const styled_str& spec,
                                           bool next_line_help,
                                           std::size_t longest,
                                           bool is_arg,
                                           std::span<const possible_value> listed) {
                if (next_line_help) {
                    out_.push_plain("\n");
                    out_.push_plain(help_tab);
                    out_.push_plain(help_next_line_indent);
                }

                const std::size_t column = next_line_help
                                                   ? help_tab_width + help_next_line_indent.size()
                                                   : longest + help_tab_width * 2;
                const std::string trailing_indent = spaces(column);

                styled_str body;
                body.push_plain(replace_newline_var(about));
                bool body_is_empty           = body.empty();
                const bool specs_on_own_line = use_long_ && is_arg;

                if (!spec.empty() && !specs_on_own_line) {
                    if (!body_is_empty) body.push_plain(" ");
                    body.append(spec);
                    body_is_empty = body.empty();
                }

                const std::size_t available = saturating_sub(term_w_, column);
                out_.append(clapp::indent(
                        clapp::wrap(body, available, cmd_->get_styles()), "", trailing_indent));

                const bool listed_any = write_possible_values(listed, column, body_is_empty);

                if (!spec.empty() && specs_on_own_line) {
                    styled_str tail;
                    if (!body_is_empty || listed_any) tail.push_plain("\n\n");
                    tail.append(spec);
                    out_.append(clapp::indent(
                            clapp::wrap(tail, available, cmd_->get_styles()), "", trailing_indent));
                }
            }

            /**
             * \brief Long-help `Possible values:` block.
             * \return Whether anything was written (separator for next-line spec).
             */
            [[nodiscard]] constexpr bool
            write_possible_values(std::span<const possible_value> listed,
                                  std::size_t column,
                                  bool body_is_empty) {
                if (listed.empty()) return false;

                std::size_t widest = 0;
                bool visible       = false;
                for (const possible_value& one : listed) {
                    if (one.is_hide_set()) continue;
                    visible                 = true;
                    const std::size_t width = display_width(one.get_name());
                    if (width > widest) widest = width;
                }
                // clap calls `.max().expect("Only called with possible value")` here and
                // would panic on an argument whose values are all hidden. Unreachable
                // today — use_long_pv() already found one that is not — but a panic is
                // not a rendering, so clapp declines instead.
                if (!visible) return false;

                constexpr std::size_t dash_space = 2;  // "- "
                const std::size_t bullet_column  = column + help_tab_width - dash_space;
                const std::string bullet_indent  = spaces(bullet_column);
                const std::string wrap_indent    = spaces(bullet_column + dash_space);

                if (!body_is_empty) {
                    out_.push_plain("\n\n");
                    out_.push_plain(bullet_indent);
                }
                out_.push_plain("Possible values:");

                for (const possible_value& one : listed) {
                    if (one.is_hide_set()) continue;
                    styled_str description;
                    description.push(style_class::literal, one.get_name());
                    if (const std::optional<std::string_view> help = one.get_help();
                        help.has_value()) {
                        description.push_plain(": ");
                        description.push_plain(
                                spaces(saturating_sub(widest, display_width(one.get_name()))));
                        description.push_plain(replace_newline_var(*help));
                    }
                    // clap's `else { usize::MAX }`, not `0`: a terminal narrower than the
                    // bullet column stops wrapping rather than putting every word on its
                    // own line.
                    const std::size_t available = term_w_ > wrap_indent.size()
                                                          ? term_w_ - wrap_indent.size()
                                                          : unbounded_width;
                    out_.push_plain("\n");
                    out_.push_plain(bullet_indent);
                    out_.push_plain("- ");
                    out_.append(
                            clapp::indent(clapp::wrap(description, available, cmd_->get_styles()),
                                          "",
                                          wrap_indent));
                }
                return true;
            }

            /** \brief True if any arg forces next-line help for the whole section. */
            [[nodiscard]] constexpr bool will_args_wrap(std::span<const arg_spec* const> args,
                                                        std::size_t longest) const {
                for (const arg_spec* one : args) {
                    if (!should_show_arg(use_long_, *one)) continue;
                    if (arg_next_line_help(*one, spec_vals(*one), longest)) return true;
                }
                return false;
            }

            /** \brief Whether this arg's description starts on its own line. */
            [[nodiscard]] constexpr bool arg_next_line_help(const arg_spec& arg,
                                                            const styled_str& spec,
                                                            std::size_t longest) const {
                if (next_line_help_ || arg.is_next_line_help_set() || use_long_) return true;
                const std::optional<std::string_view> about =
                        arg.get_help().has_value() ? arg.get_help() : arg.get_long_help();
                const std::size_t wanted = display_width(about.value_or(std::string_view{})) +
                                           display_width(spec.to_string());
                return forces_next_line(wanted, longest);
            }

            /** \brief Next-line help for a subcommand row (ignores use_long_). */
            [[nodiscard]] constexpr bool subcommand_next_line_help(const command_spec& sub,
                                                                   const styled_str& spec,
                                                                   std::size_t longest) const {
                if (next_line_help_) return true;
                const std::optional<std::string_view> about =
                        sub.get_about().has_value() ? sub.get_about() : sub.get_long_about();
                const std::size_t wanted = display_width(about.value_or(std::string_view{})) +
                                           display_width(spec.to_string());
                return forces_next_line(wanted, longest);
            }

            /**
             * \brief Whether description width forces next-line layout.
             * \param wanted Cells the description wants.
             * \param longest Option column width.
             * \return True when column is both narrow and greedy.
             * \note 40% test as integer (`taken*5 > term_w*2`); guards unbounded_width overflow.
             */
            [[nodiscard]] constexpr bool forces_next_line(std::size_t wanted,
                                                          std::size_t longest) const noexcept {
                const std::size_t taken = longest + help_tab_width * 2;
                if (term_w_ < taken) return false;
                if (term_w_ > unbounded_width / 5) return false;
                if (taken * 5 <= term_w_ * 2) return false;
                return wanted > term_w_ - taken;
            }

            // -- subcommand sections ----------------------------------------

            /** \brief Write the Commands: table. */
            constexpr void write_subcommands(const command_spec& cmd) {
                std::size_t longest = 2;

                struct row {
                    std::size_t order = 0;
                    std::string key{};
                    styled_str label{};
                    const command_spec* sub = nullptr;
                };

                std::vector<row> rows;
                for (const command_spec& sub : cmd.get_subcommands()) {
                    if (!should_show_subcommand(sub)) continue;
                    styled_str label        = subcommand_label(sub);
                    const std::size_t width = display_width(label.to_string());
                    if (width > longest) longest = width;
                    rows.push_back({.order = sub.get_display_order(),
                                    .key   = label.to_string(),
                                    .label = std::move(label),
                                    .sub   = &sub});
                }
                std::ranges::sort(rows, [](const row& lhs, const row& rhs) {
                    if (lhs.order != rhs.order) return lhs.order < rhs.order;
                    return lhs.key < rhs.key;
                });

                const bool next_line_help = will_subcommands_wrap(cmd, longest);
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    if (i != 0) out_.push_plain("\n");
                    write_subcommand(rows[i].label, *rows[i].sub, next_line_help, longest);
                }
            }

            /** \brief First column of a subcommand row: `name, -s, --long`. */
            [[nodiscard]] static constexpr styled_str subcommand_label(const command_spec& sub) {
                styled_str label;
                label.push(style_class::literal, sub.get_name());
                if (const std::optional<char> short_flag = sub.get_short_flag();
                    short_flag.has_value()) {
                    label.push_plain(", ");
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back(*short_flag);
                    label.push(style_class::literal, spelling);
                }
                if (const std::optional<std::string_view> long_flag = sub.get_long_flag();
                    long_flag.has_value()) {
                    label.push_plain(", ");
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back('-');
                    append_bytes(spelling, *long_flag);
                    label.push(style_class::literal, spelling);
                }
                return label;
            }

            [[nodiscard]] constexpr bool will_subcommands_wrap(const command_spec& cmd,
                                                               std::size_t longest) const {
                for (const command_spec& sub : cmd.get_subcommands()) {
                    if (!should_show_subcommand(sub)) continue;
                    if (subcommand_next_line_help(sub, sc_spec_vals(sub), longest)) return true;
                }
                return false;
            }

            constexpr void write_subcommand(const styled_str& label,
                                            const command_spec& sub,
                                            bool next_line_help,
                                            std::size_t longest) {
                const styled_str spec = sc_spec_vals(sub);
                const std::optional<std::string_view> about =
                        sub.get_about().has_value() ? sub.get_about() : sub.get_long_about();

                out_.push_plain(help_tab);
                out_.append(label);
                if (!next_line_help) {
                    out_.push_plain(spaces(saturating_sub(longest + help_tab_width,
                                                          display_width(label.to_string()))));
                }
                write_help_body(about.value_or(std::string_view{}),
                                spec,
                                next_line_help,
                                longest,
                                false,
                                {});
            }

            /**
             * \brief Flattened subcommands: one section per child, headed by typed path.
             * \param cmd Level whose children to flatten.
             * \param first Whether nothing written yet (separator between sections).
             * \param bin_path Plain space-joined path to \p cmd.
             */
            constexpr void write_flat_subcommands(const command_spec& cmd,
                                                  bool& first,
                                                  std::string_view bin_path) {
                struct row {
                    std::size_t order = 0;
                    std::string_view name{};
                    const command_spec* sub = nullptr;
                };

                std::vector<row> rows;
                for (const command_spec& sub : cmd.get_subcommands()) {
                    if (!should_show_subcommand(sub)) continue;
                    rows.push_back({.order = sub.get_display_order(),
                                    .name  = sub.get_name(),
                                    .sub   = &sub});
                }
                std::ranges::sort(rows, [](const row& lhs, const row& rhs) {
                    if (lhs.order != rhs.order) return lhs.order < rhs.order;
                    return lhs.name < rhs.name;
                });

                for (const row& one : rows) {
                    if (!first) out_.push_plain("\n\n");
                    first = false;

                    // Frozen tree: compute usage_name here (clap stamps it at build).
                    const std::string heading = usage_renderer{cmd, required_graph(cmd), bin_path}
                                                        .subcommand_usage_name(*one.sub, bin_path);
                    std::string headed;
                    append_bytes(headed, heading);
                    headed.push_back(':');
                    out_.push(style_class::header, headed);

                    const std::optional<std::string_view> about =
                            one.sub->get_about().has_value() ? one.sub->get_about()
                                                             : one.sub->get_long_about();
                    if (about.has_value() && !about->empty()) {
                        out_.push_plain("\n");
                        out_.push_plain(*about);
                    }

                    std::vector<const arg_spec*> args;
                    for (const arg_spec& candidate : one.sub->get_arguments()) {
                        if (!should_show_arg(use_long_, candidate)) continue;
                        if (candidate.is_global_set()) continue;
                        args.push_back(&candidate);
                    }
                    if (!args.empty()) out_.push_plain("\n");
                    write_args(args, false);

                    // Flatten recursion: frozen tree shows nested help's [COMMAND]...
                    // row; clap's lazy build omits it (差异清单 #30).
                    if (one.sub->is_flatten_help_set()) {
                        std::string child_path;
                        append_bytes(child_path, bin_path);
                        if (!child_path.empty()) child_path.push_back(' ');
                        append_bytes(child_path, one.sub->get_name());
                        write_flat_subcommands(*one.sub, first, child_path);
                    }
                }
            }

            // -- the bracketed tail -----------------------------------------

            /**
             * \brief Spec tail: `[env:]`, `[default:]`, `[aliases:]`, `[possible values:]`.
             * \param arg Argument to annotate.
             * \return Tail (long joins with newline, short with space), or empty.
             * \note hide_env drops whole annotation; hide_env_values keeps the name only.
             */
            [[nodiscard]] constexpr styled_str spec_vals(const arg_spec& arg) const {
                std::vector<styled_str> parts;

                if (const std::optional<std::string_view> variable = arg.get_env();
                    variable.has_value() && !arg.is_hide_env_set()) {
                    // lossy_utf8: env bytes may be non-UTF-8 on POSIX.
                    std::string body;
                    append_bytes(body, lossy_utf8(*variable));
                    if (!arg.is_hide_env_values_set()) {
                        body.push_back('=');
                        const std::optional<std::string_view> value = (*lookup_)(*variable);
                        if (value.has_value()) append_bytes(body, lossy_utf8(*value));
                    }
                    styled_str one;
                    one.push(style_class::context, "[env: ");
                    one.push(style_class::context_value, body);
                    one.push(style_class::context, "]");
                    parts.push_back(std::move(one));
                }

                if (arg.is_takes_value_set() && !arg.is_hide_default_value_set() &&
                    !arg.get_default_values().empty()) {
                    std::string joined;
                    bool first = true;
                    for (const arg_id& value : arg.get_default_values()) {
                        if (!first) joined.push_back(' ');
                        first = false;
                        // lossy then escape (clap order); defaults are stored as bytes.
                        append_bytes(joined, escape_help_value(lossy_utf8(value.name())));
                    }
                    styled_str one;
                    one.push(style_class::context, "[default: ");
                    one.push(style_class::context_value, joined);
                    one.push(style_class::context, "]");
                    parts.push_back(std::move(one));
                }

                std::vector<std::string> aliases;
                for (const char letter : arg.get_visible_short_aliases()) {
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back(letter);
                    aliases.push_back(std::move(spelling));
                }
                for (const std::string_view name : arg.get_visible_aliases()) {
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back('-');
                    append_bytes(spelling, name);
                    aliases.push_back(std::move(spelling));
                }
                if (!aliases.empty()) parts.push_back(alias_annotation(aliases));

                if (!arg.is_hide_possible_values_set() && !use_long_pv(arg)) {
                    std::vector<std::string> names;
                    for (const possible_value& one : arg.get_possible_values()) {
                        const std::optional<std::string_view> name = one.get_visible_name();
                        if (!name.has_value()) continue;
                        names.push_back(escape_help_value(*name));
                    }
                    if (!names.empty()) {
                        styled_str one;
                        one.push(style_class::context, "[possible values: ");
                        push_context_list(one, names);
                        one.push(style_class::context, "]");
                        parts.push_back(std::move(one));
                    }
                }

                return join_parts(parts, use_long_ ? "\n" : " ");
            }

            /** \brief Subcommand visible aliases (short flags, long flags, names). */
            [[nodiscard]] static constexpr styled_str sc_spec_vals(const command_spec& sub) {
                std::vector<std::string> aliases;
                for (const char letter : sub.get_visible_short_flag_aliases()) {
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back(letter);
                    aliases.push_back(std::move(spelling));
                }
                for (const std::string_view name : sub.get_visible_long_flag_aliases()) {
                    std::string spelling;
                    spelling.push_back('-');
                    spelling.push_back('-');
                    append_bytes(spelling, name);
                    aliases.push_back(std::move(spelling));
                }
                for (const std::string_view name : sub.get_visible_aliases()) {
                    std::string spelling;
                    append_bytes(spelling, name);
                    aliases.push_back(std::move(spelling));
                }
                if (aliases.empty()) return {};
                return alias_annotation(aliases);
            }

            /** \brief `[alias: …]` / `[aliases: …]` annotation. */
            [[nodiscard]] static constexpr styled_str
            alias_annotation(std::span<const std::string> aliases) {
                styled_str out;
                out.push(style_class::context, aliases.size() == 1 ? "[alias: " : "[aliases: ");
                push_context_list(out, aliases);
                out.push(style_class::context, "]");
                return out;
            }

            /** \brief Comma-separated context / context_value list body. */
            static constexpr void push_context_list(styled_str& out,
                                                    std::span<const std::string> items) {
                bool first = true;
                for (const std::string& item : items) {
                    if (!first) out.push(style_class::context, ", ");
                    first = false;
                    out.push(style_class::context_value, item);
                }
            }

            [[nodiscard]] static constexpr styled_str join_parts(std::span<const styled_str> parts,
                                                                 std::string_view separator) {
                styled_str out;
                bool first = true;
                for (const styled_str& one : parts) {
                    if (!first) out.push_plain(separator);
                    first = false;
                    out.append(one);
                }
                return out;
            }

            /** \brief Long PV list when any possible value has help of its own. */
            [[nodiscard]] constexpr bool use_long_pv(const arg_spec& arg) const {
                if (!use_long_) return false;
                return std::ranges::any_of(
                        arg.get_possible_values(),
                        [](const possible_value& one) { return one.should_show_help(); });
            }

            // -- shared -----------------------------------------------------

            /** \brief Plain stylized_arg text used to measure the option column. */
            [[nodiscard]] static constexpr std::string arg_column_text(const arg_spec& arg) {
                return stylized_arg(arg, std::nullopt).to_string();
            }

            /** \brief Path for flattened subcommand headings. */
            [[nodiscard]] constexpr std::string_view self_bin_path() const noexcept {
                if (!usage_name_.empty()) return usage_name_;
                return cmd_->get_bin_name().value_or(cmd_->get_name());
            }

            [[nodiscard]] static constexpr std::size_t saturating_sub(std::size_t lhs,
                                                                      std::size_t rhs) noexcept {
                return lhs > rhs ? lhs - rhs : 0;
            }

            const command_spec* cmd_ = nullptr;
            const Lookup* lookup_    = nullptr;
            std::string_view usage_name_{};
            styled_str out_{};
            std::size_t term_w_  = default_terminal_width;
            bool use_long_       = false;
            bool next_line_help_ = false;
        };

    }  // namespace detail

    // =======================================================================
    // The public seam
    // =======================================================================

    /**
     * \brief Whether `--help` has content `-h` would not (non-recursive).
     * \param cmd Command to inspect.
     * \return Whether some part of \p cmd has a long form.
     * \note Counts long_about, before/after_long_help, and visible args with long_help,
     *       hide_short/long, or PV help. Shared via long_help_exists_over() with builder.
     */
    [[nodiscard]] constexpr bool long_help_exists(const command_spec& cmd) noexcept {
        return detail::long_help_exists_over(cmd.get_long_about(),
                                             cmd.get_before_long_help(),
                                             cmd.get_after_long_help(),
                                             cmd.get_arguments());
    }

    /**
     * \brief Help screen for \p cmd.
     * \tparam Lookup env_lookup for `[env: VAR=value]`.
     * \param cmd Command to describe.
     * \param style Request (see help_style).
     * \param lookup Environment reader (no_env for pure/consteval).
     * \return Page ending in exactly one newline (styled_str; colour via render()).
     *
     * \warning **\p style.usage_name is borrowed and must outlive the call.**
     *
     * \warning **User-supplied escapes are stripped** (strip_escapes on the way out).
     *          clap may print them when colour is on; clapp spans are semantic-only —
     *          admitting raw ANSI risks mid-sequence wrap or colour-off emission
     *          (wedged terminal). 差异清单 #29.
     *
     * \warning **`use_long` is literal; `--help` also collapses via long_help_exists().**
     *          That collapse lives in render_help_text(), not here. long_form() with no
     *          long content yields next-line descs that real `--help` would not.
     */
    template<env_lookup Lookup>
    [[nodiscard]] constexpr styled_str
    render_help(const command_spec& cmd, help_style style, const Lookup& lookup) {
        return strip_escapes(detail::help_renderer<Lookup>{cmd, style, lookup}.render());
    }

    /**
     * \brief render_help with empty environment (pure; for tests / static_assert).
     * \param cmd Command.
     * \param style Request.
     * \return Page (`[env: VAR=]` when env declared but unset).
     */
    [[nodiscard]] constexpr styled_str render_help(const command_spec& cmd,
                                                   const help_style& style = {}) {
        return render_help(cmd, style, detail::no_env{});
    }

    /**
     * \brief render_help with real TTY width and process environment (used by parse()).
     * \param cmd Command.
     * \param style Request; fills detected_width if empty.
     * \return Page.
     */
    [[nodiscard]] inline styled_str render_help_for_terminal(const command_spec& cmd,
                                                             help_style style = {}) {
        if (!style.detected_width.has_value()) style.detected_width = detect_terminal_width();
        return render_help(cmd, style, detail::getenv_lookup{});
    }

    /**
     * \brief Version line for \p cmd: display name, optional version, trailing newline.
     * \param cmd Command.
     * \param long_form `--version` vs `-V`.
     * \return Line with display_name (not bin_name); each form falls back to the other.
     * \note strip_escapes on the way out (user-supplied strings).
     */
    [[nodiscard]] constexpr styled_str render_version(const command_spec& cmd, bool long_form) {
        const std::optional<std::string_view> primary =
                long_form ? cmd.get_long_version() : cmd.get_version();
        const std::optional<std::string_view> fallback =
                long_form ? cmd.get_version() : cmd.get_long_version();
        const std::optional<std::string_view> text = primary.has_value() ? primary : fallback;

        styled_str out;
        out.push_plain(cmd.get_display_name().value_or(cmd.get_name()));
        if (text.has_value()) {
            out.push_plain(" ");
            out.push_plain(*text);
        }
        out.push_plain("\n");
        return strip_escapes(out);
    }

    namespace detail {

        /** \brief Compile-time fixtures that protect help-layout invariants. */
        namespace help_contract {

            // Hand-built args: display_order 0 on verbose so help (999) sorts after it.
            inline constexpr arg_spec probe_args[] = {
                    arg_spec{.id            = arg_id{"verbose"},
                             .short_        = 'v',
                             .long_         = arg_id{"verbose"},
                             .act           = arg_action::set_true,
                             .num_args      = value_range::empty(),
                             .help_text     = "Say more",
                             .help_length   = 8,
                             .display_order = 0},
                    arg_spec{.id          = arg_id{"help"},
                             .short_      = 'h',
                             .long_       = arg_id{"help"},
                             .act         = arg_action::help,
                             .num_args    = value_range::empty(),
                             .help_text   = "Print help",
                             .help_length = 10},
            };

            inline constexpr command_spec probe_cmd{.name         = arg_id{"demo"},
                                                    .arg_data     = probe_args,
                                                    .arg_count    = 2,
                                                    .about_text   = "A demo",
                                                    .about_length = 6};

            consteval bool descriptions_share_one_column() {
                const std::string page = render_help(probe_cmd).to_string();
                return page == std::string_view{"A demo\n"
                                                "\n"
                                                "Usage: demo [OPTIONS]\n"
                                                "\n"
                                                "Options:\n"
                                                "  -v, --verbose  Say more\n"
                                                "  -h, --help     Print help\n"};
            }

            static_assert(descriptions_share_one_column(),
                          "clapp: the help column is longest + TAB_WIDTH * 2. If this "
                          "fails, help_tab_width, help_short_size or align_to_about() "
                          "moved and every help screen moved with it.");

            inline constexpr command_spec no_about_cmd{
                    .name = arg_id{"demo"}, .arg_data = probe_args, .arg_count = 2};

            consteval bool an_absent_about_leaves_no_blank_first_line() {
                const std::string page = render_help(no_about_cmd).to_string();
                return page.starts_with("Usage: demo");
            }

            static_assert(an_absent_about_leaves_no_blank_first_line(),
                          "clapp: trim_start_lines() must drop the newline that "
                          "{about-with-newline} leaves behind when there is no about.");

            inline constexpr command_spec templated_cmd{.name                 = arg_id{"demo"},
                                                        .help_template_text   = "a{nope}b",
                                                        .help_template_length = 8};

            consteval bool an_unknown_tag_round_trips() {
                return render_help(templated_cmd).to_string() == std::string_view{"a{nope}b\n"};
            }

            static_assert(an_unknown_tag_round_trips(),
                          "clapp: an unrecognised {tag} is re-emitted verbatim, which is "
                          "how a template author sees the typo.");

            static_assert(render_version(probe_cmd, false).to_string() ==
                          std::string_view{"demo\n"});

        }  // namespace help_contract

    }  // namespace detail

}  // namespace clapp
