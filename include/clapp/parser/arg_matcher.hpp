/**
 * \file
 * \brief clapp::detail::arg_matcher — mutable accumulator the parse loop drives —
 *        and pending_arg, the half-consumed argument it holds.
 */

#pragma once

#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/any_value.hpp>
#include <clapp/util/id.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp::detail {
    /**
     * \brief How the argument now collecting values was named. clap's `Identifier`.
     *
     * Defined here so pending_arg (and this header) is self-contained; parse.hpp
     * uses this spelling rather than declaring its own.
     *
     * \note Survives into diagnostics: `--flag` vs `-f` need different text; a
     *       positional has no spelling to quote.
     */
    enum class arg_identifier : std::uint8_t {
        short_, /**< Matched as `-f`. clap: `Identifier::Short`. */
        long_, /**< Matched as `--flag`. clap: `Identifier::Long`. */
        index, /**< Matched by position. clap: `Identifier::Index`. */
    };

    /** \brief How many identifier kinds there are. */
    inline constexpr std::size_t arg_identifier_count = 3;

    /** \brief Every identifier kind, in declaration order. */
    inline constexpr std::array<arg_identifier, arg_identifier_count> all_arg_identifiers{
        arg_identifier::short_,
        arg_identifier::long_,
        arg_identifier::index,
    };

    /**
     * \brief Stable kebab-case name of \p which.
     * \param which Identifier kind to name.
     * \return Empty view for a value outside the enumeration.
     */
    [[nodiscard]] constexpr std::string_view name_of(arg_identifier which) noexcept {
        switch (which) {
            case arg_identifier::short_:
                return "short";
            case arg_identifier::long_:
                return "long";
            case arg_identifier::index:
                return "index";
        }
        return {};
    }

    /**
     * \brief Whether \p which names the argument rather than positioning it.
     * \param which Identifier kind to test.
     * \return `false` only for arg_identifier::index.
     */
    [[nodiscard]] constexpr bool is_named(arg_identifier which) noexcept {
        return which != arg_identifier::index;
    }

    /**
     * \brief Argument recognized but still collecting raw values. clap's `PendingArg`.
     *
     * At most one at a time: the loop resolves (parse, commit, arity check) before
     * recognizing another. take_pending() is that hand-off.
     *
     * Raw bytes, not parsed values: occurrence well-formedness is known only when
     * complete (`--point 1` for `num_args(2)` must quote what the user typed), and
     * eager parse can process values a later token disowns.
     *
     * \note Usable in constant expressions (no any_value); transitions use `static_assert`.
     *
     * \warning #id borrows its name (as arg_id always does). Frozen command_spec ids
     *          live in `.rodata` and never dangle; an id from a dying `std::string`
     *          would, silently.
     */
    struct pending_arg {
        /** Which argument is collecting. Borrowed; see the class \warning. */
        arg_id id{};
        /** How it was named, when it was named at all. */
        std::optional<arg_identifier> ident{};
        /** The bytes collected so far, in order, unparsed. */
        std::vector<os_string> raw_values{};
        /** Index into #raw_values of the first value that followed a `--`, if any. */
        std::optional<std::size_t> trailing_index{};

        /** \brief Append \p value to #raw_values. */
        constexpr void push(os_string value) { raw_values.push_back(std::move(value)); }

        /**
         * \brief Record that everything from here on is trailing.
         *
         * First call wins (clap's `get_or_insert`); a second `--` does not move the boundary.
         * \note Idempotent so the loop may call it on every token after `--`.
         */
        constexpr void mark_trailing() noexcept {
            if (!trailing_index.has_value()) trailing_index = raw_values.size();
        }

        /** \brief Whether a `--` boundary has been recorded. */
        [[nodiscard]] constexpr bool has_trailing() const noexcept {
            return trailing_index.has_value();
        }

        /**
         * \brief How many raw values have been collected.
         * \note What needs_more_vals() tests; why pending exists vs writing matched_arg.
         */
        [[nodiscard]] constexpr std::size_t size() const noexcept { return raw_values.size(); }

        /**
         * \brief Whether nothing has been collected yet.
         * \note Empty pending is meaningful: `--opt` with value still to come.
         */
        [[nodiscard]] constexpr bool empty() const noexcept { return raw_values.empty(); }

        /**
         * \brief Collected bytes.
         * \return Span invalidated by the next push().
         */
        [[nodiscard]] constexpr std::span<const os_string> values() const noexcept {
            return raw_values;
        }

        /**
         * \brief Values collected before the `--` boundary.
         * \return Everything when no boundary was recorded.
         * \warning Span invalidated by the next push().
         */
        [[nodiscard]] constexpr std::span<const os_string> leading_values() const noexcept {
            const std::span<const os_string> all{raw_values};
            return all.first(trailing_index.value_or(raw_values.size()));
        }

        /**
         * \brief Values at or after the `--` boundary.
         * \return Empty span when no boundary was recorded.
         * \note After `--`, a leading `-` is data, never a flag.
         * \warning Span invalidated by the next push().
         */
        [[nodiscard]] constexpr std::span<const os_string> trailing_values() const noexcept {
            const std::span<const os_string> all{raw_values};
            return all.subspan(trailing_index.value_or(raw_values.size()));
        }

        /** \brief Member-wise equality; raw values compared byte for byte. */
        [[nodiscard]] constexpr bool operator==(const pending_arg &other) const {
            return id == other.id && ident == other.ident &&
                   trailing_index == other.trailing_index &&
                   std::ranges::equal(raw_values,
                                      other.raw_values,
                                      [](const os_string &a, const os_string &b) {
                                          return a.view() == b.view();
                                      });
        }
    };

    /**
     * \brief Abort because two different arguments are pending at once.
     *
     * clap's `debug_assert` only — release silently merges. clapp reports in every
     * preset (release must not answer a different question than debug).
     *
     * \param held   Id already collecting.
     * \param wanted Id the caller asked for.
     *
     * \note `std::fwrite` (invariant already broken). **`[[noreturn]]`, not
     *       `contract_assert`** — clang-p2996 has no P2900. See ADR-0008.
     */
    [[noreturn]] inline void report_pending_conflict(std::string_view held,
                                                     std::string_view wanted) {
        const auto put = [](std::string_view text) {
            (void) std::fwrite(text.data(), 1, text.size(), stderr);
        };
        put("clapp: `");
        put(wanted);
        put("` began collecting values while `");
        put(held);
        put("` was still pending -- the parse loop must resolve one argument before it "
            "starts the next. This is a bug in clapp, not in its input.\n");
        std::abort();
    }

    /**
     * \brief Abort because one pending argument was named two different ways.
     *
     * clap's second `debug_assert` in `pending_values_mut`. Merging spellings would
     * record the wrong identifier and quote the wrong flag in diagnostics.
     *
     * \param id     Pending argument's id.
     * \param held   How it was first named.
     * \param wanted How it has now been named.
     */
    [[noreturn]] inline void report_pending_identifier_conflict(std::string_view id,
                                                                std::string_view held,
                                                                std::string_view wanted) {
        const auto put = [](std::string_view text) {
            (void) std::fwrite(text.data(), 1, text.size(), stderr);
        };
        put("clapp: `");
        put(id);
        put("` is pending as a ");
        put(held);
        put(" argument but was continued as a ");
        put(wanted);
        put(" one -- the parse loop must resolve one spelling before it starts another. "
            "This is a bug in clapp, not in its input.\n");
        std::abort();
    }

    /**
     * \brief Whether \p matched satisfies \p predicate, ignoring defaults.
     *        clap's `MatchedArg::check_explicit`.
     *
     * \param matched   Accumulated state of one argument.
     * \param predicate What to ask about it.
     * \return `false` when only a `default_value` recorded the argument.
     *
     * \note Env values count as explicit (see is_explicit). "Explicit" = not a default.
     * \note equals arm uses has_raw_value(), which folds case for `ignore_case`; clap
     *       compares bytes even then. Deliberate: `required_if_eq("mode","fast")` should
     *       fire after `--mode FAST`. **Sole home of that divergence** — member form
     *       delegates here.
     */
    [[nodiscard]] inline bool check_explicit(const matched_arg &matched,
                                             const arg_predicate &predicate) {
        if (!matched.is_explicit()) return false;
        if (predicate.is_present_only()) return true;
        return matched.has_raw_value(os_str{predicate.value.name()});
    }

    /**
     * \brief Parse loop working state: arg_matches being built plus pending argument.
     *
     * clap's `ArgMatcher`. Construct from command_spec, drive with mutators, move out
     * via into_inner(). Not reusable afterwards.
     *
     * \code
     * clapp::detail::arg_matcher matcher{spec};
     * matcher.start_custom_arg(*spec.find_arg("port"), clapp::value_source::command_line);
     * matcher.push_pending_value(clapp::arg_id{"port"}, clapp::os_string{"8080"},
     *                            clapp::detail::arg_identifier::long_);
     * // resolve pending, then: clapp::arg_matches r = std::move(matcher).into_inner();
     * \endcode
     *
     * \warning **Resolve pending before recognizing another id/spelling.** Second open
     *          aborts; guard with pending_is(). See report_pending_conflict.
     */
    class arg_matcher {
    public:
        /**
         * \brief Empty matcher with no id validation.
         * \note For tests / callers without a command tree. Parse loop uses the other ctor.
         */
        arg_matcher() = default;

        /**
         * \brief Matcher for \p cmd, validation seeded from it. clap's `ArgMatcher::new`.
         * \param cmd Command being parsed.
         * \note clap seeds only under `debug_assertions`; clapp always does.
         */
        explicit arg_matcher(const command_spec &cmd) {
            std::vector<arg_id> ids;
            ids.reserve(cmd.get_arguments().size() + cmd.get_groups().size());
            ids.append_range(cmd.get_arguments() | std::views::transform(&arg_spec::get_id));
            ids.append_range(cmd.get_groups() | std::views::transform(&group_spec::get_id));
            matches_.set_valid_ids(std::move(ids));

            matches_.set_valid_subcommands(cmd.get_subcommands() |
                                           std::views::transform([](const command_spec &one) {
                                               return std::string{one.get_name()};
                                           }) |
                                           std::ranges::to<std::vector>());
        }

        // -------------------------------------------------------------------
        // The transition into clapp::arg_matches
        // -------------------------------------------------------------------

        /**
         * \brief Matches built so far.
         * \return Reference valid while this matcher lives.
         */
        [[nodiscard]] const arg_matches &matches() const noexcept { return matches_; }

        /** \copydoc matches() const */
        [[nodiscard]] arg_matches &matches() noexcept { return matches_; }

        /**
         * \brief Move the finished matches out. clap's `into_inner`.
         * \return Accumulated arg_matches.
         *
         * \warning **A still-pending argument is discarded** (as clap). Values the user
         *          typed are lost with no diagnostic. Ask has_pending() first if you
         *          are not the parse loop.
         */
        [[nodiscard]] arg_matches into_inner() && { return std::move(matches_); }

        // -------------------------------------------------------------------
        // The map, i.e. what clap reaches through Deref
        // -------------------------------------------------------------------

        /**
         * \brief Accumulated state for \p id. clap's `get`.
         * \param id Argument or group id.
         * \return Pointer to state, or nullptr if none yet.
         * \note Pointer-as-absent is safe (not consteval); contains() is the predicate.
         */
        [[nodiscard]] const matched_arg *get(std::string_view id) const noexcept {
            return matches_.find_arg(id);
        }

        /** \copydoc get(std::string_view) const */
        [[nodiscard]] matched_arg *get(std::string_view id) noexcept {
            return matches_.find_arg(id);
        }

        /**
         * \brief Whether anything has been recorded for \p id. clap's `contains`.
         * \param id Argument or group id.
         */
        [[nodiscard]] bool contains(std::string_view id) const noexcept {
            return matches_.find_arg(id) != nullptr;
        }

        /**
         * \brief Drop everything recorded for \p id. clap's `remove`.
         *
         * Used by `overrides_with`: overridden arg is removed, absent from ids() after.
         * \param id Argument or group id.
         * \return Whether anything was recorded for it.
         */
        bool remove(std::string_view id) { return matches_.erase_arg(id); }

        /**
         * \brief State for \p id, creating empty if absent. clap's `entry`.
         * \param id Argument or group id.
         * \return Reference valid until the next insertion.
         * \note Not `[[nodiscard]]`: discarding to ensure id exists is valid use.
         * \warning Created entry has **no declared type and no `ignore_case`**. Prefer
         *          start_custom_arg() when an arg_spec is available.
         */
        matched_arg &entry(arg_id id) { return matches_.entry(id); }

        /** \brief Every id recorded so far, in name order. clap's `arg_ids`. */
        [[nodiscard]] auto arg_ids() const noexcept { return matches_.ids(); }

        /** \brief Whole id-to-state map. clap's `args`. */
        [[nodiscard]] const arg_matches::arg_map &args() const noexcept { return matches_.args(); }

        /** \brief How many ids have been recorded. */
        [[nodiscard]] std::size_t arg_count() const noexcept { return matches_.arg_count(); }

        /**
         * \brief Whether nothing has been recorded at all.
         * \note Says nothing about the pending argument; ask has_pending().
         */
        [[nodiscard]] bool empty() const noexcept { return matches_.empty(); }

        // -------------------------------------------------------------------
        // Subcommands
        // -------------------------------------------------------------------

        /**
         * \brief Record that \p name ran with \p child. clap's `subcommand`.
         * \param name  Subcommand name as declared.
         * \param child Child result, moved in.
         */
        void set_subcommand(std::string name, arg_matches child) {
            matches_.set_subcommand(std::move(name), std::move(child));
        }

        /**
         * \brief Name of the subcommand that ran. clap's `subcommand_name`.
         * \return nullopt when none has been recorded.
         */
        [[nodiscard]] std::optional<std::string_view> subcommand_name() const noexcept {
            return matches_.subcommand_name();
        }

        /** \brief Whether a subcommand has been recorded. */
        [[nodiscard]] bool has_subcommand() const noexcept { return matches_.has_subcommand(); }

        // -------------------------------------------------------------------
        // Predicates the validator asks
        // -------------------------------------------------------------------

        /**
         * \brief Whether \p id satisfies \p predicate, ignoring defaults.
         *        clap's `ArgMatcher::check_explicit`.
         *
         * \param id        Argument or group id.
         * \param predicate What to ask about it.
         * \return `false` when never recorded, or only via `default_value`.
         *
         * \note Free check_explicit() plus lookup; holds the `ignore_case` divergence.
         */
        [[nodiscard]] bool check_explicit(std::string_view id,
                                          const arg_predicate &predicate) const {
            const matched_arg *found = matches_.find_arg(id);
            return found != nullptr && detail::check_explicit(*found, predicate);
        }

        // -------------------------------------------------------------------
        // Starting an occurrence
        // -------------------------------------------------------------------

        /**
         * \brief Begin an occurrence of \p arg attributed to \p source.
         *        clap's `start_custom_arg`.
         *
         * Creates matched_arg on first sighting, merges \p source (see strongest()),
         * opens a fresh occurrence.
         *
         * \param arg           Argument's frozen spec.
         * \param source        Where this occurrence came from.
         * \param declared_type Type the value_parser produces, when known.
         * \return Reference to state, valid until next insertion.
         *
         * \note \p declared_type has no clap counterpart (clap reads type_id on the spot).
         *       Frozen arg_spec exposes type only as a name; any_id has no name ctor
         *       (trap 11). Defaulted is safe: infer_type_id() uses stored values.
         * \note `ignore_case` is carried from \p arg.
         */
        matched_arg &start_custom_arg(const arg_spec &arg,
                                      clapp::value_source source,
                                      any_id declared_type = any_id{}) {
            matched_arg *existing = matches_.find_arg(arg.get_id().name());
            if (existing == nullptr) {
                matched_arg fresh{declared_type};
                fresh.set_ignore_case(arg.is_ignore_case_set());
                existing = std::addressof(matches_.insert_arg(arg.get_id(), std::move(fresh)));
            }
            existing->set_source(source);
            existing->start_occurrence();
            return *existing;
        }

        /**
         * \brief Begin an occurrence of an arg_group. clap's `start_custom_group`.
         *
         * No value parser of its own; type inferred from members.
         * \param id     Group id.
         * \param source Where this occurrence came from.
         * \return Reference to state, valid until next insertion.
         */
        matched_arg &start_custom_group(arg_id id, clapp::value_source source) {
            matched_arg *existing = matches_.find_arg(id.name());
            if (existing == nullptr)
                existing = std::addressof(matches_.insert_arg(id, matched_arg::for_group()));
            existing->set_source(source);
            existing->start_occurrence();
            return *existing;
        }

        /**
         * \brief Begin occurrence collecting external subcommand args.
         *        clap's `start_occurrence_of_external`.
         *
         * Id is external_id (empty name); source is always command_line.
         * \param declared_type Type the external value parser produces, when known.
         * \return Reference to state, valid until next insertion.
         */
        matched_arg &start_occurrence_of_external(any_id declared_type = any_id{}) {
            matched_arg *existing = matches_.find_arg(external_id.name());
            if (existing == nullptr)
                existing = std::addressof(
                    matches_.insert_arg(external_id, matched_arg{declared_type}));
            existing->set_source(clapp::value_source::command_line);
            existing->start_occurrence();
            return *existing;
        }

        /**
         * \brief Begin a command-line occurrence of \p arg.
         *
         * clap 3's `inc_occurrence_of_arg` (clap 4 folded into start_custom_arg).
         * \param arg           Argument's frozen spec.
         * \param declared_type See start_custom_arg().
         * \return Reference to state, valid until next insertion.
         * \note Increments even with no values (`set_true` / `count`).
         */
        matched_arg &start_occurrence_of(const arg_spec &arg, any_id declared_type = any_id{}) {
            return start_custom_arg(arg, clapp::value_source::command_line, declared_type);
        }

        // -------------------------------------------------------------------
        // Committing values
        // -------------------------------------------------------------------

        /**
         * \brief Commit one parsed value with its raw bytes to \p id. clap's `add_val_to`.
         * \param id    Argument or group id.
         * \param value Parsed value from value_parser.
         * \param raw   Original command-line bytes.
         * \note clap panics if no entry; clapp creates one (no declared type/ignore_case).
         */
        void add_val_to(arg_id id, any_value value, os_string raw) {
            matches_.entry(id).append_value(std::move(value), std::move(raw));
        }

        /**
         * \brief Record that a value of \p id occupied position \p index. clap's `add_index_to`.
         * \param id    Argument or group id.
         * \param index Clap-style index; see arg_matches::index_of().
         */
        void add_index_to(arg_id id, std::size_t index) { matches_.entry(id).push_index(index); }

        // -------------------------------------------------------------------
        // Arity — the question the loop asks on every token
        // -------------------------------------------------------------------

        /**
         * \brief Whether the occurrence in flight may still take another value.
         *        clap's `needs_more_vals`.
         *
         * \param arg Argument's frozen spec.
         * \return `true` when \p arg is collecting and `num_args` admits one more.
         *
         * \warning **Count comes from pending values, not committed ones.**
         *          value_count_in_last_occurrence() is 0 while in flight; substituting
         *          it makes `num_args(2..)` stop after zero and hand rest to positionals.
         *          Parse still succeeds with values on the wrong argument.
         * \note value_range::infer() is resolved via default_num_args() first; an
         *       unresolved range answers "takes no values" (option becomes a flag).
         */
        [[nodiscard]] bool needs_more_vals(const arg_spec &arg) const noexcept {
            const std::size_t collected = pending_is(arg.get_id().name()) ? pending_->size() : 0;
            const value_range expected =
                    arg.get_num_args().resolve_or(default_num_args(arg.get_action()));
            return expected.accepts_more(collected);
        }

        // -------------------------------------------------------------------
        // The pending argument
        // -------------------------------------------------------------------

        /** \brief Whether some argument is currently collecting values. */
        [[nodiscard]] bool has_pending() const noexcept { return pending_.has_value(); }

        /**
         * \brief Whether \p id is currently collecting values.
         * \param id Argument id to test.
         * \return `false` when nothing is pending.
         * \note Guard that keeps pending_values() from aborting.
         */
        [[nodiscard]] bool pending_is(std::string_view id) const noexcept {
            return pending_.has_value() && pending_->id == id;
        }

        /**
         * \brief Which argument is currently collecting. clap's `pending_arg_id`.
         * \return nullopt when nothing is pending.
         * \note optional, not empty arg_id: empty id is external_id, a real argument.
         */
        [[nodiscard]] std::optional<arg_id> pending_arg_id() const noexcept {
            if (!pending_.has_value()) return std::nullopt;
            return pending_->id;
        }

        /**
         * \brief How the pending argument was named.
         * \return nullopt when nothing pending or recognized without a spelling.
         */
        [[nodiscard]] std::optional<arg_identifier> pending_identifier() const noexcept {
            if (!pending_.has_value()) return std::nullopt;
            return pending_->ident;
        }

        /**
         * \brief Values the pending argument has collected, without taking them.
         * \return Empty span when nothing is pending.
         *
         * \warning **Invalidated by push_pending_value(), pending_values(), and
         *          take_pending().** First two may reallocate; third moves the buffer
         *          out. ASan heap-use-after-free on two successive pushes. Read before
         *          touching the matcher again.
         */
        [[nodiscard]] std::span<const os_string> peek_pending_values() const noexcept {
            if (!pending_.has_value()) return {};
            return pending_->values();
        }

        /**
         * \brief How many values the pending argument has collected.
         * \return `0` when nothing is pending.
         */
        [[nodiscard]] std::size_t pending_value_count() const noexcept {
            return pending_.has_value() ? pending_->size() : 0;
        }

        /**
         * \brief Where the pending argument's trailing values begin.
         * \return nullopt when nothing pending or no `--` was seen.
         */
        [[nodiscard]] std::optional<std::size_t> pending_trailing_index() const noexcept {
            if (!pending_.has_value()) return std::nullopt;
            return pending_->trailing_index;
        }

        /**
         * \brief Pending value buffer, opening one for \p id if needed.
         *        clap's `pending_values_mut`.
         *
         * \param id              Argument now collecting.
         * \param ident           How named, or nullopt when adding to an earlier open.
         * \param trailing_values Whether past `--`; records trailing boundary if needed.
         * \return Buffer reference, valid until the next member call.
         *
         * \warning **Aborts when a different argument is already pending**, or when
         *          \p ident disagrees with the open spelling. Guard with pending_is().
         * \note Not `[[nodiscard]]`: side effect alone is valid (`--opt` opens empty).
         */
        std::vector<os_string> &pending_values(arg_id id,
                                               std::optional<arg_identifier> ident = std::nullopt,
                                               bool trailing_values = false) {
            if (!pending_.has_value())
                pending_ = pending_arg{
                    .id = id, .ident = ident, .raw_values = {}, .trailing_index = std::nullopt
                };
            pending_arg &open = *pending_;
            if (open.id != id) report_pending_conflict(open.id.name(), id.name());
            if (ident.has_value() && open.ident != ident)
                report_pending_identifier_conflict(
                    open.id.name(),
                    open.ident.has_value() ? name_of(open.ident.value()) : "positional",
                    name_of(ident.value()));
            if (trailing_values) open.mark_trailing();
            return open.raw_values;
        }

        /**
         * \brief Append one raw value to the pending argument, opening if needed.
         * \param id              Argument now collecting.
         * \param value           Bytes to record, moved in.
         * \param ident           How named; see pending_values().
         * \param trailing_values Whether past `--`.
         * \warning Aborts on mismatched id; see pending_values().
         */
        void push_pending_value(arg_id id,
                                os_string value,
                                std::optional<arg_identifier> ident = std::nullopt,
                                bool trailing_values = false) {
            pending_values(id, ident, trailing_values).push_back(std::move(value));
        }

        /**
         * \brief Mark remaining pending values as trailing. clap's `start_trailing`.
         * \note No-op when nothing pending (`--` then concerns positionals). Idempotent.
         */
        void start_trailing() {
            if (pending_.has_value()) pending_->mark_trailing();
        }

        /**
         * \brief Detach the pending argument. clap's `take_pending`.
         * \return Pending argument, or nullopt. Nothing pending afterwards either way.
         * \note Hand-off: caller parses, checks num_args, commits via add_val_to().
         */
        [[nodiscard]] std::optional<pending_arg> take_pending() {
            std::optional<pending_arg> taken = std::move(pending_);
            pending_.reset();
            return taken;
        }

        // -------------------------------------------------------------------
        // Global arguments
        // -------------------------------------------------------------------

        /**
         * \brief Copy every global argument's strongest value to every command level.
         *        clap's `propagate_globals`.
         *
         * \param global_args Ids of every global argument, any order.
         *
         * \note **Strongest wins, not innermost.** Ties keep the deeper level (clap's
         *       strict `>`). Levels that never saw the argument get it too.
         */
        void propagate_globals(std::span<const arg_id> global_args) {
            arg_matches::arg_map collected;
            fill_in_global_values(matches_, global_args, collected);
        }

    private:
        /**
         * \brief One level of propagate_globals()' descent.
         * \param level       Matches to merge into and read from.
         * \param global_args Global ids.
         * \param collected   Strongest value per id so far, updated in place.
         * \note Detach-recurse-reattach (clap's mem::take/swap) — child and parent both mutate.
         */
        static void fill_in_global_values(arg_matches &level,
                                          std::span<const arg_id> global_args,
                                          arg_matches::arg_map &collected) {
            for (const arg_id &id: global_args) {
                const matched_arg *here = level.find_arg(id.name());
                if (here == nullptr) continue;
                const matched_arg *parent = collected.find_value(id.name());
                if (parent != nullptr && parent->source() > here->source()) continue;
                collected.insert_or_assign(id, *here);
            }

            if (std::optional<std::pair<std::string, arg_matches> > child =
                        level.remove_subcommand();
                child.has_value()) {
                fill_in_global_values(child->second, global_args, collected);
                level.set_subcommand(std::move(child->first), std::move(child->second));
            }

            for (const auto &[id, value]: collected) level.insert_arg(id, value);
        }

        arg_matches matches_{};
        std::optional<pending_arg> pending_{};
    };

    /**
     * Compile-time contract on the pending state machine.
     * Transitions decide "another value of the option" vs "something new".
     * Built from string *literals* (trap 10: ubsan will not fold string from a variable).
     */
    consteval bool pending_counts_what_it_holds() {
        pending_arg pending{.id = arg_id{"point"}, .ident = arg_identifier::long_};
        if (!pending.empty() || pending.size() != 0) return false;
        pending.push(os_string{"1"});
        pending.push(os_string{"2"});
        return pending.size() == 2 && !pending.empty() &&
               pending.values()[0].view() == os_str{"1"} &&
               pending.values()[1].view() == os_str{"2"};
    }

    static_assert(pending_counts_what_it_holds());

    /** \brief Verify that repeated delimiters do not move the trailing boundary. */
    consteval bool pending_trailing_boundary_is_sticky() {
        pending_arg pending{.id = arg_id{"files"}};
        pending.push(os_string{"a"});
        if (pending.has_trailing()) return false;
        pending.mark_trailing();
        pending.push(os_string{"-b"});
        pending.mark_trailing(); // a second `--` must not move the boundary
        if (pending.trailing_index != std::optional<std::size_t>{1}) return false;
        return pending.leading_values().size() == 1 && pending.trailing_values().size() == 1 &&
               pending.leading_values()[0].view() == os_str{"a"} &&
               pending.trailing_values()[0].view() == os_str{"-b"};
    }

    static_assert(pending_trailing_boundary_is_sticky());

    /** \brief Verify that all pending values are leading before a delimiter. */
    consteval bool pending_without_a_boundary_is_all_leading() {
        pending_arg pending{.id = arg_id{"files"}};
        pending.push(os_string{"a"});
        pending.push(os_string{"b"});
        return !pending.has_trailing() && pending.leading_values().size() == 2 &&
               pending.trailing_values().empty();
    }

    static_assert(pending_without_a_boundary_is_all_leading());

    /**
     * Compile-time contract: empty pending is a *state*, not absence.
     * Collapsing it makes the next token look like a positional.
     */
    consteval bool an_empty_pending_is_still_a_pending() {
        const pending_arg pending{.id = arg_id{"opt"}, .ident = arg_identifier::short_};
        return pending.ident == arg_identifier::short_;
    }

    static_assert(an_empty_pending_is_still_a_pending());

    /**
     * Compile-time contract: identifier table is total and names are distinct.
     */
    consteval bool arg_identifier_table_is_total() {
        for (std::size_t i = 0; i < all_arg_identifiers.size(); ++i) {
            if (name_of(all_arg_identifiers[i]).empty()) return false;
            for (std::size_t j = i + 1; j < all_arg_identifiers.size(); ++j)
                if (name_of(all_arg_identifiers[i]) == name_of(all_arg_identifiers[j]))
                    return false;
        }
        return !is_named(arg_identifier::index);
    }

    static_assert(arg_identifier_table_is_total());
} // namespace clapp::detail
