#include <clapp/builder/arg_group.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

    using clapp::arg_id;
    using clapp::group_builder;
    using clapp::group_spec;

    // ---------------------------------------------------------------------------
    // The neutral group
    // ---------------------------------------------------------------------------

    static_assert(std::is_trivially_copyable_v<group_spec>);
    static_assert(std::is_aggregate_v<group_spec>);

    // command_of<T>() may mint a group before it knows who joins it, so an empty group is a
    // legal state rather than a mistake.
    static_assert(group_spec{}.empty());
    static_assert(group_spec{}.size() == 0);
    static_assert(!group_spec{}.is_required_set());
    static_assert(!group_spec{}.is_multiple());
    static_assert(!group_spec{}.contains("anything"));
    static_assert(group_spec{}.get_args().empty());
    static_assert(group_spec{}.get_requires().empty());
    static_assert(group_spec{}.get_conflicts().empty());
    static_assert(group_spec{} == group_spec{});

    // ---------------------------------------------------------------------------
    // The headline claim: a consteval builder produces a .rodata-ready group
    // ---------------------------------------------------------------------------

    consteval group_spec make_versioning() {
        return group_builder("vers")
                .args({"set-ver", "major", "minor", "patch"})
                .required()
                .freeze();
    }
    static constexpr group_spec versioning = make_versioning();

    static_assert(versioning.get_id() == arg_id{"vers"});
    static_assert(!versioning.get_id().bound());  // slots belong to a command's table
    static_assert(versioning.size() == 4);
    static_assert(!versioning.empty());
    static_assert(versioning.is_required_set());
    static_assert(!versioning.is_multiple());  // clap's default: mutually exclusive
    static_assert(versioning.get_args()[0] == arg_id{"set-ver"});
    static_assert(versioning.get_args()[3] == arg_id{"patch"});
    static_assert(versioning.contains("major"));
    static_assert(versioning.contains("set-ver"));
    static_assert(!versioning.contains("vers"));  // the group is not its own member
    static_assert(!versioning.contains("unknown"));
    static_assert(versioning.get_requires().empty());
    static_assert(versioning.get_conflicts().empty());

    // The lazy name view is what usage rendering and did_you_mean() consume.
    static_assert(std::ranges::distance(versioning.names()) == 4);
    static_assert(std::ranges::equal(versioning.names(),
                                     std::array<std::string_view, 4>{
                                             "set-ver", "major", "minor", "patch"}));

    // Two freezes of the same description compare equal by content.
    static_assert(versioning == make_versioning());

    // ---------------------------------------------------------------------------
    // multiple() and required() are independent
    // ---------------------------------------------------------------------------

    consteval group_spec make_flags(bool required, bool multiple) {
        group_builder g("flags");
        std::move(g).args({"a", "b"}).required(required).multiple(multiple);
        return g.freeze();
    }
    static_assert(!make_flags(false, false).is_required_set());
    static_assert(!make_flags(false, false).is_multiple());
    static_assert(make_flags(true, false).is_required_set());
    static_assert(!make_flags(true, false).is_multiple());
    static_assert(!make_flags(false, true).is_required_set());
    static_assert(make_flags(false, true).is_multiple());
    static_assert(make_flags(true, true).is_required_set());
    static_assert(make_flags(true, true).is_multiple());

    // Both setters retract.
    consteval bool toggles_retract() {
        group_builder g("x");
        std::move(g).required(true).multiple(true);
        if (!g.is_required_set() || !g.is_multiple()) return false;
        std::move(g).required(false).multiple(false);
        return !g.is_required_set() && !g.is_multiple();
    }
    static_assert(toggles_retract());

    // ---------------------------------------------------------------------------
    // Relations
    // ---------------------------------------------------------------------------

    consteval group_spec make_related() {
        return group_builder("io")
                .arg("input")
                .arg("output")
                .requires_("format")
                .requires_all({"writer", "sink"})
                .conflicts_with("dry-run")
                .conflicts_with_all({"list", "check"})
                .freeze();
    }
    static constexpr group_spec related = make_related();

    static_assert(related.size() == 2);
    static_assert(related.get_requires().size() == 3);
    static_assert(related.get_requires()[0] == arg_id{"format"});
    static_assert(related.get_requires()[1] == arg_id{"writer"});
    static_assert(related.get_requires()[2] == arg_id{"sink"});
    static_assert(related.get_conflicts().size() == 3);
    static_assert(related.get_conflicts()[0] == arg_id{"dry-run"});
    static_assert(related.get_conflicts()[2] == arg_id{"check"});

    // Differing in any list makes two groups unequal.
    static_assert(!(related == versioning));
    static_assert(!(group_builder("io").arg("input").freeze() == related));

    // ---------------------------------------------------------------------------
    // Renaming and incremental construction
    // ---------------------------------------------------------------------------

    consteval bool rename_group() {
        group_builder g("old");
        std::move(g).id("new");
        return g.get_id() == "new";
    }
    static_assert(rename_group());

    // The shape command_of<T>() will use: break the chain across statements, because the
    // returned reference IS the object.
    consteval group_spec incremental() {
        group_builder g("logging");
        std::move(g).arg("verbose");
        std::move(g).arg("quiet");
        for (std::string_view name : {"debug", "trace"}) std::move(g).arg(name);
        std::move(g).multiple();
        return g.freeze();
    }
    static constexpr group_spec logging = incremental();
    static_assert(logging.size() == 4);
    static_assert(logging.is_multiple());
    static_assert(logging.contains("trace"));

    // Plural setters take any range, not just a braced list.
    consteval std::size_t args_from_vector() {
        std::vector<std::string> names{"a", "b", "c"};
        group_builder g("x");
        std::move(g).args(names);
        return g.size();
    }
    static_assert(args_from_vector() == 3);

    consteval std::size_t requires_from_vector() {
        std::vector<std::string_view> names{"a", "b"};
        group_builder g("x");
        std::move(g).requires_all(names);
        return g.get_requires().size();
    }
    static_assert(requires_from_vector() == 2);

    // ---------------------------------------------------------------------------
    // An empty group survives freeze
    // ---------------------------------------------------------------------------

    consteval group_spec make_empty() { return group_builder("placeholder").freeze(); }
    static constexpr group_spec placeholder = make_empty();
    static_assert(placeholder.empty());
    static_assert(placeholder.get_id() == arg_id{"placeholder"});
    static_assert(placeholder.get_args().data() == nullptr);  // no allocation for nothing

    // ---------------------------------------------------------------------------
    // Builder-side and query properties — constant-evaluated, not run
    // ---------------------------------------------------------------------------
    //
    // clapp::group_builder holds std::vector<std::string>, so it can never be a constexpr
    // *variable*. It can still be built, queried and destroyed inside a consteval function,
    // which is the distinction flat_map.hpp:56-60 draws — so these fail the build rather
    // than a test binary.

    consteval bool a_fresh_group_is_empty_optional_and_exclusive() {
        const group_builder g("vers");
        return g.get_id() == "vers" && g.empty() && g.size() == 0 && !g.is_required_set() &&
               !g.is_multiple() && !g.contains("major");
    }
    static_assert(a_fresh_group_is_empty_optional_and_exclusive());

    consteval bool chaining_mutates_in_place() {
        group_builder g("vers");
        const group_builder& returned = std::move(g).arg("major").required();
        return &returned == &g && g.size() == 1 && g.is_required_set();
    }
    static_assert(chaining_mutates_in_place());

    consteval bool getters_borrow_the_builders_own_storage() {
        group_builder g("io");
        std::move(g).args({"input", "output"}).requires_("format").conflicts_with("dry-run");
        return g.get_args().size() == 2 && g.get_args()[0] == "input" &&
               g.get_requires().size() == 1 && g.get_requires()[0] == "format" &&
               g.get_conflicts().size() == 1 && g.get_conflicts()[0] == "dry-run" &&
               g.contains("output") && !g.contains("format");  // a requirement is not a membership
    }
    static_assert(getters_borrow_the_builders_own_storage());

    consteval bool membership_lookup_is_what_the_validator_will_call() {
        // The validator asks "which group does this argument belong to" once per matched
        // argument, so the scan has to answer correctly for members, non-members and the
        // group's own id alike.
        std::size_t found = 0;
        for (std::string_view name : {"set-ver", "major", "minor", "patch", "vers", "other"}) {
            if (versioning.contains(name)) ++found;
        }
        return found == 4;
    }
    static_assert(membership_lookup_is_what_the_validator_will_call());

    /**
     * \note `push_back` in a loop rather than `usage += name`, which is what a renderer
     *       would naturally write. `+=` reaches libstdc++'s `_M_mutate`, whose
     *       `if (__s && __len2)` tests the *source* pointer — and these names point into
     *       `std::define_static_string` storage, so under `-fsanitize=undefined` GCC 16.1.0
     *       refuses to fold that comparison and the whole `static_assert` collapses with
     *       `'(((const char*)(&"minor")) != 0)' is not a constant expression`. The property
     *       under test — that names() yields the
     *       four member ids in declaration order — is unchanged.
     */
    consteval bool names_feeds_the_usage_renderer() {
        std::string usage;
        for (std::string_view name : versioning.names()) {
            if (!usage.empty()) usage.push_back('|');
            for (const char byte : name) usage.push_back(byte);
        }
        return usage == "set-ver|major|minor|patch";
    }
    static_assert(names_feeds_the_usage_renderer());

    // ---------------------------------------------------------------------------
    // Runtime case — the one property that is about run time itself
    // ---------------------------------------------------------------------------

    CLAPP_TEST("group_spec: frozen groups are usable in a runtime container") {
        // A vector of group_spec is what a runtime-assembled tree would hold, and the one
        // witness that group_spec really is trivially copyable in practice.
        const std::vector<group_spec> groups{versioning, related, logging, placeholder};
        CLAPP_CHECK(groups.size() == 4);
        CLAPP_CHECK(groups[0].is_required_set());
        CLAPP_CHECK(groups[1].get_conflicts().size() == 3);
        CLAPP_CHECK(groups[2].is_multiple());
        CLAPP_CHECK(groups[3].empty());
    }

}  // namespace
