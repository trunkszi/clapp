/**
 * \file
 * \brief clapp::detail::validator — post-token-loop checks before matches are returned.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/context.hpp>
#include <clapp/error/error.hpp>
#include <clapp/output/help.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/output/usage.hpp>
#include <clapp/parser/arg_matcher.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/util/graph.hpp>
#include <clapp/util/id.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace clapp::detail {
    // ===================================================================
    // The requires relation, lifted into a graph
    // ===================================================================

    /**
     * \brief Unconditional `requires` as a digraph over `cmd.get_arguments()` indices.
     *
     * \tparam N Max argument count; surplus args contribute no edges.
     * \param cmd Command whose `requires` edges to lift.
     * \return Raw edge set; call `transitive_closure()` for the full relation.
     *
     * \note Compile-time half only — not what the validator runs (`requires_if` is
     *       match-dependent). Pins shape at compile time; runtime uses
     *       unroll_arg_requires(). No production caller by design (M3 chose per-parse).
     */
    template<std::size_t N>
    [[nodiscard]] constexpr digraph<N> requires_digraph(const command_spec &cmd) {
        digraph<N> graph;
        const std::span<const arg_spec> args = cmd.get_arguments();
        const std::size_t count = args.size() < N ? args.size() : N;
        for (std::size_t from = 0; from < count; ++from) {
            for (const requires_spec &rule: args[from].get_requires()) {
                if (!rule.when.is_present_only()) continue;
                for (std::size_t to = 0; to < count; ++to) {
                    if (args[to].get_id().name() != rule.target.name()) continue;
                    graph.add_edge(from, to);
                    break;
                }
            }
        }
        return graph;
    }

    // ===================================================================
    // Conflicts
    // ===================================================================

    /**
     * \brief Everything \p arg directly collides with. clap's `gather_arg_direct_conflicts`.
     *
     * \param cmd Command level being validated.
     * \param arg Argument to ask about.
     * \return Argument **and group** ids, possibly with duplicates.
     *
     * \note Three sources (dropping any fails silently): own `conflicts_with`;
     *       conflicts of every group it belongs to; other members of non-`multiple`
     *       groups (entire implementation of `ArgGroup::multiple(false)`).
     * \note `overrides_with` folded last (conflict that survived loop deletion).
     */
    [[nodiscard]] constexpr std::vector<arg_id>
    gather_arg_direct_conflicts(const command_spec &cmd, const arg_spec &arg) {
        std::vector<arg_id> conflicts;
        for (const arg_id &other: arg.get_conflicts()) conflicts.push_back(other);

        for (const std::string_view group_name: cmd.groups_for_arg(arg.get_id().name())) {
            if (!cmd.has_group(group_name)) continue;
            const group_spec &group = *cmd.find_group(group_name);
            for (const arg_id &other: group.get_conflicts()) conflicts.push_back(other);
            if (group.is_multiple()) continue;
            for (const arg_id &member: group.get_args()) {
                if (member.name() == arg.get_id().name()) continue;
                conflicts.push_back(member);
            }
        }

        for (const arg_id &other: arg.get_overrides()) conflicts.push_back(other);
        return conflicts;
    }

    /**
     * \brief Everything \p group directly collides with. clap's `gather_group_direct_conflicts`.
     * \param group Group to ask about.
     * \return Group's own `conflicts_with` only (does not inherit members' conflicts).
     */
    [[nodiscard]] constexpr std::vector<arg_id>
    gather_group_direct_conflicts(const group_spec &group) {
        return group.get_conflicts() | std::ranges::to<std::vector<arg_id> >();
    }

    /**
     * \brief Direct conflicts for argument or group \p id. clap's `gather_direct_conflicts`.
     * \param cmd Command level being validated.
     * \param id  Argument or group id.
     * \return Direct conflicts; empty for an unknown id.
     */
    [[nodiscard]] constexpr std::vector<arg_id> gather_direct_conflicts(const command_spec &cmd,
                                                                        std::string_view id) {
        if (cmd.has_arg(id)) return gather_arg_direct_conflicts(cmd, *cmd.find_arg(id));
        if (cmd.has_group(id)) return gather_group_direct_conflicts(*cmd.find_group(id));
        return {};
    }

    // ===================================================================
    // clap-compatible insertion order
    // ===================================================================

    /**
     * \brief Explicitly supplied ids (args **and** groups) in clap insertion order.
     *
     * Used by conflicts::with_args() and gather_requires() (group ids matter).
     * \param matcher Accumulated matches.
     * \return Ids ordered by saved insertion ordinals.
     */
    [[nodiscard]] inline std::vector<arg_id> supplied_ids_in_order(const arg_matcher &matcher) {
        std::vector<arg_id> ordered;
        for (const auto &[id, matched]: matcher.args()) {
            if (!matched.is_explicit()) continue;
            ordered.push_back(id);
        }
        std::ranges::stable_sort(ordered, {}, [&](const arg_id &id) {
            return matcher.matches().insertion_ordinal_of(id.name()).value_or(
                std::numeric_limits<std::size_t>::max());
        });
        return ordered;
    }

    /**
     * \brief Explicitly supplied arguments of \p cmd, in **command-line order**.
     *
     * supplied_ids_in_order() with group ids and foreign args dropped.
     * \param cmd     Command level being validated.
     * \param matcher Accumulated matches.
     * \return Argument ids of \p cmd, by insertion ordinal.
     *
     * \warning **Not a cosmetic ordering.** First id is the *subject* of a conflict
     *          message; alphabetical order makes `--all --delete` and
     *          `--delete --all` identical. Ordering by id would also make renaming
     *          an id silently change an unrelated error message.
     */
    [[nodiscard]] inline std::vector<arg_id> supplied_in_order(const command_spec &cmd,
                                                               const arg_matcher &matcher) {
        return supplied_ids_in_order(matcher) |
               std::views::filter([&](const arg_id &id) { return cmd.has_arg(id.name()); }) |
               std::ranges::to<std::vector>();
    }

    /**
     * \brief Conflict table for one parse. clap's `Conflicts`.
     *
     * Built once per validate() from supplied ids. Materialised so
     * gather_conflicts()'s second loop can find conflicts declared by the other side.
     */
    class conflicts {
    public:
        /** \brief One present id and everything it directly collides with. */
        struct entry {
            arg_id id{}; /**< Present argument or group. */
            std::vector<arg_id> direct{}; /**< Its direct conflicts. */
        };

        /** \brief Empty table. */
        conflicts() = default;

        /**
         * \brief Build from explicitly-supplied ids. clap's `Conflicts::with_args`.
         * \param cmd     Command level being validated.
         * \param matcher Accumulated matches.
         * \return The table.
         *
         * \note Defaults do not conflict; env values do (see is_explicit()).
         * \note **Row order is load-bearing** — walks supplied_ids_in_order(), not
         *       the sorted map, so collision report order matches command line.
         */
        [[nodiscard]] static conflicts with_args(const command_spec &cmd,
                                                 const arg_matcher &matcher) {
            conflicts table;
            for (const arg_id &id: supplied_ids_in_order(matcher))
                table.potential_.push_back(
                    entry{.id = id, .direct = gather_direct_conflicts(cmd, id.name())});
            return table;
        }

        /**
         * \brief Stored direct conflicts of \p id, if present.
         * \param id Argument or group id.
         * \return nullopt when not supplied (distinct from "supplied, conflicts with nothing").
         */
        [[nodiscard]] std::optional<std::span<const arg_id> >
        get_direct_conflicts(std::string_view id) const noexcept {
            for (const entry &one: potential_) {
                if (one.id.name() == id) return std::span<const arg_id>{one.direct};
            }
            return std::nullopt;
        }

        /**
         * \brief Every present id that collides with \p id. clap's `gather_conflicts`.
         * \param cmd Command level being validated.
         * \param id  Argument or group id to ask about.
         * \return Colliding ids; **may list an id twice** when both sides declared it.
         * \note \p id need not be present (is_missing_required_ok asks about missing).
         */
        [[nodiscard]] std::vector<arg_id> gather_conflicts(const command_spec &cmd,
                                                           std::string_view id) const {
            std::vector<arg_id> found;

            std::vector<arg_id> recomputed;
            std::span<const arg_id> direct;
            if (const std::optional<std::span<const arg_id> > stored = get_direct_conflicts(id);
                stored.has_value()) {
                direct = *stored;
            } else {
                recomputed = gather_direct_conflicts(cmd, id);
                direct = recomputed;
            }

            for (const entry &other: potential_) {
                if (other.id.name() == id) continue;
                if (contains_id(direct, other.id.name())) found.push_back(other.id);
                if (contains_id(other.direct, id)) found.push_back(other.id);
            }
            return found;
        }

        /** \brief The whole table, for tests and diagnostics. */
        [[nodiscard]] std::span<const entry> potential() const noexcept { return potential_; }

    private:
        std::vector<entry> potential_{};
    };

    /**
     * \brief Text an `arg_required_else_help` command shows when given nothing.
     *
     * \param cmd        Command that wanted an argument.
     * \param usage_name What `Usage:` calls \p cmd (full path); empty asks \p cmd.
     * \return Whole short help page (what `-h` prints), via render_help_for_terminal().
     *
     * \note Always short form (`use_long = false`): user asked for nothing, not `--help`.
     */
    [[nodiscard]] inline styled_str render_arg_required_help(const command_spec &cmd,
                                                             std::string_view usage_name) {
        return clapp::render_help_for_terminal(
            cmd, help_style{.use_long = false, .usage_name = usage_name});
    }

    // ===================================================================
    // The validator
    // ===================================================================

    /**
     * \brief clap's `Validator`: checks the token loop deliberately skipped.
     *
     * Construct from the parsed command_spec, then validate(matcher). Matcher is
     * not mutated; move into arg_matches afterwards.
     *
     * \code
     * clapp::detail::arg_matcher matcher{spec};
     * // ... run the parse loop ...
     * if (auto ok = clapp::detail::validator{spec}.validate(matcher); !ok)
     *     return std::unexpected(std::move(ok.error()));
     * clapp::arg_matches result = std::move(matcher).into_inner();
     * \endcode
     *
     * \note **Check order is part of the contract** (exclusive, then conflicts, then
     *       requirements; subcommand_required before either). Reordering changes
     *       which message fires, not whether the line is rejected.
     */
    class validator {
    public:
        /**
         * \brief Validator for \p cmd.
         * \param cmd        Command level parsed; must outlive the validator.
         * \param usage_name What `Usage:` calls \p cmd; empty asks \p cmd (root only).
         * \param bin_path   Plain path for `'…' requires a subcommand`; empty asks \p cmd.
         *
         * \warning Both are **borrowed views** and must outlive the validator.
         */
        explicit validator(const command_spec &cmd,
                           std::string_view usage_name = {},
                           std::string_view bin_path = {})
            : cmd_(&cmd), usage_name_(usage_name), bin_path_(bin_path) {
            required_ = required_graph(cmd);
        }

        /**
         * \brief Run every check. clap's `Validator::validate`.
         * \param matcher Accumulated matches.
         * \return Nothing, or the first failure.
         * \note `subcommand_negates_reqs` suppresses only requirements, not conflicts.
         */
        [[nodiscard]] std::expected<void, error> validate(const arg_matcher &matcher) {
            const conflicts table = conflicts::with_args(*cmd_, matcher);
            const bool has_sub = matcher.subcommand_name().has_value();

            if (!has_sub && cmd_->is_arg_required_else_help_set()) {
                std::size_t supplied = 0;
                for (const matched_arg &matched: matcher.args().values()) {
                    // Explicit, not merely recorded: clapp::command_builder gives
                    // every `set_true` flag and every `count` argument a default, so
                    // a command with either is never "empty" by presence alone.
                    if (matched.is_explicit()) ++supplied;
                }
                if (supplied == 0)
                    return std::unexpected(error::display_help_error(
                        render_arg_required_help(*cmd_, usage_name_)));
            }

            if (!has_sub && cmd_->is_subcommand_required_set()) {
                std::vector<cow_str> available =
                        cmd_->all_subcommand_names() |
                        std::views::transform(
                            [](std::string_view name) { return cow_str::borrowed(name); }) |
                        std::ranges::to<std::vector>();
                // clap quotes `bin_name`, not `usage_name`: the message reads
                // `'test sub' requires a subcommand`, with no flag forms and no
                // requirement fragments. Measured against clap_builder 4.6.5.
                // owned(), not borrowed(): bin_name() may be a view into the parse
                // engine's own string, and the error outlives the engine. Borrowing
                // it compiles, passes on the root (where the view names the frozen
                // spec) and dangles the moment a subcommand raises this — which is
                // exactly the shape clapp::cow_str exists to make visible.
                return std::unexpected(
                    error::missing_subcommand(cow_str::owned(bin_name()),
                                              std::move(available),
                                              usage(std::span<const arg_id>{})));
            }

            if (std::expected<void, error> checked = validate_conflicts(matcher, table);
                !checked.has_value())
                return checked;

            if (cmd_->is_subcommand_negates_reqs_set() && has_sub) return {};
            return validate_required(matcher, table);
        }

        /**
         * \brief Required set (grown as `requires` targets are found). For tests/usage.
         */
        [[nodiscard]] const id_set &required() const noexcept { return required_; }

    private:
        // -- conflicts ----------------------------------------------------

        /**
         * clap's `validate_conflicts`: exclusive first, then each supplied arg.
         * Group ids skipped; build_conflict_err unrolls them to members.
         */
        [[nodiscard]] std::expected<void, error> validate_conflicts(const arg_matcher &matcher,
                                                                    const conflicts &table) const {
            if (std::expected<void, error> checked = validate_exclusive(matcher);
                !checked.has_value())
                return checked;

            for (const arg_id &id: supplied_in_order(*cmd_, matcher)) {
                const std::vector<arg_id> against = table.gather_conflicts(*cmd_, id.name());
                if (std::expected<void, error> checked =
                            build_conflict_err(id.name(), against, matcher);
                    !checked.has_value())
                    return checked;
            }
            return {};
        }

        /** clap's `validate_exclusive`: an `exclusive` argument tolerates no company. */
        [[nodiscard]] std::expected<void, error>
        validate_exclusive(const arg_matcher &matcher) const {
            const std::vector<arg_id> supplied = supplied_in_order(*cmd_, matcher);
            if (supplied.size() <= 1) return {};

            for (const arg_id &id: supplied) {
                const arg_spec &arg = *cmd_->find_arg(id.name());
                if (!arg.is_exclusive_set()) continue;
                return std::unexpected(error::argument_conflict(
                    cow_str{stylized_arg(arg, std::nullopt).to_string()},
                    {},
                    usage(std::span<const arg_id>{})));
            }
            return {};
        }

        /** clap's `build_conflict_err`: name **both** sides, with groups unrolled. */
        [[nodiscard]] std::expected<void, error>
        build_conflict_err(std::string_view name,
                           std::span<const arg_id> against,
                           const arg_matcher &matcher) const {
            if (against.empty()) return {};

            id_set unrolled;
            for (const arg_id &one: against) {
                if (cmd_->has_group(one.name())) {
                    for (const arg_id &member: unroll_args_in_group(*cmd_, one.name()))
                        unrolled.insert(member);
                } else {
                    unrolled.insert(one);
                }
            }

            std::vector<cow_str> others;
            for (const arg_id &one: unrolled.ids()) {
                if (!cmd_->has_arg(one.name())) continue;
                others.push_back(cow_str{
                    stylized_arg(*cmd_->find_arg(one.name()), std::nullopt).to_string()
                });
            }
            if (!cmd_->has_arg(name)) return {};

            return std::unexpected(error::argument_conflict(
                cow_str{stylized_arg(*cmd_->find_arg(name), std::nullopt).to_string()},
                std::move(others),
                conflict_usage(matcher, unrolled.ids())));
        }

        /**
         * clap's `build_conflict_err_usage`: usage shows what the user had, minus
         * conflict args, plus what those still require.
         * \note `used` from supplied_in_order() so Usage echoes command-line order.
         */
        [[nodiscard]] std::optional<styled_str>
        conflict_usage(const arg_matcher &matcher, std::span<const arg_id> conflicting) const {
            std::vector<arg_id> used;
            for (const arg_id &id: supplied_in_order(*cmd_, matcher)) {
                if (cmd_->find_arg(id.name())->is_hide_set()) continue;
                if (contains_id(conflicting, id.name())) continue;
                used.push_back(id);
            }

            std::vector<arg_id> wanted;
            for (const arg_id &one: used) {
                if (!cmd_->has_arg(one.name())) continue;
                for (const requires_spec &rule: cmd_->find_arg(one.name())->get_requires()) {
                    if (contains_id(used, rule.target.name())) continue;
                    if (contains_id(conflicting, rule.target.name())) continue;
                    wanted.push_back(rule.target);
                }
            }
            for (const arg_id &one: used) wanted.push_back(one);
            return usage(wanted);
        }

        // -- requirements -------------------------------------------------

        /**
         * clap's `gather_requires`: fold typed args into the required set.
         * \note Walks supplied_ids_in_order() so required-list and Usage order match CLI.
         */
        void gather_requires(const arg_matcher &matcher) {
            for (const arg_id &id: supplied_ids_in_order(matcher)) {
                const matched_arg *found = matcher.args().find_value(id.name());
                if (found == nullptr) continue;
                const matched_arg &matched = *found;
                if (cmd_->has_arg(id.name())) {
                    const arg_spec &arg = *cmd_->find_arg(id.name());
                    const auto is_relevant = [&](const requires_spec &rule) {
                        if (!check_explicit(matched, rule.when)) return std::optional<arg_id>{};
                        return std::optional<arg_id>{rule.target};
                    };
                    for (const arg_id &target:
                         unroll_arg_requires(*cmd_, is_relevant, arg.get_id().name()))
                        required_.insert(target);
                } else if (cmd_->has_group(id.name())) {
                    for (const arg_id &target: cmd_->find_group(id.name())->get_requires())
                        required_.insert(target);
                }
            }
        }

        [[nodiscard]] std::expected<void, error> validate_required(const arg_matcher &matcher,
                                                                   const conflicts &table) {
            gather_requires(matcher);

            std::vector<arg_id> missing;
            std::size_t highest_index = 0;

            // The sorted map is fine here, and in the arg_required_else_help count
            // above: both fold to a single value that no permutation can change. Only
            // the loops that BUILD A LIST need supplied_in_order().
            bool exclusive_present = false;
            for (const auto &[id, matched]: matcher.args()) {
                if (!matched.is_explicit()) continue;
                if (!cmd_->has_arg(id.name())) continue;
                if (cmd_->find_arg(id.name())->is_exclusive_set()) exclusive_present = true;
            }

            const auto note_missing = [&](const arg_spec &arg) {
                missing.push_back(arg.get_id());
                if (arg.is_last_set()) return;
                const std::size_t index = arg.get_index().value_or(0);
                if (index > highest_index) highest_index = index;
            };

            // The unconditional half: `required()` arguments and groups.
            for (const arg_id &one: required_.ids()) {
                if (matcher.check_explicit(one.name(), arg_predicate::present())) continue;
                if (cmd_->has_arg(one.name())) {
                    const arg_spec &arg = *cmd_->find_arg(one.name());
                    if (exclusive_present) continue;
                    if (is_missing_required_ok(arg, table)) continue;
                    note_missing(arg);
                } else if (cmd_->has_group(one.name())) {
                    const group_spec &group = *cmd_->find_group(one.name());
                    const std::vector<arg_id> members =
                            unroll_args_in_group(*cmd_, group.get_id().name());
                    const bool occupied =
                            std::ranges::any_of(members, [&](const arg_id &member) {
                                return matcher.check_explicit(member.name(),
                                                              arg_predicate::present());
                            });
                    if (!occupied) missing.push_back(group.get_id());
                }
            }

            // The conditional half: required_if_eq*, required_unless_present*.
            for (const arg_spec &arg: cmd_->get_arguments()) {
                if (matcher.check_explicit(arg.get_id().name(), arg_predicate::present()))
                    continue;

                bool wanted = false;
                for (const required_if_spec &rule: arg.get_required_if_eq_any()) {
                    if (matcher.check_explicit(rule.id.name(), equals(rule.value)))
                        wanted = true;
                }
                const std::span<const required_if_spec> all = arg.get_required_if_eq_all();
                if (!all.empty() && std::ranges::all_of(all, [&](const required_if_spec &rule) {
                    return matcher.check_explicit(rule.id.name(), equals(rule.value));
                }))
                    wanted = true;

                const bool has_unless = !arg.get_required_unless_present_any().empty() ||
                                        !arg.get_required_unless_present_all().empty();
                if (has_unless && fails_required_unless(arg, matcher)) wanted = true;

                if (!wanted || exclusive_present) continue;
                note_missing(arg);
            }

            // For display only: name every earlier positional too, so the message
            // reads `<SRC> <DST>` rather than a lone `<DST>`.
            //
            // The `>=` is clap's `pos.get_index() < Some(highest_index)`. Relaxing it
            // to `>` is a proven-equivalent change and not a latent bug: the only
            // positional at exactly `highest_index` is the one whose note_missing()
            // set it, and that one is already in `missing`. Left as clap wrote it.
            if (!cmd_->is_allow_missing_positional_set()) {
                for (const arg_spec &pos: cmd_->get_positionals()) {
                    if (matcher.check_explicit(pos.get_id().name(), arg_predicate::present()))
                        continue;
                    if (pos.get_index().value_or(0) >= highest_index) continue;
                    missing.push_back(pos.get_id());
                }
            }

            if (missing.empty()) return {};
            return missing_required_error(matcher, std::move(missing));
        }

        /**
         * clap's `is_missing_required_ok`: a requirement that conflicts with
         * something the user *did* supply is excused rather than reported.
         */
        [[nodiscard]] bool is_missing_required_ok(const arg_spec &arg,
                                                  const conflicts &table) const {
            if (!table.gather_conflicts(*cmd_, arg.get_id().name()).empty()) return true;
            for (const std::string_view group: cmd_->groups_for_arg(arg.get_id().name())) {
                if (!table.gather_conflicts(*cmd_, group).empty()) return true;
            }
            return false;
        }

        /** clap's `fails_arg_required_unless`: the "unless" was not satisfied. */
        [[nodiscard]] static bool fails_required_unless(const arg_spec &arg,
                                                        const arg_matcher &matcher) {
            const auto exists = [&](const arg_id &id) {
                return matcher.check_explicit(id.name(), arg_predicate::present());
            };
            const std::span<const arg_id> all = arg.get_required_unless_present_all();
            const std::span<const arg_id> any = arg.get_required_unless_present_any();
            return (all.empty() || !std::ranges::all_of(all, exists)) &&
                   !std::ranges::any_of(any, exists);
        }

        [[nodiscard]] std::expected<void, error>
        missing_required_error(const arg_matcher &matcher,
                               std::vector<arg_id> raw_required) const {
            const usage_renderer renderer{*cmd_, required_, usage_name_};

            // No deduplication here, matching clap's `usage`-enabled path:
            // get_required_usage_from() already deduplicates options against each
            // other and groups against each other, and keys positionals by slot, so
            // a second pass could never remove anything. (clap's *no-usage* path
            // does collect into a `FlatSet`, because it renders from the raw id list
            // instead and really can repeat itself.)
            std::vector<cow_str> rendered =
                    renderer.get_required_usage_from(raw_required, matcher, true) |
                    std::views::transform(
                        [](const styled_str &one) { return cow_str{one.to_string()}; }) |
                    std::ranges::to<std::vector>();

            // supplied_in_order(), not `matcher.args()`: `used` is echoed back
            // verbatim as the tail of the `Usage:` line, so walking the sorted map
            // would make `--zulu --bravo --mike` and `--mike --bravo --zulu` produce
            // the identical suggestion `--bravo --mike --zulu`. clap prints each in
            // the order it was typed.
            std::vector<arg_id> used;
            for (const arg_id &id: supplied_in_order(*cmd_, matcher)) {
                if (cmd_->find_arg(id.name())->is_hide_set()) continue;
                used.push_back(id);
            }
            for (const arg_id &one: raw_required) used.push_back(one);

            return std::unexpected(error::missing_required_argument(
                std::move(rendered), renderer.create_usage_with_title(used)));
        }

        // -- helpers ------------------------------------------------------

        /**
         * The `arg_predicate` for `required_if_eq`, built at run time —
         * clapp::arg_predicate::equal_to() is `consteval` because it promotes its
         * text, and this value is already promoted.
         */
        [[nodiscard]] static constexpr arg_predicate equals(arg_id value) noexcept {
            return arg_predicate{.kind = predicate_kind::equals, .value = value};
        }

        [[nodiscard]] std::optional<styled_str> usage(std::span<const arg_id> used) const {
            return usage_renderer{*cmd_, required_, usage_name_}.create_usage_with_title(used);
        }

        /** clap's `Command::get_bin_name_fallback`. */
        [[nodiscard]] std::string_view bin_name() const noexcept {
            if (!bin_path_.empty()) return bin_path_;
            return cmd_->get_bin_name().value_or(cmd_->get_name());
        }

        const command_spec *cmd_ = nullptr;
        id_set required_{};
        /** Borrowed; owned by clapp::detail::parse_engine. See the constructor. */
        std::string_view usage_name_{};
        std::string_view bin_path_{};
    };

    /**
     * \brief Validate \p matcher against \p cmd. clap's end of `get_matches_with`.
     * \param cmd        Command level that was parsed.
     * \param matcher    Accumulated matches.
     * \param usage_name What `Usage:` calls \p cmd; empty asks \p cmd.
     * \param bin_path   Plain path to \p cmd; empty asks \p cmd.
     * \return Nothing, or the first failure.
     */
    [[nodiscard]] inline std::expected<void, error> validate(const command_spec &cmd,
                                                             const arg_matcher &matcher,
                                                             std::string_view usage_name = {},
                                                             std::string_view bin_path = {}) {
        return validator{cmd, usage_name, bin_path}.validate(matcher);
    }

    // ===================================================================
    // Compile-time contracts
    // ===================================================================

    // Everything the validator delegates is a pure question about a frozen tree, so
    // it is asserted here rather than reported by a runtime case. The fixtures are
    // built from string *literals* throughout: under `-fsanitize=null` GCC 16.1.0
    // will not fold libstdc++'s `basic_string(const CharT*, size_type)` when the
    // source pointer is a variable. CLAUDE.md trap 10.

    /** \brief Compile-time fixtures that protect validator invariants. */
    namespace validator_contract {
        inline constexpr arg_id member_ids[] = {arg_id{"json"}, arg_id{"yaml"}};
        inline constexpr group_spec format_group_spec{
            .id = arg_id{"format"},
            .arg_data = member_ids,
            .arg_count = 2,
            .required = true,
            .multiple = false
        };
        inline constexpr arg_spec format_args[] = {
            arg_spec{
                .id = arg_id{"json"},
                .long_ = arg_id{"json"},
                .act = arg_action::set_true,
                .num_args = value_range::empty()
            },
            arg_spec{
                .id = arg_id{"yaml"},
                .long_ = arg_id{"yaml"},
                .act = arg_action::set_true,
                .num_args = value_range::empty()
            },
        };
        inline constexpr command_spec format_cmd{
            .name = arg_id{"demo"},
            .arg_data = format_args,
            .arg_count = 2,
            .group_data = &format_group_spec,
            .group_count = 1
        };

        // A group that admits one member makes its members conflict with each other.
        // That is the ENTIRE implementation of ArgGroup::multiple(false); deleting
        // the `is_multiple()` test below leaves every other check green.
        consteval bool a_single_valued_group_makes_its_members_conflict() {
            const std::vector<arg_id> json = gather_direct_conflicts(format_cmd, "json");
            const std::vector<arg_id> yaml = gather_direct_conflicts(format_cmd, "yaml");
            return json.size() == 1 && json.front().name() == "yaml" && yaml.size() == 1 &&
                   yaml.front().name() == "json";
        }

        static_assert(a_single_valued_group_makes_its_members_conflict());

        consteval bool a_required_group_is_in_the_required_graph() {
            const id_set required = required_graph(format_cmd);
            return required.size() == 1 && required.contains("format") &&
                   !required.contains("json");
        }

        static_assert(a_required_group_is_in_the_required_graph());

        consteval bool a_group_renders_its_members_spellings() {
            constexpr std::array checks{
                    format_group(format_cmd, "format").to_string() == "<--json|--yaml>",
                    format_group(format_cmd, "nope").to_string() == "<>"
            };
            return std::ranges::all_of(checks, std::identity{});
        }

        static_assert(a_group_renders_its_members_spellings());

        // The `requires` chain: a -> b -> c, all unconditional.
        inline constexpr requires_spec a_requires[] = {requires_spec{.target = arg_id{"b"}}};
        inline constexpr requires_spec b_requires[] = {requires_spec{.target = arg_id{"c"}}};
        inline constexpr arg_spec chain_args[] = {
            arg_spec{
                .id = arg_id{"a"},
                .long_ = arg_id{"a"},
                .requires_data = a_requires,
                .requires_count = 1
            },
            arg_spec{
                .id = arg_id{"b"},
                .long_ = arg_id{"b"},
                .requires_data = b_requires,
                .requires_count = 1
            },
            arg_spec{.id = arg_id{"c"}, .long_ = arg_id{"c"}},
        };
        inline constexpr command_spec chain_cmd{
            .name = arg_id{"demo"}, .arg_data = chain_args, .arg_count = 3
        };

        // `a requires b` and `b requires c` together mean `a requires c`. An
        // implementation that reads only the edge list of `a` passes every
        // single-level test and silently stops demanding `--c`.
        consteval bool requires_is_transitive() {
            const std::vector<arg_id> from_a =
                    unroll_arg_requires(chain_cmd, unconditional_requirement, "a");
            const std::array checks{
                    from_a.size() == 2,
                    contains_id(from_a, "b"),
                    contains_id(from_a, "c"),
                    !contains_id(from_a, "a"),
                    unroll_arg_requires(chain_cmd, unconditional_requirement, "c").empty()
            };
            return std::ranges::all_of(checks, std::identity{});
        }

        static_assert(requires_is_transitive());

        // The same relation, through <clapp/util/graph.hpp>. Two independent
        // implementations of one closure: if they ever disagree, one is wrong.
        consteval bool the_graph_closure_agrees_with_the_walk() {
            constexpr digraph<3> closed = requires_digraph<3>(chain_cmd).transitive_closure();
            constexpr std::array checks{closed.has_edge(0, 1),
                                    closed.has_edge(1, 2),
                                    closed.has_edge(0, 2),
                                    !closed.has_edge(2, 0),
                                    !closed.has_edge(0, 0),
                                    closed.edge_count() == 3};
            return std::ranges::all_of(checks, std::identity{});
        }

        static_assert(the_graph_closure_agrees_with_the_walk());

        // A cycle terminates and reports the arguments on it, matching both clap's
        // worklist and clapp::digraph::transitive_closure().
        inline constexpr requires_spec x_requires[] = {requires_spec{.target = arg_id{"y"}}};
        inline constexpr requires_spec y_requires[] = {requires_spec{.target = arg_id{"x"}}};
        inline constexpr arg_spec cycle_args[] = {
            arg_spec{
                .id = arg_id{"x"},
                .long_ = arg_id{"x"},
                .requires_data = x_requires,
                .requires_count = 1
            },
            arg_spec{
                .id = arg_id{"y"},
                .long_ = arg_id{"y"},
                .requires_data = y_requires,
                .requires_count = 1
            },
        };
        inline constexpr command_spec cycle_cmd{
            .name = arg_id{"demo"}, .arg_data = cycle_args, .arg_count = 2
        };

        consteval bool a_requires_cycle_terminates() {
            const std::vector<arg_id> from_x =
                    unroll_arg_requires(cycle_cmd, unconditional_requirement, "x");
            const std::array checks{
                    contains_id(from_x, "y"),
                    contains_id(from_x, "x"),
                    requires_digraph<2>(cycle_cmd).transitive_closure().self_reachable(0)
            };
            return std::ranges::all_of(checks, std::identity{});
        }

        static_assert(a_requires_cycle_terminates());

        // The usage line is what tells the user what to type instead. `--out` is
        // required, so it is rendered with `<>`; `--tag` is not, so it hides behind
        // `[OPTIONS]`.
        inline constexpr arg_spec usage_args[] = {
            arg_spec{
                .id = arg_id{"out"},
                .long_ = arg_id{"out"},
                .settings = arg_flags{}.set(arg_setting::required)
            },
            arg_spec{.id = arg_id{"tag"}, .long_ = arg_id{"tag"}},
            arg_spec{
                .id = arg_id{"src"},
                .index = 1,
                .settings = arg_flags{}.set(arg_setting::required)
            },
        };
        inline constexpr command_spec usage_cmd{
            .name = arg_id{"demo"}, .arg_data = usage_args, .arg_count = 3
        };

        consteval bool the_usage_line_names_what_is_required() {
            const std::optional<styled_str> line =
                    usage_renderer{usage_cmd}.create_usage_with_title({});
            return line.has_value() &&
                   line->to_string() == "Usage: demo [OPTIONS] --out <out> <src>";
        }

        static_assert(the_usage_line_names_what_is_required());

        // stylized_arg(a, nullopt) is clap's `Display for Arg`, i.e. exactly
        // clapp::detail::arg_display() in parse.hpp; the `required` override is the
        // only difference and it is what a usage line needs.
        consteval bool the_required_override_changes_the_brackets() {
            constexpr arg_spec optional_positional{.id = arg_id{"src"}, .index = 1};
            constexpr std::array checks{
                    stylized_arg(optional_positional, std::nullopt).to_string() == "[src]",
                    stylized_arg(optional_positional, true).to_string() == "<src>",
                    stylized_arg(optional_positional, false).to_string() == "[src]"
            };
            return std::ranges::all_of(checks, std::identity{});
        }

        static_assert(the_required_override_changes_the_brackets());

        consteval bool trimming_drops_only_the_tail() {
            styled_str padded;
            padded.push(style_class::literal, "demo");
            padded.push_plain("   ");
            const std::array checks{styled_trim_end(padded).to_string() == "demo",
                                    styled_trim_end(styled_str{"   "}).empty()};
            return std::ranges::all_of(checks, std::identity{});
        }

        static_assert(trimming_drops_only_the_tail());
    } // namespace validator_contract
} // namespace clapp::detail
