/**
 * \file
 * \brief clapp::render_usage() and clapp::detail::usage_renderer — the Usage line.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/output/textwrap.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {

    namespace detail {

        // ===================================================================
        // id_set — clap's FlatSet<Id> and the half of ChildGraph<Id> that is used
        // ===================================================================

        /**
         * \brief Insertion-ordered set of clapp::arg_id (clap FlatSet / ChildGraph).
         *
         * \note Not clapp::flat_set (sorted). Order is user-visible in Usage: and in
         *       "required arguments were not provided" — declaration order, trap 17.
         * \note Deduplicates on insert; clap's ChildGraph does not, but consumers do.
         */
        class id_set {
        public:
            /** \brief An empty set. */
            constexpr id_set() = default;

            /**
             * \brief Add \p id if it is not present already.
             * \param id The id to add.
             * \return `true` when \p id was newly added.
             */
            constexpr bool insert(arg_id id) {
                if (contains(id.name())) return false;
                ids_.push_back(id);
                return true;
            }

            /**
             * \brief Whether \p name is in the set.
             * \param name The spelling to look for.
             */
            [[nodiscard]] constexpr bool contains(std::string_view name) const noexcept {
                return std::ranges::any_of(ids_,
                                           [name](const arg_id& id) { return id.name() == name; });
            }

            /** \brief The ids, in insertion order. */
            [[nodiscard]] constexpr std::span<const arg_id> ids() const noexcept { return ids_; }

            /** \brief How many ids the set holds. */
            [[nodiscard]] constexpr std::size_t size() const noexcept { return ids_.size(); }

            /** \brief Whether the set is empty. */
            [[nodiscard]] constexpr bool empty() const noexcept { return ids_.empty(); }

            /** \brief Equality by content **and order**. */
            [[nodiscard]] constexpr bool operator==(const id_set& other) const noexcept {
                return std::ranges::equal(ids_, other.ids_);
            }

        private:
            std::vector<arg_id> ids_{};
        };

        /**
         * \brief Whether \p names contains \p wanted.
         * \param names The ids to scan.
         * \param wanted The spelling to look for.
         */
        [[nodiscard]] constexpr bool contains_id(std::span<const arg_id> names,
                                                 std::string_view wanted) noexcept {
            return std::ranges::any_of(names,
                                       [wanted](const arg_id& id) { return id.name() == wanted; });
        }

        /**
         * \brief \p first then \p second (duplicates kept; callers dedupe rendered form).
         * \param first Ids first.
         * \param second Ids after.
         * \return Concatenation.
         */
        [[nodiscard]] constexpr std::vector<arg_id> concat_ids(std::span<const arg_id> first,
                                                               std::span<const arg_id> second) {
            std::vector<arg_id> out;
            out.reserve(first.size() + second.size());
            // append_range(), not views::concat: the supported libc++ lacks concat.
            out.append_range(first);
            out.append_range(second);
            return out;
        }

        // ===================================================================
        // The two walks over the frozen tree
        // ===================================================================

        /**
         * \brief Argument ids reachable from group \p group_id (nested groups flattened).
         * \param cmd Command level.
         * \param group_id Group to expand.
         * \return Member arg ids in declaration order, no duplicates; unknown → empty.
         * \note Worklist skips already-expanded groups so a hand-written cyclic
         *       command_spec cannot hang constexpr (freeze() already rejects bad members).
         */
        [[nodiscard]] constexpr std::vector<arg_id>
        unroll_args_in_group(const command_spec& cmd, std::string_view group_id) {
            // A worklist, deliberately: the loop pushes back onto the container it is
            // draining (a group may name another group), so there is no range to pipe.
            std::vector<std::string_view> pending{group_id};
            std::vector<std::string_view> expanded;
            std::vector<arg_id> args;

            while (!pending.empty()) {
                const std::string_view current = pending.back();
                pending.pop_back();
                if (std::ranges::find(expanded, current) != expanded.end()) continue;
                expanded.push_back(current);
                if (!cmd.has_group(current)) continue;

                const group_spec& group = *cmd.find_group(current);
                for (const arg_id& member : group.get_args()) {
                    if (contains_id(args, member.name())) continue;
                    if (cmd.has_arg(member.name())) {
                        args.push_back(member);
                    } else {
                        pending.push_back(member.name());
                    }
                }
            }
            return args;
        }

        /**
         * \brief Transitive closure of \p start's `requires`, filtered by \p is_relevant.
         * \tparam Relevant `(const requires_spec&) -> optional<arg_id>`; nullopt drops.
         * \param cmd Command level.
         * \param is_relevant Which edges count.
         * \param start Id to walk from.
         * \return Reachable ids (may duplicate); \p start itself is never included.
         *
         * \warning \p is_relevant is applied at **every** level, bound to the walk's
         *          *start* matched_arg (clap). So `--a` value `x` with
         *          `a.requires_if("x","b")` and `b.requires_if("x","c")` pulls in `c`.
         *          Reproduced deliberately; changing it alters required-arg sets.
         */
        template<class Relevant>
        [[nodiscard]] constexpr std::vector<arg_id>
        unroll_arg_requires(const command_spec& cmd, Relevant is_relevant, std::string_view start) {
            // A worklist, deliberately; see unroll_args_in_group(). `requires` is
            // transitive, so the loop grows its own input.
            std::vector<std::string_view> pending{start};
            std::vector<std::string_view> expanded;
            std::vector<arg_id> found;

            while (!pending.empty()) {
                const std::string_view current = pending.back();
                pending.pop_back();
                if (std::ranges::find(expanded, current) != expanded.end()) continue;
                expanded.push_back(current);
                if (!cmd.has_arg(current)) continue;

                const arg_spec& here = *cmd.find_arg(current);
                for (const requires_spec& rule : here.get_requires()) {
                    const std::optional<arg_id> target = is_relevant(rule);
                    if (!target.has_value()) continue;
                    if (cmd.has_arg(target->name())) {
                        const arg_spec& next = *cmd.find_arg(target->name());
                        if (!next.get_requires().empty()) pending.push_back(next.get_id().name());
                    }
                    found.push_back(*target);
                }
            }
            return found;
        }

        /**
         * \brief Keep unconditional `requires` edges; drop `requires_if`.
         * \param rule One requires / requires_if edge.
         * \return Target, or nullopt for conditional rules.
         */
        [[nodiscard]] constexpr std::optional<arg_id>
        unconditional_requirement(const requires_spec& rule) noexcept {
            if (!rule.when.is_present_only()) return std::nullopt;
            return rule.target;
        }

        // ===================================================================
        // The matcher seam
        // ===================================================================

        /**
         * \brief Matcher seam: was this id explicit and does it satisfy \p when?
         * \tparam M Models check_explicit(id, when) → bool (arg_matcher or test stub).
         * \note Concept keeps `output → parser` out of the module graph.
         * \warning Predicate is by **const reference** (call constraint, not signature).
         */
        template<class M>
        concept arg_presence_source =
                requires(const M& source, std::string_view id, const arg_predicate& when) {
                    { source.check_explicit(id, when) } -> std::same_as<bool>;
                };

        // ===================================================================
        // Rendering: how an argument names itself in a usage line
        // ===================================================================

        /**
         * \brief Text after `--long`/`-s`: ` <FILE>`, `[=<level>]`, `...`, or empty.
         * \param arg Argument.
         * \param required Value required-ness; nullopt asks the argument.
         * \return Suffix spans. Shared with help_renderer::write_arg().
         */
        [[nodiscard]] constexpr styled_str stylized_arg_suffix(const arg_spec& arg,
                                                               std::optional<bool> required) {
            styled_str out;
            bool need_closing_bracket = false;
            if (arg.is_takes_value_set() && !arg.is_positional()) {
                const bool optional_val = arg.get_min_vals() == 0;
                if (arg.is_require_equals_set()) {
                    if (optional_val) {
                        need_closing_bracket = true;
                        out.push(style_class::placeholder, "[=");
                    } else {
                        out.push(style_class::literal, "=");
                    }
                } else if (optional_val) {
                    need_closing_bracket = true;
                    out.push(style_class::placeholder, " [");
                } else {
                    out.push(style_class::placeholder, " ");
                }
            }
            if (arg.is_takes_value_set() || arg.is_positional()) {
                out.push(style_class::placeholder,
                         render_arg_values(arg, required.value_or(arg.is_required_set())));
            } else if (arg.get_action() == arg_action::count) {
                out.push(style_class::placeholder, "...");
            }
            if (need_closing_bracket) out.push(style_class::placeholder, "]");
            return out;
        }

        /**
         * \brief How \p arg is spelled in a usage line or diagnostic.
         * \param arg Argument.
         * \param required Required rendering; nullopt asks the argument.
         * \return e.g. `--port <port>`, `-v`, `<FILE>...` (literal + placeholder spans).
         * \note \p required lets a demanded arg render as `<>` even when not declared
         *       required; using the arg's own flag alone yields wrong `[FILE]`.
         */
        [[nodiscard]] constexpr styled_str stylized_arg(const arg_spec& arg,
                                                        std::optional<bool> required) {
            styled_str out;
            if (const std::optional<std::string_view> long_name = arg.get_long();
                long_name.has_value()) {
                std::string spelling;
                spelling.push_back('-');
                spelling.push_back('-');
                append_bytes(spelling, *long_name);
                out.push(style_class::literal, spelling);
            } else if (const std::optional<char> short_name = arg.get_short();
                       short_name.has_value()) {
                std::string spelling;
                spelling.push_back('-');
                spelling.push_back(*short_name);
                out.push(style_class::literal, spelling);
            }
            out.append(stylized_arg_suffix(arg, required));
            return out;
        }

        /**
         * \brief Positional placeholders without surrounding brackets.
         * \param arg Argument.
         * \return Value name(s), or the id when none declared.
         */
        [[nodiscard]] constexpr std::string arg_name_no_brackets(const arg_spec& arg) {
            const std::span<const arg_id> names = arg.get_value_names();
            std::string out;
            if (names.empty()) {
                append_bytes(out, arg.get_id().name());
                return out;
            }
            if (names.size() == 1) {
                append_bytes(out, names.front().name());
                return out;
            }
            bool first = true;
            for (const arg_id& one : names) {
                if (!first) out.push_back(' ');
                first = false;
                out.push_back('<');
                append_bytes(out, one.name());
                out.push_back('>');
            }
            return out;
        }

        /**
         * \brief Group as `<--a|--b|<FILE>>` (by spelling, not id).
         * \param cmd Command level.
         * \param group_id Group to render.
         * \return Rendered group; unknown/empty → `<>`.
         */
        [[nodiscard]] constexpr styled_str format_group(const command_spec& cmd,
                                                        std::string_view group_id) {
            std::string body;
            bool first = true;
            for (const arg_id& member : unroll_args_in_group(cmd, group_id)) {
                if (!cmd.has_arg(member.name())) continue;
                const arg_spec& arg = *cmd.find_arg(member.name());
                if (!first) body.push_back('|');
                first = false;
                if (arg.is_positional()) {
                    append_bytes(body, arg_name_no_brackets(arg));
                } else {
                    append_bytes(body, stylized_arg(arg, std::nullopt).to_string());
                }
            }

            styled_str out;
            out.push(style_class::placeholder, "<");
            out.push(style_class::placeholder, body);
            out.push(style_class::placeholder, ">");
            return out;
        }

        /**
         * \brief \p text with trailing whitespace removed (forwards to clapp::trim_end).
         * \param text Message to trim.
         * \return Copy without trailing whitespace; empty fragments dropped.
         */
        [[nodiscard]] constexpr styled_str styled_trim_end(const styled_str& text) {
            return clapp::trim_end(text);
        }

        // ===================================================================
        // The required graph
        // ===================================================================

        /**
         * \brief Ids this command demands before any argument is seen.
         * \param cmd Command level.
         * \return required() args, then required() groups and their requires targets.
         * \note Conditional requirements need matches; see validator::gather_requires().
         */
        [[nodiscard]] constexpr id_set required_graph(const command_spec& cmd) {
            id_set required;
            for (const arg_spec& one : cmd.get_arguments()) {
                if (one.is_required_set()) required.insert(one.get_id());
            }
            for (const group_spec& group : cmd.get_groups()) {
                if (!group.is_required_set()) continue;
                required.insert(group.get_id());
                for (const arg_id& target : group.get_requires()) required.insert(target);
            }
            return required;
        }

        // ===================================================================
        // Usage
        // ===================================================================

        /** \brief The placeholder a command with subcommands shows when none was named. */
        inline constexpr std::string_view default_subcommand_value_name = "COMMAND";

        /** \brief What clap's `USAGE_SEP` puts between two alternative usage lines. */
        inline constexpr std::string_view usage_separator = "\n       ";

        /**
         * \brief Append \p form unless an equal rendering is already present.
         * \param forms Accumulated renderings (insertion order).
         * \param form Candidate.
         * \note Dedupe by text+styling so two rules demanding the same spelling collapse.
         */
        constexpr void push_unique_form(std::vector<styled_str>& forms, styled_str form) {
            if (std::ranges::find(forms, form) != forms.end()) return;
            forms.push_back(std::move(form));
        }

        /**
         * \brief Usage-line engine behind clapp::render_usage().
         *
         * Parser/validator use this when they already hold a grown required set or a
         * parse-known usage_name. Pure function of frozen command_spec; constexpr where
         * the matcher allows (stub matchers keep requires_if paths compile-time).
         */
        class usage_renderer {
        private:
            /**
             * \brief Shared body of get_required_usage_from overloads.
             * \note Defined above callers (clang member-template instantiation rule).
             */
            template<class Present>
            [[nodiscard]] constexpr std::vector<styled_str> required_usage_forms(
                    std::span<const arg_id> wanted, bool incl_last, Present is_present) const {
                id_set group_members;
                std::vector<styled_str> group_forms;
                for (const arg_id& req : wanted) {
                    if (!cmd_->has_group(req.name())) continue;
                    const std::vector<arg_id> members = unroll_args_in_group(*cmd_, req.name());
                    const bool satisfied = std::ranges::any_of(members, [&](const arg_id& member) {
                        return is_present(member.name());
                    });
                    if (satisfied) continue;
                    for (const arg_id& member : members) group_members.insert(member);
                    push_unique_form(group_forms, format_group(*cmd_, req.name()));
                }

                std::vector<styled_str> opt_forms;
                std::vector<styled_str> positional_forms;
                for (const arg_id& req : wanted) {
                    if (!cmd_->has_arg(req.name())) continue;
                    const arg_spec& arg = *cmd_->find_arg(req.name());
                    if (group_members.contains(arg.get_id().name())) continue;
                    if (is_present(req.name())) continue;

                    styled_str form = stylized_arg(arg, true);
                    if (const std::optional<std::size_t> index = arg.get_index();
                        index.has_value()) {
                        if (arg.is_last_set() && !incl_last) continue;
                        if (positional_forms.size() < *index + 1)
                            positional_forms.resize(*index + 1);
                        positional_forms[*index] = std::move(form);
                    } else {
                        push_unique_form(opt_forms, std::move(form));
                    }
                }

                std::vector<styled_str> out = std::move(opt_forms);
                for (styled_str& form : group_forms) out.push_back(std::move(form));
                for (styled_str& form : positional_forms) {
                    if (form.empty()) continue;
                    out.push_back(std::move(form));
                }
                return out;
            }

            /** \brief Unconditional requires chain (requires_if dropped). */
            [[nodiscard]] constexpr std::vector<arg_id> unconditional_requirements() const {
                std::vector<arg_id> unrolled;
                for (const arg_id& one : required_.ids()) {
                    for (const arg_id& reached :
                         unroll_arg_requires(*cmd_, unconditional_requirement, one.name()))
                        unrolled.push_back(reached);
                    unrolled.push_back(one);
                }
                return unrolled;
            }

            /**
             * \brief Requires chain with requires_if judged against \p matcher.
             * \note Predicate binds the **outer** required id (see unroll_arg_requires
             *       \warning). Defined above callers (clang member-template rule).
             */
            template<arg_presence_source Matcher>
            [[nodiscard]] constexpr std::vector<arg_id>
            conditional_requirements(const Matcher& matcher) const {
                std::vector<arg_id> unrolled;
                for (const arg_id& one : required_.ids()) {
                    const std::string_view outer = one.name();
                    const auto is_relevant       = [&](const requires_spec& rule) {
                        if (rule.when.is_present_only()) return std::optional<arg_id>{rule.target};
                        if (!matcher.check_explicit(outer, rule.when))
                            return std::optional<arg_id>{};
                        return std::optional<arg_id>{rule.target};
                    };
                    for (const arg_id& reached : unroll_arg_requires(*cmd_, is_relevant, outer))
                        unrolled.push_back(reached);
                    unrolled.push_back(one);
                }
                return unrolled;
            }

        public:
            /**
             * \brief A renderer for \p cmd, computing its required set itself.
             * \param cmd The command to describe; must outlive the renderer.
             */
            explicit constexpr usage_renderer(const command_spec& cmd)
                : cmd_(&cmd), required_(required_graph(cmd)) {}

            /**
             * \brief A renderer for \p cmd using an already-computed required set.
             * \param cmd      The command to describe; must outlive the renderer.
             * \param required The required set, normally the validator's own — which has
             *        grown by then to include the `requires` targets of what was typed.
             */
            constexpr usage_renderer(const command_spec& cmd, id_set required)
                : cmd_(&cmd), required_(std::move(required)) {}

            /**
             * \brief Renderer for \p cmd named by a caller-supplied path.
             * \param cmd Command; must outlive the renderer.
             * \param required Required set.
             * \param usage_name What Usage: calls \p cmd; empty asks \p cmd itself.
             *
             * \warning **Borrowed view; must outlive the renderer.** command_spec is
             *          frozen (ADR-0005) and cannot hold its own path; parse_engine
             *          threads usage_name_ down so subcommands say `Usage: test sub …`.
             */
            constexpr usage_renderer(const command_spec& cmd,
                                     id_set required,
                                     std::string_view usage_name)
                : cmd_(&cmd), required_(std::move(required)), usage_name_(usage_name) {}

            /**
             * \brief Usage line with `Usage:` heading.
             * \param used Ids to force in (smart usage); empty = full help usage.
             * \return The line, or nullopt when empty.
             */
            [[nodiscard]] constexpr std::optional<styled_str>
            create_usage_with_title(std::span<const arg_id> used) const {
                const std::optional<styled_str> body = create_usage_no_title(used);
                if (!body.has_value()) return std::nullopt;
                styled_str out;
                out.push(style_class::usage, "Usage:");
                out.push_plain(" ");
                out.append(*body);
                return out;
            }

            /**
             * \brief Usage line without heading.
             * \param used See create_usage_with_title().
             * \return The line, or nullopt when empty.
             * \note strip_escapes runs here (not only in render_usage): parser/validator
             *       construct usage_renderer directly and would bypass a public-only strip.
             */
            [[nodiscard]] constexpr std::optional<styled_str>
            create_usage_no_title(std::span<const arg_id> used) const {
                styled_str out;
                write_usage_no_title(out, used);
                out = strip_escapes(styled_trim_end(out));
                if (out.empty()) return std::nullopt;
                return out;
            }

            /**
             * \brief Outstanding requirements, each rendered alone (error bullet list).
             * \tparam Matcher arg_presence_source (keeps parser out of this header).
             * \param incls Ids to include even if satisfied.
             * \param matcher Drops requirements already met.
             * \param incl_last Whether a last() positional may appear.
             * \return One styled_str per requirement: options, groups, then positionals.
             */
            template<arg_presence_source Matcher>
            [[nodiscard]] constexpr std::vector<styled_str> get_required_usage_from(
                    std::span<const arg_id> incls, const Matcher& matcher, bool incl_last) const {
                return required_usage_forms(concat_ids(conditional_requirements(matcher), incls),
                                            incl_last,
                                            [&](std::string_view id) {
                                                return matcher.check_explicit(
                                                        id, arg_predicate::present());
                                            });
            }

            /**
             * \brief get_required_usage_from with no matches (requires_if dropped).
             * \param incls Ids to include.
             * \param incl_last Whether a last() positional may appear.
             * \return One styled_str per requirement.
             * \note Keeps subcommand_usage_name / flatten-help usage constexpr.
             */
            [[nodiscard]] constexpr std::vector<styled_str>
            get_required_usage_from(std::span<const arg_id> incls, bool incl_last) const {
                return required_usage_forms(concat_ids(unconditional_requirements(), incls),
                                            incl_last,
                                            [](std::string_view) { return false; });
            }

            /** \brief The required set this renderer was given or computed. */
            [[nodiscard]] constexpr const id_set& required() const noexcept { return required_; }

            /**
             * \brief What the `Usage:` line calls this command. clap's
             *        `Command::get_usage_name_fallback`.
             * \return The path this renderer was constructed with, or the command's own
             *         `bin_name` / `name` when it was given none.
             */
            [[nodiscard]] constexpr std::string_view usage_name() const noexcept {
                if (!usage_name_.empty()) return usage_name_;
                return cmd_->get_bin_name().value_or(cmd_->get_name());
            }

            /**
             * \brief Child's usage path: bin_path + outstanding reqs + child name/flags.
             * \param sub Child command.
             * \param bin_path Parent plain path (space-joined names).
             * \return e.g. `test {sub|--sub|-s}`, `test --out <out> sub`, or `test sub`.
             */
            [[nodiscard]] constexpr std::string
            subcommand_usage_name(const command_spec& sub, std::string_view bin_path) const {
                std::string names;
                append_bytes(names, sub.get_name());
                bool is_flag_subcommand = false;
                if (const std::optional<std::string_view> long_flag = sub.get_long_flag();
                    long_flag.has_value()) {
                    append_bytes(names, "|--");
                    append_bytes(names, *long_flag);
                    is_flag_subcommand = true;
                }
                if (const std::optional<char> short_flag = sub.get_short_flag();
                    short_flag.has_value()) {
                    append_bytes(names, "|-");
                    names.push_back(*short_flag);
                    is_flag_subcommand = true;
                }
                if (is_flag_subcommand) {
                    std::string braced;
                    braced.push_back('{');
                    append_bytes(braced, names);
                    braced.push_back('}');
                    names = std::move(braced);
                }

                if (bin_path.empty()) return names;

                std::string out;
                append_bytes(out, bin_path);
                out.push_back(' ');
                if (!cmd_->is_subcommand_negates_reqs_set() &&
                    !cmd_->is_args_conflicts_with_subcommands_set()) {
                    for (const styled_str& one : get_required_usage_from({}, true)) {
                        append_bytes(out, one.to_string());
                        out.push_back(' ');
                    }
                }
                append_bytes(out, names);
                return out;
            }

        private:
            /** Whether `[OPTIONS]` belongs in the line. clap's `needs_options_tag`. */
            [[nodiscard]] constexpr bool needs_options_tag() const {
                for (const arg_spec& one : cmd_->get_arguments()) {
                    if (one.is_positional()) continue;
                    if (one.get_long() == std::optional<std::string_view>{"help"}) continue;
                    if (one.get_long() == std::optional<std::string_view>{"version"}) continue;
                    switch (one.get_action()) {
                    case arg_action::help:
                    case arg_action::help_short:
                    case arg_action::help_long:
                    case arg_action::version:
                        continue;
                    case arg_action::set:
                    case arg_action::append:
                    case arg_action::set_true:
                    case arg_action::set_false:
                    case arg_action::count:
                    case arg_action::infer:
                        break;
                    }
                    if (one.is_hide_set()) continue;
                    if (one.is_required_set()) continue;

                    bool in_required_group = false;
                    for (const std::string_view name : cmd_->groups_for_arg(one.get_id().name())) {
                        if (!cmd_->has_group(name)) continue;
                        if (cmd_->find_group(name)->is_required_set()) {
                            in_required_group = true;
                            break;
                        }
                    }
                    if (in_required_group) continue;
                    return true;
                }
                return false;
            }

            constexpr void write_usage_no_title(styled_str& out,
                                                std::span<const arg_id> used) const {
                if (const std::optional<std::string_view> override_text =
                            cmd_->get_override_usage();
                    override_text.has_value()) {
                    out.push_plain(*override_text);
                    return;
                }
                if (used.empty()) {
                    write_help_usage(out);
                } else {
                    write_smart_usage(out, used);
                }
            }

            constexpr void write_help_usage(styled_str& out) const {
                if (cmd_->has_visible_subcommands() && cmd_->is_flatten_help_set()) {
                    if (!cmd_->is_subcommand_required_set() ||
                        cmd_->is_args_conflicts_with_subcommands_set()) {
                        write_arg_usage(out, {}, true);
                        out = styled_trim_end(out);
                        out.push_plain(usage_separator);
                    }
                    bool first = true;
                    for (const command_spec& sub : cmd_->get_subcommands()) {
                        if (sub.is_hide_set()) continue;
                        if (!first) {
                            out = styled_trim_end(out);
                            out.push_plain(usage_separator);
                        }
                        first = false;
                        // Compute path here (frozen tree has no usage_name); 差异清单 #30
                        // notes nested help [COMMAND]... that clap's lazy build omits.
                        const std::string child = subcommand_usage_name(sub, usage_name());
                        usage_renderer{sub, required_graph(sub), child}.write_usage_no_title(out,
                                                                                             {});
                    }
                    return;
                }
                write_arg_usage(out, {}, true);
                write_subcommand_usage(out);
            }

            constexpr void write_smart_usage(styled_str& out, std::span<const arg_id> used) const {
                write_arg_usage(out, used, true);
                if (!cmd_->is_subcommand_required_set()) return;
                write_subcommand_placeholder(out, '<', '>');
            }

            constexpr void
            write_arg_usage(styled_str& out, std::span<const arg_id> used, bool incl_reqs) const {
                const std::string_view bin = usage_name();
                if (!bin.empty()) {
                    out.push(style_class::literal, bin);
                    out.push_plain(" ");
                }
                if (used.empty() && needs_options_tag()) {
                    out.push(style_class::placeholder, "[OPTIONS]");
                    out.push_plain(" ");
                }
                write_args(out, used, !incl_reqs);
            }

            constexpr void write_subcommand_usage(styled_str& out) const {
                if (!cmd_->has_visible_subcommands() && !cmd_->is_allow_external_subcommands_set())
                    return;

                if (cmd_->is_subcommand_negates_reqs_set() ||
                    cmd_->is_args_conflicts_with_subcommands_set()) {
                    out = styled_trim_end(out);
                    out.push_plain(usage_separator);
                    if (cmd_->is_args_conflicts_with_subcommands_set()) {
                        const std::string_view bin = usage_name();
                        if (!bin.empty()) {
                            out.push(style_class::literal, bin);
                            out.push_plain(" ");
                        }
                    } else {
                        write_arg_usage(out, {}, false);
                    }
                    write_subcommand_placeholder(out, '<', '>');
                    return;
                }
                if (cmd_->is_subcommand_required_set()) {
                    write_subcommand_placeholder(out, '<', '>');
                    return;
                }
                write_subcommand_placeholder(out, '[', ']');
            }

            constexpr void
            write_subcommand_placeholder(styled_str& out, char open, char close) const {
                std::string text;
                text.push_back(open);
                append_bytes(
                        text,
                        cmd_->get_subcommand_value_name().value_or(default_subcommand_value_name));
                text.push_back(close);
                out.push(style_class::placeholder, text);
            }

            /** \brief Write required args/groups/positionals in clap order. */
            constexpr void
            write_args(styled_str& out, std::span<const arg_id> incls, bool force_optional) const {
                const std::vector<arg_id> wanted = concat_ids(unconditional_requirements(), incls);

                id_set group_members;
                std::vector<styled_str> group_forms;
                for (const arg_id& req : wanted) {
                    if (!cmd_->has_group(req.name())) continue;
                    for (const arg_id& member : unroll_args_in_group(*cmd_, req.name()))
                        group_members.insert(member);
                    push_unique_form(group_forms, format_group(*cmd_, req.name()));
                }

                std::vector<styled_str> opt_forms;
                std::vector<styled_str> positional_forms;
                for (const arg_id& req : wanted) {
                    if (!cmd_->has_arg(req.name())) continue;
                    const arg_spec& arg = *cmd_->find_arg(req.name());
                    if (group_members.contains(arg.get_id().name())) continue;

                    styled_str form = stylized_arg(arg, !force_optional);
                    if (const std::optional<std::size_t> index = arg.get_index();
                        index.has_value()) {
                        if (positional_forms.size() < *index + 1)
                            positional_forms.resize(*index + 1);
                        positional_forms[*index] = std::move(form);
                    } else {
                        push_unique_form(opt_forms, std::move(form));
                    }
                }

                for (const arg_spec& pos : cmd_->get_positionals()) {
                    if (pos.is_hide_set()) continue;
                    if (group_members.contains(pos.get_id().name())) continue;
                    const std::size_t index = pos.get_index().value_or(0);
                    if (positional_forms.size() < index + 1) positional_forms.resize(index + 1);

                    if (!positional_forms[index].empty()) {
                        if (pos.is_last_set()) {
                            styled_str fenced;
                            fenced.push(style_class::literal, "--");
                            fenced.push_plain(" ");
                            fenced.append(positional_forms[index]);
                            positional_forms[index] = std::move(fenced);
                        }
                    } else if (pos.is_last_set()) {
                        styled_str fenced;
                        fenced.push(style_class::literal, "[--");
                        fenced.push_plain(" ");
                        fenced.append(stylized_arg(pos, true));
                        fenced.push(style_class::literal, "]");
                        positional_forms[index] = std::move(fenced);
                    } else {
                        positional_forms[index] = stylized_arg(pos, false);
                    }

                    if (pos.is_last_set() && force_optional) positional_forms[index].clear();
                }

                if (!force_optional) {
                    for (const styled_str& form : opt_forms) {
                        out.append(form);
                        out.push_plain(" ");
                    }
                    for (const styled_str& form : group_forms) {
                        out.append(form);
                        out.push_plain(" ");
                    }
                }
                for (const styled_str& form : positional_forms) {
                    if (form.empty()) continue;
                    out.append(form);
                    out.push_plain(" ");
                }
            }

            const command_spec* cmd_ = nullptr;
            id_set required_{};
            std::string_view usage_name_{};  /**< Borrowed; empty asks #cmd_. */
        };

    }  // namespace detail

    // =======================================================================
    // The public seam
    // =======================================================================

    /**
     * \brief `Usage:` line for \p cmd.
     * \param cmd Command to describe.
     * \param used Empty = help usage; non-empty = smart usage (what this line still needs).
     * \param usage_name What the line calls \p cmd; empty asks \p cmd (root only).
     * \return The line, or nullopt when empty.
     *
     * \warning **\p usage_name is borrowed and must outlive the call.** Dangling yields
     *          garbage, not a diagnostic. Frozen command_spec has no path (ADR-0005);
     *          pass the path for subcommands (parse_engine threads it).
     */
    [[nodiscard]] constexpr std::optional<styled_str>
    render_usage(const command_spec& cmd,
                 std::span<const arg_id> used = {},
                 std::string_view usage_name  = {}) {
        return detail::usage_renderer{cmd, detail::required_graph(cmd), usage_name}
                .create_usage_with_title(used);
    }

    /**
     * \brief render_usage without the `Usage:` heading (`{usage}` template body).
     * \param cmd Command.
     * \param used See render_usage().
     * \param usage_name See render_usage() \warning.
     * \return Line body, or nullopt when empty.
     */
    [[nodiscard]] constexpr std::optional<styled_str>
    render_usage_body(const command_spec& cmd,
                      std::span<const arg_id> used = {},
                      std::string_view usage_name  = {}) {
        return detail::usage_renderer{cmd, detail::required_graph(cmd), usage_name}
                .create_usage_no_title(used);
    }

    namespace detail {

        /** \brief Compile-time fixtures that protect usage-rendering invariants. */
        namespace usage_contract {

            /** \brief Matcher stub with no parser dependency (proves the concept seam). */
            struct never_present {
                [[nodiscard]] static constexpr bool check_explicit(std::string_view,
                                                                   const arg_predicate&) noexcept {
                    return false;
                }
            };

            static_assert(arg_presence_source<never_present>,
                          "clapp: the usage renderer's matcher seam must be satisfiable "
                          "without <clapp/parser/arg_matcher.hpp>, or output depends on "
                          "parser and the module graph has a cycle.");

            inline constexpr arg_spec seam_args[] = {
                    arg_spec{.id       = arg_id{"out"},
                             .long_    = arg_id{"out"},
                             .settings = arg_flags{}.set(arg_setting::required)},
                    arg_spec{.id = arg_id{"tag"}, .long_ = arg_id{"tag"}},
                    arg_spec{.id       = arg_id{"src"},
                             .index    = 1,
                             .settings = arg_flags{}.set(arg_setting::required)},
            };
            inline constexpr command_spec seam_cmd{
                    .name = arg_id{"demo"}, .arg_data = seam_args, .arg_count = 3};

            // 1. render_usage() *is* usage_renderer, not a second implementation that
            //    happens to agree today. Comparing the styled_str values, not their
            //    to_string()s, also pins the fragment classes.
            consteval bool the_public_seam_is_the_renderer() {
                const std::optional<styled_str> direct =
                        usage_renderer{seam_cmd}.create_usage_with_title({});
                const std::optional<styled_str> seam = render_usage(seam_cmd);
                return direct.has_value() && seam.has_value() && *direct == *seam &&
                       seam->to_string() == "Usage: demo [OPTIONS] --out <out> <src>";
            }

            static_assert(the_public_seam_is_the_renderer());

            // 2. The heading is the *only* difference between the two entry points, and
            //    it carries style_class::usage. Every to_string() assertion in the tree
            //    is blind to the class, so it needs an assertion of its own.
            consteval bool the_heading_is_the_only_difference() {
                const styled_str titled = *render_usage(seam_cmd);
                const styled_str bare   = *render_usage_body(seam_cmd);
                std::string expected;
                append_bytes(expected, "Usage: ");
                append_bytes(expected, bare.to_string());
                return titled.text_of(style_class::usage) == "Usage:" &&
                       bare.text_of(style_class::usage).empty() && titled.to_string() == expected;
            }

            static_assert(the_heading_is_the_only_difference());

            // 3. The matcher-less get_required_usage_from() overload stays reachable
            //    from a constant expression. It is the one that clap spells `None`, and
            //    it is what subcommand_usage_name() calls — so if it ever stops being
            //    `constexpr`, every compile-time usage assertion in the tree turns into a
            //    runtime one without a single test failing.
            consteval bool the_matcher_less_overload_is_constexpr() {
                const usage_renderer renderer{seam_cmd};
                const std::vector<styled_str> forms = renderer.get_required_usage_from({}, true);
                return forms.size() == 2 && forms[0].to_string() == "--out <out>" &&
                       forms[1].to_string() == "<src>";
            }

            static_assert(the_matcher_less_overload_is_constexpr());

        }  // namespace usage_contract

    }  // namespace detail

}  // namespace clapp
