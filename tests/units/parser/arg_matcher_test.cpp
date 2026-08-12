#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/parser/arg_matcher.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/any_value.hpp>
#include <clapp/util/id.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    using clapp::any_id;
    using clapp::any_value;
    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_id;
    using clapp::arg_matches;
    using clapp::arg_predicate;
    using clapp::arg_spec;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::group_builder;
    using clapp::matched_arg;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::value_range;
    using clapp::value_source;
    using clapp::detail::arg_identifier;
    using clapp::detail::arg_matcher;
    using clapp::detail::pending_arg;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Compile-time facts
    // ---------------------------------------------------------------------------

    // The reason the arg_matcher half of this file is runtime-only. An arg_matcher owns an
    // clapp::arg_matches, which owns clapp::any_value s, whose destructor is not constexpr.
    // This is a property of the design, not a gap in the tests, and asserting it is what stops
    // someone "fixing" the balance by moving cases that cannot move.
    static_assert(!std::is_trivially_destructible_v<arg_matcher>);
    static_assert(std::is_default_constructible_v<arg_matcher>);
    static_assert(std::is_move_constructible_v<arg_matcher>);
    static_assert(std::is_move_assignable_v<arg_matcher>);
    static_assert(std::is_constructible_v<arg_matcher, const command_spec&>);

    // clapp::detail::pending_arg, by contrast, holds only an arg_id, os_strings and an
    // optional index — no type erasure anywhere — so the state machine that actually decides
    // whether the next token is a value IS reachable from a static_assert.
    static_assert(std::is_default_constructible_v<pending_arg>);
    static_assert(std::is_copy_constructible_v<pending_arg>);
    static_assert(std::is_move_constructible_v<pending_arg>);

    // The identifier kinds. Distinctness matters because the three are stored in one
    // std::optional and a collapsed pair would make `-f` and `--flag` interchangeable in
    // diagnostics without changing any behaviour a parse test could see.
    static_assert(clapp::detail::name_of(arg_identifier::short_) == "short"sv);
    static_assert(clapp::detail::name_of(arg_identifier::long_) == "long"sv);
    static_assert(clapp::detail::name_of(arg_identifier::index) == "index"sv);
    static_assert(clapp::detail::all_arg_identifiers.size() == clapp::detail::arg_identifier_count);
    static_assert(clapp::detail::is_named(arg_identifier::short_));
    static_assert(clapp::detail::is_named(arg_identifier::long_));
    static_assert(!clapp::detail::is_named(arg_identifier::index));

    // An empty pending is a STATE. `--opt` whose value is still to come is exactly this, and
    // it is the only thing that makes the following token a value rather than a positional.
    // If empty() were allowed to mean "nothing pending", the loop would lose the option.
    consteval bool an_empty_pending_carries_its_identity() {
        const pending_arg pending{.id = arg_id{"opt"}, .ident = arg_identifier::long_};
        return pending.empty() && pending.size() == 0 && !pending.has_trailing() &&
               pending.id == "opt" && pending.ident == arg_identifier::long_ &&
               pending.values().empty();
    }
    static_assert(an_empty_pending_carries_its_identity());

    // Values accumulate in order and the count is the count. This is the number
    // needs_more_vals() reads.
    consteval bool pending_values_accumulate_in_order() {
        pending_arg pending{.id = arg_id{"point"}, .ident = arg_identifier::long_};
        pending.push(os_string{"1"});
        pending.push(os_string{"2"});
        pending.push(os_string{"3"});
        return pending.size() == 3 && !pending.empty() && pending.values().size() == 3 &&
               pending.values()[0].view() == os_str{"1"} &&
               pending.values()[1].view() == os_str{"2"} &&
               pending.values()[2].view() == os_str{"3"};
    }
    static_assert(pending_values_accumulate_in_order());

    // The `--` boundary is recorded once and never moves. clap spells this
    // `trailing_idx.get_or_insert(raw_vals.len())`; writing it as a plain assignment compiles
    // and passes every single-`--` test, and turns the second `--` in
    // `prog -- a -- b` into a new boundary that reclassifies `a`.
    consteval bool the_trailing_boundary_is_recorded_once() {
        pending_arg pending{.id = arg_id{"files"}, .ident = arg_identifier::index};
        pending.push(os_string{"keep"});
        if (pending.has_trailing()) return false;
        pending.mark_trailing();
        pending.push(os_string{"-a"});
        pending.mark_trailing();
        pending.push(os_string{"-b"});
        pending.mark_trailing();
        if (pending.trailing_index != std::optional<std::size_t>{1}) return false;
        return pending.leading_values().size() == 1 && pending.trailing_values().size() == 2 &&
               pending.leading_values()[0].view() == os_str{"keep"} &&
               pending.trailing_values()[0].view() == os_str{"-a"} &&
               pending.trailing_values()[1].view() == os_str{"-b"};
    }
    static_assert(the_trailing_boundary_is_recorded_once());

    // With no boundary at all everything is leading. The degenerate case matters because
    // `trailing_values()` is what a caller consults to decide `allow_hyphen_values`, and an
    // off-by-one that made the whole buffer trailing would silently accept `--oops` as data.
    consteval bool without_a_boundary_nothing_is_trailing() {
        pending_arg pending{.id = arg_id{"files"}};
        pending.push(os_string{"a"});
        pending.push(os_string{"b"});
        return !pending.has_trailing() && !pending.trailing_index.has_value() &&
               pending.leading_values().size() == 2 && pending.trailing_values().empty();
    }
    static_assert(without_a_boundary_nothing_is_trailing());

    // A boundary marked before any value makes everything trailing, which is what
    // `prog -- -a -b` means.
    consteval bool a_boundary_at_zero_makes_everything_trailing() {
        pending_arg pending{.id = arg_id{"files"}};
        pending.mark_trailing();
        pending.push(os_string{"-a"});
        pending.push(os_string{"-b"});
        return pending.trailing_index == std::optional<std::size_t>{0} &&
               pending.leading_values().empty() && pending.trailing_values().size() == 2;
    }
    static_assert(a_boundary_at_zero_makes_everything_trailing());

    // Equality compares the bytes, the identifier and the boundary. Two pendings that differ
    // only in where the `--` fell are not the same pending.
    consteval bool pending_equality_sees_the_boundary() {
        pending_arg left{.id = arg_id{"files"}, .ident = arg_identifier::index};
        pending_arg right{.id = arg_id{"files"}, .ident = arg_identifier::index};
        left.push(os_string{"a"});
        right.push(os_string{"a"});
        if (!(left == right)) return false;
        left.mark_trailing();
        if (left == right) return false;
        right.mark_trailing();
        if (!(left == right)) return false;
        right.push(os_string{"b"});
        return !(left == right);
    }
    static_assert(pending_equality_sees_the_boundary());

    // ---------------------------------------------------------------------------
    // The command under test
    // ---------------------------------------------------------------------------
    //
    // One frozen command_spec, reused by everything below. It carries the four arities the
    // pending machinery has to tell apart — a flag, a fixed pair, an unbounded list and a
    // counter — plus a global argument for the propagation cases.

    consteval command_spec make_app() {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("verbose").short_('v').long_("verbose").action(arg_action::count))
                .arg(arg_builder("point").long_("point").num_args(value_range::exactly(2)))
                .arg(arg_builder("files").long_("files").num_args(value_range::at_least(1)))
                .arg(arg_builder("mode").long_("mode").ignore_case())
                .arg(arg_builder("config").short_('c').long_("config").global())
                .group(group_builder("coords").arg("point").arg("files"))
                .subcommand(command_builder("build"))
                .subcommand(command_builder("check"));
        return app.freeze();
    }
    static constexpr command_spec app = make_app();

    // The fixture really does carry the arities the tests below depend on. Asserting it here
    // means a builder-side change that silently resolves `num_args` differently fails to
    // compile rather than quietly making the arity cases vacuous.
    static_assert(app.find_arg("point")->get_num_args() == value_range::exactly(2));
    static_assert(app.find_arg("files")->get_num_args() == value_range::at_least(1));
    static_assert(!app.find_arg("verbose")->get_num_args().takes_values());
    static_assert(app.find_arg("mode")->is_ignore_case_set());
    static_assert(app.find_arg("config")->is_global_set());

    // The arithmetic needs_more_vals() performs, spelled out on clapp::value_range so that
    // every expected value in the runtime cases below is traceable to one line here. This is
    // the whole of the arity decision; what the runtime cases add is *which count* is fed in.
    static_assert(!value_range::empty().accepts_more(0));     // a flag never wants a value
    static_assert(value_range::exactly(2).accepts_more(0));   // `--point` with nothing yet
    static_assert(value_range::exactly(2).accepts_more(1));   // `--point 1`
    static_assert(!value_range::exactly(2).accepts_more(2));  // `--point 1 2` is complete
    static_assert(value_range::at_least(1).accepts_more(0));
    static_assert(value_range::at_least(1).accepts_more(99));  // unbounded, so always

    const arg_spec& spec_of(std::string_view id) { return *app.find_arg(id); }

    any_value int_value(int value) { return any_value(std::in_place_type<int>, value); }

    std::vector<std::string> raw_of(std::span<const os_string> values) {
        std::vector<std::string> out;
        out.reserve(values.size());
        for (const os_string& one : values) out.emplace_back(one.chars());
        return out;
    }

}  // namespace

// ---------------------------------------------------------------------------
// Construction from a command_spec
// ---------------------------------------------------------------------------

CLAPP_TEST("arg_matcher seeds id and subcommand validation from the command") {
    const arg_matcher matcher{app};
    const arg_matches& built = matcher.matches();

    CLAPP_CHECK(built.has_id_validation());
    CLAPP_CHECK(built.has_subcommand_validation());

    // Every declared argument, and every declared GROUP: clap collects both, and a
    // validator that asks about a group id must not be told the id is unknown.
    CLAPP_CHECK(built.is_valid_id("point"));
    CLAPP_CHECK(built.is_valid_id("verbose"));
    CLAPP_CHECK(built.is_valid_id("coords"));
    CLAPP_CHECK(built.is_valid_id("help"));  // command_builder injects it
    CLAPP_CHECK(!built.is_valid_id("nonesuch"));
    // A long flag spelling is not an id; that mistake is the whole reason the list exists.
    CLAPP_CHECK(!built.is_valid_id("--point"));

    CLAPP_CHECK(built.is_valid_subcommand("build"));
    CLAPP_CHECK(built.is_valid_subcommand("check"));
    CLAPP_CHECK(!built.is_valid_subcommand("deploy"));

    // Nothing has been matched yet: validation lists are about the command, not the parse.
    CLAPP_CHECK(built.empty());
    CLAPP_CHECK(matcher.arg_count() == 0);
    CLAPP_CHECK(!matcher.has_pending());
}

CLAPP_TEST("a default-constructed arg_matcher validates nothing") {
    const arg_matcher matcher;
    CLAPP_CHECK(!matcher.matches().has_id_validation());
    CLAPP_CHECK(matcher.matches().is_valid_id("anything-at-all"));
    CLAPP_CHECK(matcher.empty());
}

// ---------------------------------------------------------------------------
// needs_more_vals — the question asked on every token
// ---------------------------------------------------------------------------

CLAPP_TEST("needs_more_vals counts the pending values, not the committed ones") {
    arg_matcher matcher{app};
    const arg_spec& point = spec_of("point");

    // Nothing pending: the occurrence has taken no values, so a two-value option wants
    // more.
    CLAPP_CHECK(matcher.needs_more_vals(point));

    matcher.start_custom_arg(point, value_source::command_line);
    CLAPP_CHECK(matcher.needs_more_vals(point));

    matcher.push_pending_value(arg_id{"point"}, os_string{"1"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.pending_value_count() == 1);
    CLAPP_CHECK(matcher.needs_more_vals(point));

    matcher.push_pending_value(arg_id{"point"}, os_string{"2"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.pending_value_count() == 2);
    CLAPP_CHECK(!matcher.needs_more_vals(point));

    // Resolving the occurrence commits the values and clears the pending. The committed
    // count is now 2 and the pending count is 0, so the two possible readings of
    // "how many values so far" now disagree: the correct answer is "wants more",
    // because a SECOND occurrence of `--point` starts empty.
    const std::optional<pending_arg> resolved = matcher.take_pending();
    CLAPP_CHECK(resolved.has_value());
    matcher.add_val_to(arg_id{"point"}, int_value(1), os_string{"1"});
    matcher.add_val_to(arg_id{"point"}, int_value(2), os_string{"2"});
    CLAPP_CHECK(matcher.get("point")->value_count() == 2);
    CLAPP_CHECK(matcher.get("point")->value_count_in_last_occurrence() == 2);
    CLAPP_CHECK(matcher.needs_more_vals(point));

    // And with one value pending on top of the two committed, the readings disagree in
    // the other direction: pending says 1 (< 2, wants more), committed says 2 (done).
    matcher.push_pending_value(arg_id{"point"}, os_string{"3"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.needs_more_vals(point));
}

CLAPP_TEST("needs_more_vals ignores a pending that belongs to another argument") {
    arg_matcher matcher{app};
    matcher.push_pending_value(arg_id{"files"}, os_string{"a"}, arg_identifier::long_);
    matcher.push_pending_value(arg_id{"files"}, os_string{"b"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.pending_value_count() == 2);

    // `point` has collected nothing of its own, whatever `files` is holding.
    CLAPP_CHECK(matcher.needs_more_vals(spec_of("point")));
    // An unbounded arity always wants more, which is what makes `--files a b c` work.
    CLAPP_CHECK(matcher.needs_more_vals(spec_of("files")));
}

CLAPP_TEST("needs_more_vals says no for a flag and yes forever for an unbounded arity") {
    arg_matcher matcher{app};

    // A `count` flag takes no values at all: even with nothing collected, it wants none.
    CLAPP_CHECK(!matcher.needs_more_vals(spec_of("verbose")));

    const arg_spec& files = spec_of("files");
    for (int i = 0; i < 5; ++i) {
        CLAPP_CHECK(matcher.needs_more_vals(files));
        matcher.push_pending_value(arg_id{"files"}, os_string{"x"}, arg_identifier::long_);
    }
    CLAPP_CHECK(matcher.needs_more_vals(files));
}

// ---------------------------------------------------------------------------
// The pending argument
// ---------------------------------------------------------------------------

CLAPP_TEST("pending_values opens an empty pending, which is what `--opt` alone means") {
    arg_matcher matcher{app};
    CLAPP_CHECK(!matcher.has_pending());
    CLAPP_CHECK(!matcher.pending_arg_id().has_value());
    CLAPP_CHECK(matcher.pending_value_count() == 0);
    CLAPP_CHECK(matcher.peek_pending_values().empty());

    // clap calls pending_values_mut purely for this side effect.
    matcher.pending_values(arg_id{"point"}, arg_identifier::long_);

    CLAPP_CHECK(matcher.has_pending());
    CLAPP_CHECK(matcher.pending_is("point"));
    CLAPP_CHECK(!matcher.pending_is("files"));
    CLAPP_CHECK(matcher.pending_arg_id().has_value());
    CLAPP_CHECK(matcher.pending_arg_id().value() == "point");
    CLAPP_CHECK(matcher.pending_identifier() == arg_identifier::long_);
    CLAPP_CHECK(matcher.pending_value_count() == 0);
    CLAPP_CHECK(matcher.peek_pending_values().empty());

    // Nothing reached the matches: an occurrence in flight is invisible until it resolves.
    CLAPP_CHECK(!matcher.contains("point"));
}

CLAPP_TEST("take_pending hands the buffer over and leaves nothing behind") {
    arg_matcher matcher{app};
    matcher.push_pending_value(arg_id{"point"}, os_string{"3"}, arg_identifier::long_);
    matcher.push_pending_value(arg_id{"point"}, os_string{"4"}, arg_identifier::long_);

    const std::optional<pending_arg> taken = matcher.take_pending();
    CLAPP_CHECK(taken.has_value());
    CLAPP_CHECK(taken->id == "point");
    CLAPP_CHECK(taken->ident == arg_identifier::long_);
    CLAPP_CHECK(raw_of(taken->values()) == std::vector<std::string>{"3", "4"});
    CLAPP_CHECK(!taken->has_trailing());

    CLAPP_CHECK(!matcher.has_pending());
    CLAPP_CHECK(!matcher.pending_is("point"));
    CLAPP_CHECK(matcher.pending_value_count() == 0);
    CLAPP_CHECK(!matcher.take_pending().has_value());
}

CLAPP_TEST("a resolved pending lets the next argument open without conflict") {
    arg_matcher matcher{app};
    matcher.push_pending_value(arg_id{"point"}, os_string{"1"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.pending_is("point"));

    // This is the guard the parse loop tests before it resolves; the abort it avoids is
    // documented at the top of this file.
    if (!matcher.pending_is("files")) (void)matcher.take_pending();

    matcher.push_pending_value(arg_id{"files"}, os_string{"a"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.pending_is("files"));
    CLAPP_CHECK(matcher.pending_value_count() == 1);
}

CLAPP_TEST("start_trailing with nothing pending does not invent a pending") {
    arg_matcher matcher{app};
    matcher.start_trailing();
    // A `--` seen while no argument is collecting concerns the positionals; inventing a
    // pending here would make the very next token look like a value of something.
    CLAPP_CHECK(!matcher.has_pending());
    CLAPP_CHECK(!matcher.pending_trailing_index().has_value());

    matcher.push_pending_value(arg_id{"files"}, os_string{"a"}, arg_identifier::index);
    matcher.start_trailing();
    matcher.push_pending_value(arg_id{"files"}, os_string{"-b"}, arg_identifier::index);
    matcher.start_trailing();  // the boundary must not move
    CLAPP_CHECK(matcher.pending_trailing_index() == std::optional<std::size_t>{1});

    const std::optional<pending_arg> taken = matcher.take_pending();
    CLAPP_CHECK(taken.has_value());
    CLAPP_CHECK(raw_of(taken->leading_values()) == std::vector<std::string>{"a"});
    CLAPP_CHECK(raw_of(taken->trailing_values()) == std::vector<std::string>{"-b"});
}

CLAPP_TEST("pending_values records the trailing boundary when asked to") {
    arg_matcher matcher{app};
    matcher.pending_values(arg_id{"files"}, arg_identifier::index).push_back(os_string{"a"});
    CLAPP_CHECK(!matcher.pending_trailing_index().has_value());

    matcher.pending_values(arg_id{"files"}, arg_identifier::index, /*trailing_values=*/true)
            .push_back(os_string{"-b"});
    CLAPP_CHECK(matcher.pending_trailing_index() == std::optional<std::size_t>{1});

    // Asking again must not move it, exactly as start_trailing() must not.
    matcher.pending_values(arg_id{"files"}, arg_identifier::index, /*trailing_values=*/true)
            .push_back(os_string{"-c"});
    CLAPP_CHECK(matcher.pending_trailing_index() == std::optional<std::size_t>{1});
    CLAPP_CHECK(matcher.pending_value_count() == 3);
}

CLAPP_TEST("pending_values with no identifier joins an argument recognized earlier") {
    arg_matcher matcher{app};
    // `--point` opened with a spelling ...
    matcher.pending_values(arg_id{"point"}, arg_identifier::long_);
    // ... and the loop adds the following tokens without repeating it. clap passes None
    // here for exactly this reason, and passing Some(Index) instead would trip its
    // identifier assertion.
    matcher.pending_values(arg_id{"point"}, std::nullopt).push_back(os_string{"1"});
    matcher.pending_values(arg_id{"point"}).push_back(os_string{"2"});

    CLAPP_CHECK(matcher.pending_value_count() == 2);
    CLAPP_CHECK(matcher.pending_identifier() == arg_identifier::long_);
}

// ---------------------------------------------------------------------------
// Starting occurrences
// ---------------------------------------------------------------------------

CLAPP_TEST("start_custom_arg adds an occurrence rather than replacing the entry") {
    arg_matcher matcher{app};
    const arg_spec& files = spec_of("files");

    matcher.start_custom_arg(files, value_source::command_line);
    matcher.add_val_to(arg_id{"files"}, int_value(1), os_string{"one"});
    CLAPP_CHECK(matcher.get("files")->occurrence_count() == 1);
    CLAPP_CHECK(matcher.get("files")->value_count() == 1);

    matcher.start_custom_arg(files, value_source::command_line);
    matcher.add_val_to(arg_id{"files"}, int_value(2), os_string{"two"});

    const matched_arg* entry = matcher.get("files");
    CLAPP_CHECK(entry != nullptr);
    CLAPP_CHECK(entry->occurrence_count() == 2);
    CLAPP_CHECK(entry->value_count() == 2);
    CLAPP_CHECK(raw_of(entry->raw_values()) == std::vector<std::string>{"one", "two"});
    // The boundaries slice the flat storage one value per occurrence.
    CLAPP_CHECK(entry->occurrences()[0] == clapp::value_group{.first = 0, .count = 1});
    CLAPP_CHECK(entry->occurrences()[1] == clapp::value_group{.first = 1, .count = 1});
    CLAPP_CHECK(matcher.arg_count() == 1);
}

CLAPP_TEST("start_custom_arg keeps an occurrence that carries no values") {
    arg_matcher matcher{app};
    const arg_spec& verbose = spec_of("verbose");

    matcher.start_custom_arg(verbose, value_source::command_line);
    matcher.start_custom_arg(verbose, value_source::command_line);
    matcher.start_custom_arg(verbose, value_source::command_line);

    // `-vvv`. Dropping empty occurrences would leave the count at 1 with nothing failing.
    CLAPP_CHECK(matcher.get("verbose")->occurrence_count() == 3);
    CLAPP_CHECK(matcher.get("verbose")->value_count() == 0);
    CLAPP_CHECK(matcher.get("verbose")->empty());
}

CLAPP_TEST("start_custom_arg merges the source instead of overwriting it") {
    arg_matcher matcher{app};
    const arg_spec& mode = spec_of("mode");

    matcher.start_custom_arg(mode, value_source::command_line);
    CLAPP_CHECK(matcher.get("mode")->source() == value_source::command_line);

    // Defaults are applied after the command line has been read. Overwriting here would
    // report a user-supplied value as a default, which is what `value_source()` exists to
    // tell apart and what every `required_if_eq` consults.
    matcher.start_custom_arg(mode, value_source::default_value);
    CLAPP_CHECK(matcher.get("mode")->source() == value_source::command_line);

    // ... and the merge is monotone in the other direction too.
    arg_matcher other{app};
    other.start_custom_arg(mode, value_source::default_value);
    CLAPP_CHECK(other.get("mode")->source() == value_source::default_value);
    other.start_custom_arg(mode, value_source::env_variable);
    CLAPP_CHECK(other.get("mode")->source() == value_source::env_variable);
    other.start_custom_arg(mode, value_source::command_line);
    CLAPP_CHECK(other.get("mode")->source() == value_source::command_line);
}

CLAPP_TEST("start_custom_arg carries ignore_case across from the spec") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("mode"), value_source::command_line);
    matcher.add_val_to(arg_id{"mode"}, int_value(0), os_string{"FAST"});

    CLAPP_CHECK(matcher.get("mode")->ignore_case());
    CLAPP_CHECK(matcher.get("mode")->has_raw_value(os_str{"fast"}));

    // An argument that did not ask for it still compares bytes.
    matcher.start_custom_arg(spec_of("files"), value_source::command_line);
    matcher.add_val_to(arg_id{"files"}, int_value(0), os_string{"FAST"});
    CLAPP_CHECK(!matcher.get("files")->ignore_case());
    CLAPP_CHECK(!matcher.get("files")->has_raw_value(os_str{"fast"}));
    CLAPP_CHECK(matcher.get("files")->has_raw_value(os_str{"FAST"}));
}

CLAPP_TEST("start_custom_arg records the declared type when the caller supplies it") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("point"), value_source::command_line, any_id::of<int>());

    const matched_arg* entry = matcher.get("point");
    CLAPP_CHECK(entry->has_type_id());
    CLAPP_CHECK(entry->type_id() == any_id::of<int>());
    // With a declared type, an EMPTY occurrence still reports it — which is the one case
    // infer_type_id() cannot recover on its own.
    CLAPP_CHECK(entry->infer_type_id(any_id::of<std::string>()) == any_id::of<int>());

    // Left out, the type is inferred from the values instead.
    arg_matcher untyped{app};
    untyped.start_custom_arg(spec_of("point"), value_source::command_line);
    CLAPP_CHECK(!untyped.get("point")->has_type_id());
    untyped.add_val_to(arg_id{"point"}, int_value(7), os_string{"7"});
    CLAPP_CHECK(untyped.get("point")->infer_type_id(any_id::of<std::string>()) ==
                any_id::of<int>());
    CLAPP_CHECK(untyped.get("point")->infer_type_id(any_id::of<int>()) == any_id::of<int>());
}

CLAPP_TEST("start_custom_group creates an untyped entry") {
    arg_matcher matcher{app};
    matcher.start_custom_group(arg_id{"coords"}, value_source::command_line);

    const matched_arg* entry = matcher.get("coords");
    CLAPP_CHECK(entry != nullptr);
    CLAPP_CHECK(!entry->has_type_id());
    CLAPP_CHECK(entry->occurrence_count() == 1);
    CLAPP_CHECK(entry->source() == value_source::command_line);

    // A second sighting adds an occurrence, as for an argument.
    matcher.start_custom_group(arg_id{"coords"}, value_source::command_line);
    CLAPP_CHECK(matcher.get("coords")->occurrence_count() == 2);
}

CLAPP_TEST("start_occurrence_of_external files values under the empty id") {
    arg_matcher matcher{app};
    matcher.start_occurrence_of_external();
    matcher.add_val_to(clapp::external_id,
                       any_value(std::in_place_type<os_string>, os_string{"whatever"}),
                       os_string{"whatever"});

    const matched_arg* entry = matcher.get("");
    CLAPP_CHECK(entry != nullptr);
    CLAPP_CHECK(entry->source() == value_source::command_line);
    CLAPP_CHECK(entry->value_count() == 1);
    // clap's Id::EXTERNAL is the empty string, and the id list must not reject it.
    CLAPP_CHECK(clapp::external_id.empty());
    CLAPP_CHECK(matcher.matches().is_valid_id(""));
}

CLAPP_TEST("start_occurrence_of is start_custom_arg pinned to the command line") {
    arg_matcher matcher{app};
    matcher.start_occurrence_of(spec_of("verbose"));
    matcher.start_occurrence_of(spec_of("verbose"));

    CLAPP_CHECK(matcher.get("verbose")->occurrence_count() == 2);
    CLAPP_CHECK(matcher.get("verbose")->source() == value_source::command_line);
    CLAPP_CHECK(matcher.get("verbose")->is_explicit());
}

// ---------------------------------------------------------------------------
// Committing values and indices
// ---------------------------------------------------------------------------

CLAPP_TEST("add_val_to and add_index_to keep values, bytes and positions parallel") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("point"), value_source::command_line);
    matcher.add_val_to(arg_id{"point"}, int_value(10), os_string{"10"});
    matcher.add_index_to(arg_id{"point"}, 2);
    matcher.add_val_to(arg_id{"point"}, int_value(20), os_string{"20"});
    matcher.add_index_to(arg_id{"point"}, 3);

    const matched_arg* entry = matcher.get("point");
    CLAPP_CHECK(entry->value_count() == 2);
    CLAPP_CHECK(raw_of(entry->raw_values()) == std::vector<std::string>{"10", "20"});
    CLAPP_CHECK(entry->indices().size() == 2);
    CLAPP_CHECK(entry->index_at(0) == std::optional<std::size_t>{2});
    CLAPP_CHECK(entry->index_at(1) == std::optional<std::size_t>{3});
    CLAPP_CHECK(entry->occurrence_count() == 1);
    CLAPP_CHECK(entry->value_count_in_last_occurrence() == 2);
}

CLAPP_TEST("add_val_to on an id nobody started opens the entry and the occurrence") {
    arg_matcher matcher{app};
    // clap panics here. clapp recovers, the same trade matched_arg::append_value makes —
    // but the entry it creates has no declared type and no ignore_case, which is the tell.
    matcher.add_val_to(arg_id{"files"}, int_value(1), os_string{"1"});

    const matched_arg* entry = matcher.get("files");
    CLAPP_CHECK(entry != nullptr);
    CLAPP_CHECK(entry->occurrence_count() == 1);
    CLAPP_CHECK(entry->value_count() == 1);
    CLAPP_CHECK(!entry->has_type_id());
    CLAPP_CHECK(!entry->has_source());
}

// ---------------------------------------------------------------------------
// The map surface clap reaches through Deref
// ---------------------------------------------------------------------------

CLAPP_TEST("get, contains, entry and remove agree about what is recorded") {
    arg_matcher matcher{app};
    CLAPP_CHECK(!matcher.contains("point"));
    CLAPP_CHECK(matcher.get("point") == nullptr);
    CLAPP_CHECK(!matcher.remove("point"));

    matcher.entry(arg_id{"point"});
    CLAPP_CHECK(matcher.contains("point"));
    CLAPP_CHECK(matcher.get("point") != nullptr);
    CLAPP_CHECK(matcher.arg_count() == 1);
    CLAPP_CHECK(!matcher.empty());

    // entry() is idempotent — this is or_insert_with, not insert.
    matcher.entry(arg_id{"point"}).start_occurrence();
    matcher.entry(arg_id{"point"});
    CLAPP_CHECK(matcher.get("point")->occurrence_count() == 1);

    // The mutable overload really does hand back the stored object.
    matcher.get("point")->set_source(value_source::env_variable);
    CLAPP_CHECK(matcher.matches().value_source("point") == value_source::env_variable);

    CLAPP_CHECK(matcher.remove("point"));
    CLAPP_CHECK(!matcher.contains("point"));
    CLAPP_CHECK(!matcher.remove("point"));
    CLAPP_CHECK(matcher.empty());
}

CLAPP_TEST("contains does not consult the id validation list") {
    arg_matcher matcher{app};
    // An id the command never declared is simply absent, not an error: `remove` is how
    // `overrides_with` works and it must not care.
    CLAPP_CHECK(!matcher.contains("nonesuch"));
    matcher.entry(arg_id{"nonesuch"});
    CLAPP_CHECK(matcher.contains("nonesuch"));
    CLAPP_CHECK(matcher.remove("nonesuch"));
}

CLAPP_TEST("arg_ids reports every recorded id") {
    arg_matcher matcher{app};
    matcher.entry(arg_id{"point"});
    matcher.entry(arg_id{"files"});
    matcher.entry(arg_id{"verbose"});

    std::vector<std::string_view> seen;
    for (const arg_id& id : matcher.arg_ids()) seen.push_back(id.name());
    std::ranges::sort(seen);
    CLAPP_CHECK(seen == std::vector<std::string_view>{"files", "point", "verbose"});
    CLAPP_CHECK(matcher.args().size() == 3);
}

// ---------------------------------------------------------------------------
// check_explicit
// ---------------------------------------------------------------------------

CLAPP_TEST("check_explicit is false for an absent id and for a defaulted one") {
    arg_matcher matcher{app};
    CLAPP_CHECK(!matcher.check_explicit("mode", arg_predicate::present()));

    matcher.start_custom_arg(spec_of("mode"), value_source::default_value);
    matcher.add_val_to(arg_id{"mode"}, int_value(0), os_string{"slow"});
    CLAPP_CHECK(matcher.contains("mode"));
    // Present, but only because a default put it there.
    CLAPP_CHECK(!matcher.check_explicit("mode", arg_predicate::present()));
}

CLAPP_TEST("check_explicit counts an environment value as explicit") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("mode"), value_source::env_variable);
    matcher.add_val_to(arg_id{"mode"}, int_value(0), os_string{"slow"});

    // The trap clapp::is_explicit(value_source) warns about: reading "explicit" as "typed
    // on the command line" makes env-supplied arguments stop participating in conflict and
    // requirement checks, and nothing else notices.
    CLAPP_CHECK(matcher.check_explicit("mode", arg_predicate::present()));
    CLAPP_CHECK(matcher.check_explicit("mode", arg_predicate::equal_to("slow")));
    CLAPP_CHECK(!matcher.check_explicit("mode", arg_predicate::equal_to("fast")));
}

CLAPP_TEST("check_explicit compares values, and folds case when the argument asked to") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("mode"), value_source::command_line);
    matcher.add_val_to(arg_id{"mode"}, int_value(0), os_string{"FAST"});

    CLAPP_CHECK(matcher.check_explicit("mode", arg_predicate::present()));
    CLAPP_CHECK(matcher.check_explicit("mode", arg_predicate::equal_to("FAST")));
    // clapp folds here because `mode` is declared ignore_case; clap compares bytes even
    // then. The divergence is recorded on arg_matcher::check_explicit().
    CLAPP_CHECK(matcher.check_explicit("mode", arg_predicate::equal_to("fast")));
    CLAPP_CHECK(!matcher.check_explicit("mode", arg_predicate::equal_to("slow")));

    // An argument without ignore_case does not fold.
    matcher.start_custom_arg(spec_of("files"), value_source::command_line);
    matcher.add_val_to(arg_id{"files"}, int_value(0), os_string{"FAST"});
    CLAPP_CHECK(matcher.check_explicit("files", arg_predicate::equal_to("FAST")));
    CLAPP_CHECK(!matcher.check_explicit("files", arg_predicate::equal_to("fast")));
}

CLAPP_TEST("check_explicit is true for a valueless occurrence under the present predicate") {
    arg_matcher matcher{app};
    matcher.start_occurrence_of(spec_of("verbose"));
    // A flag stores nothing, so an "is it present" question must not go looking at values.
    CLAPP_CHECK(matcher.get("verbose")->empty());
    CLAPP_CHECK(matcher.check_explicit("verbose", arg_predicate::present()));
    CLAPP_CHECK(!matcher.check_explicit("verbose", arg_predicate::equal_to("anything")));
}

// ---------------------------------------------------------------------------
// Subcommands and the transition out
// ---------------------------------------------------------------------------

CLAPP_TEST("into_inner moves the matches out, subcommand and all") {
    arg_matcher matcher{app};
    CLAPP_CHECK(!matcher.has_subcommand());
    CLAPP_CHECK(!matcher.subcommand_name().has_value());
    matcher.start_custom_arg(spec_of("verbose"), value_source::command_line);

    arg_matcher child{*app.find_subcommand("build")};
    child.entry(arg_id{"help"});
    matcher.set_subcommand("build", std::move(child).into_inner());

    CLAPP_CHECK(matcher.has_subcommand());
    CLAPP_CHECK(matcher.subcommand_name() == "build"sv);

    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(result.has_subcommand());
    CLAPP_CHECK(result.subcommand_name() == "build"sv);
    CLAPP_CHECK(result.subcommand_matches("build") != nullptr);
    CLAPP_CHECK(result.contains_id("verbose"));
    CLAPP_CHECK(result.has_id_validation());
}

CLAPP_TEST("a pending argument never reaches the matches") {
    arg_matcher matcher{app};
    matcher.push_pending_value(arg_id{"point"}, os_string{"1"}, arg_identifier::long_);
    CLAPP_CHECK(matcher.has_pending());

    // into_inner() drops it, exactly as clap's does. The parse loop is what must not let
    // this happen; has_pending() is how a caller checks.
    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(result.empty());
    CLAPP_CHECK(!result.contains_id("point"));
}

// ---------------------------------------------------------------------------
// Global propagation
// ---------------------------------------------------------------------------

namespace {

    arg_matcher tree_with(std::optional<value_source> at_root,
                          std::optional<value_source> at_child,
                          std::string_view root_value,
                          std::string_view child_value) {
        arg_matcher matcher{app};
        if (at_root.has_value()) {
            matcher.start_custom_arg(spec_of("config"), at_root.value());
            matcher.add_val_to(arg_id{"config"}, int_value(1), os_string{std::string{root_value}});
        }

        arg_matcher child{*app.find_subcommand("build")};
        if (at_child.has_value()) {
            child.start_custom_arg(spec_of("config"), at_child.value());
            child.add_val_to(arg_id{"config"}, int_value(2), os_string{std::string{child_value}});
        }
        matcher.set_subcommand("build", std::move(child).into_inner());
        return matcher;
    }

    const std::array<arg_id, 1> globals{arg_id{"config"}};

}  // namespace

CLAPP_TEST("propagate_globals copies a root value down to a subcommand that never saw it") {
    arg_matcher matcher = tree_with(value_source::command_line, std::nullopt, "root.toml", "");
    const std::optional<std::size_t> root_ordinal =
            matcher.matches().insertion_ordinal_of("config");
    matcher.propagate_globals(globals);

    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(result.insertion_ordinal_of("config") == root_ordinal);
    const arg_matches* child = result.subcommand_matches("build");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(child->contains_id("config"));
    CLAPP_CHECK(child->insertion_ordinal_of("config").has_value());
    CLAPP_CHECK(child->value_source("config") == value_source::command_line);
    const auto child_raw = child->get_raw("config");
    CLAPP_CHECK(child_raw.has_value());
    CLAPP_CHECK(raw_of(child_raw.value()) == std::vector<std::string>{"root.toml"});
}

CLAPP_TEST("propagate_globals copies a subcommand value up to the root") {
    arg_matcher matcher = tree_with(std::nullopt, value_source::command_line, "", "sub.toml");
    matcher.propagate_globals(globals);

    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(result.contains_id("config"));
    const auto root_raw = result.get_raw("config");
    CLAPP_CHECK(root_raw.has_value());
    CLAPP_CHECK(raw_of(root_raw.value()) == std::vector<std::string>{"sub.toml"});
}

CLAPP_TEST("propagate_globals keeps the stronger source, whichever level it came from") {
    // Root typed, child defaulted: the typed one must survive at BOTH levels. Reading
    // "innermost wins" instead would demote the user's value to a default.
    arg_matcher stronger_above =
            tree_with(value_source::command_line, value_source::default_value, "typed", "fallback");
    stronger_above.propagate_globals(globals);
    const arg_matches above = std::move(stronger_above).into_inner();
    CLAPP_CHECK(above.value_source("config") == value_source::command_line);
    CLAPP_CHECK(raw_of(above.get_raw("config").value()) == std::vector<std::string>{"typed"});
    CLAPP_CHECK(raw_of(above.subcommand_matches("build")->get_raw("config").value()) ==
                std::vector<std::string>{"typed"});

    // Root defaulted, child typed: the child's wins.
    arg_matcher stronger_below =
            tree_with(value_source::default_value, value_source::command_line, "fallback", "typed");
    stronger_below.propagate_globals(globals);
    const arg_matches below = std::move(stronger_below).into_inner();
    CLAPP_CHECK(below.value_source("config") == value_source::command_line);
    CLAPP_CHECK(raw_of(below.get_raw("config").value()) == std::vector<std::string>{"typed"});
}

CLAPP_TEST("propagate_globals breaks a tie in favour of the deeper level") {
    // clap's test is `parent_ma.source() > ma.source()` — strictly greater — so an equal
    // parent yields. `prog --config a build --config b` must read back as `b`.
    arg_matcher matcher =
            tree_with(value_source::command_line, value_source::command_line, "a", "b");
    matcher.propagate_globals(globals);

    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(raw_of(result.get_raw("config").value()) == std::vector<std::string>{"b"});
    CLAPP_CHECK(raw_of(result.subcommand_matches("build")->get_raw("config").value()) ==
                std::vector<std::string>{"b"});
}

CLAPP_TEST("propagate_globals leaves non-global arguments where they are") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("verbose"), value_source::command_line);
    matcher.start_custom_arg(spec_of("config"), value_source::command_line);
    matcher.add_val_to(arg_id{"config"}, int_value(1), os_string{"root.toml"});

    arg_matcher child{*app.find_subcommand("build")};
    child.entry(arg_id{"help"});
    matcher.set_subcommand("build", std::move(child).into_inner());

    matcher.propagate_globals(globals);

    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(result.contains_id("verbose"));
    // `verbose` is not in the global list, so the child must not have acquired it.
    //
    // Asked through find_arg() rather than contains_id(), and the difference is a real
    // property of the design worth pinning down: the child's matcher was seeded from the
    // `build` spec, whose id list does NOT contain `verbose` — only global arguments are
    // propagated into a subcommand by freeze() — so contains_id("verbose") on the child is
    // an unknown-id programming error and aborts. Measured: it does. find_arg() is the
    // question "is there a record", which is what this assertion means.
    CLAPP_CHECK(result.subcommand_matches("build")->find_arg("verbose") == nullptr);
    CLAPP_CHECK(!result.subcommand_matches("build")->is_valid_id("verbose"));
    // ... whereas `config` IS global, so freeze() put it in the child's id list, which is
    // why every propagation case above may ask the child about it directly.
    CLAPP_CHECK(result.subcommand_matches("build")->is_valid_id("config"));
    const std::optional<std::size_t> help_ordinal =
            result.subcommand_matches("build")->insertion_ordinal_of("help");
    const std::optional<std::size_t> config_ordinal =
            result.subcommand_matches("build")->insertion_ordinal_of("config");
    CLAPP_CHECK(help_ordinal.has_value());
    CLAPP_CHECK(config_ordinal.has_value());
    CLAPP_CHECK(*help_ordinal < *config_ordinal);
}

CLAPP_TEST("propagate_globals on a tree with no subcommand is a no-op that still works") {
    arg_matcher matcher{app};
    matcher.start_custom_arg(spec_of("config"), value_source::command_line);
    matcher.add_val_to(arg_id{"config"}, int_value(1), os_string{"only.toml"});
    matcher.propagate_globals(globals);

    const arg_matches result = std::move(matcher).into_inner();
    CLAPP_CHECK(!result.has_subcommand());
    CLAPP_CHECK(result.contains_id("config"));
    CLAPP_CHECK(raw_of(result.get_raw("config").value()) == std::vector<std::string>{"only.toml"});
}
