/**
 * \file
 * \brief clapp::group_builder and frozen clapp::group_spec (named argument sets).
 */

#pragma once

// For clapp::detail::promote_ids — the shared "lift a list of owning strings into a
// static-storage clapp::arg_id array" step that both freeze() implementations need.
// Duplicating it here instead would be a redefinition the moment both headers are
// included, and a group's members are argument ids, so the dependency is real rather
// than incidental.
#include <clapp/builder/arg.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {
    /**
     * \brief Frozen named argument set with shared validation (from group_builder::freeze).
     * \note Structural: pointer+count pairs, not span/optional, for .rodata promotion.
     */
    struct group_spec {
        /** \brief The group's unique name; also the id matches report it under. */
        arg_id id{};

        /** \brief The member argument ids. */
        const arg_id *arg_data = nullptr;
        /** \brief How many members there are. */
        std::size_t arg_count = 0;

        /** \brief Arguments (or groups) that become required when this group is used. */
        const arg_id *requires_data = nullptr;
        /** \brief How many such requirements there are. */
        std::size_t requires_count = 0;

        /** \brief Arguments (or groups) that may not appear alongside this one. */
        const arg_id *conflict_data = nullptr;
        /** \brief How many such conflicts there are. */
        std::size_t conflict_count = 0;

        /** \brief Whether one of the members must appear. */
        bool required = false;

        /** \brief Whether more than one member may appear. */
        bool multiple = false;

        /** \brief The group's unique name. */
        [[nodiscard]] constexpr arg_id get_id() const noexcept { return id; }

        /** \brief The member argument ids, in declaration order. */
        [[nodiscard]] constexpr std::span<const arg_id> get_args() const noexcept {
            return {arg_data, arg_count};
        }

        /** \brief The ids this group requires. */
        [[nodiscard]] constexpr std::span<const arg_id> get_requires() const noexcept {
            return {requires_data, requires_count};
        }

        /** \brief The ids this group conflicts with. */
        [[nodiscard]] constexpr std::span<const arg_id> get_conflicts() const noexcept {
            return {conflict_data, conflict_count};
        }

        /**
         * \brief Whether \p name is a member of this group.
         * \param name The argument id to look for.
         * \note Linear, which is the right shape here: groups hold a handful of ids and
         *       a scan over a contiguous array beats both a hash and a tree at that
         *       size — see the same reasoning on clapp::id_table.
         */
        [[nodiscard]] constexpr bool contains(std::string_view name) const noexcept {
            return std::ranges::any_of(get_args(),
                                       [&](const arg_id &member) { return member == name; });
        }

        /** \brief Whether the group is empty. */
        [[nodiscard]] constexpr bool empty() const noexcept { return arg_count == 0; }

        /** \brief How many members the group has. */
        [[nodiscard]] constexpr std::size_t size() const noexcept { return arg_count; }

        /** \brief Reports clapp::group_builder::required(). */
        [[nodiscard]] constexpr bool is_required_set() const noexcept { return required; }

        /** \brief Reports clapp::group_builder::multiple(). */
        [[nodiscard]] constexpr bool is_multiple() const noexcept { return multiple; }

        /**
         * \brief The member spellings, as a lazy range — shaped for
         *        clapp::did_you_mean() and for rendering `<a|b|c>` in usage.
         */
        [[nodiscard]] constexpr auto names() const noexcept {
            return get_args() |
                   std::views::transform([](const arg_id &member) { return member.name(); });
        }

        /**
         * \brief Equality **by content**, not by pointer.
         * \note Hand-written for the reason clapp::arg_spec::operator== gives: a
         *       defaulted comparison would compare the `const arg_id*` members as
         *       addresses, which happens to work only because
         *       `std::define_static_array` deduplicates.
         */
        [[nodiscard]] constexpr bool operator==(const group_spec &other) const noexcept {
            return id.name() == other.id.name() && required == other.required &&
                   multiple == other.multiple && std::ranges::equal(get_args(), other.get_args()) &&
                   std::ranges::equal(get_requires(), other.get_requires()) &&
                   std::ranges::equal(get_conflicts(), other.get_conflicts());
        }
    };

    /**
     * \brief Chainable description of a group_spec (clap ArgGroup).
     * \warning Never `static constexpr`: holds vectors (transient in consteval). Freeze
     *          inside a consteval function and return the group_spec.
     * \warning Never `g = std::move(g).required();` — self-move can empty the vectors.
     *          Call setters as statements.
     */
    class group_builder {
    public:
        /**
         * \brief Create a group with the unique name \p id.
         * \param id The name used to refer to the group in conflict and requirement
         *        rules, and the id its matched member is reported under. Copied.
         * \note A fresh group is neither required nor multiple, exactly as in clap.
         */
        constexpr explicit group_builder(std::string_view id) : id_(id) {
        }

        /** \brief Rename the group. \param name The new unique name. */
        constexpr group_builder &&id(std::string_view name) && {
            id_ = std::string{name};
            return std::move(*this);
        }

        /**
         * \brief Add \p arg_id to the group.
         * \param arg_id The id of an argument (or of another group).
         */
        constexpr group_builder &&arg(std::string_view arg_id) && {
            args_.emplace_back(arg_id);
            return std::move(*this);
        }

        /** \brief Add several members. */
        template<string_view_range R>
        constexpr group_builder &&args(R &&arg_ids) && {
            for (std::string_view member: arg_ids) args_.emplace_back(member);
            return std::move(*this);
        }

        /** \brief Add several members from a braced list. */
        constexpr group_builder &&args(std::initializer_list<std::string_view> arg_ids) && {
            return std::move(*this).args<std::initializer_list<std::string_view> &>(arg_ids);
        }

        /**
         * \brief Allow more than one member to appear.
         * \param yes `false` — the default — makes the members mutually exclusive.
         */
        constexpr group_builder &&multiple(bool yes = true) && {
            multiple_ = yes;
            return std::move(*this);
        }

        /**
         * \brief Require one of the members to appear.
         *
         * \param yes Whether the group is mandatory.
         * \note Usage renders a required group as `<a|b|c>`. A required group is still
         *       satisfiable by a conflicting rule elsewhere excusing it, which is why
         *       this is a group property rather than `required()` on every member.
         */
        constexpr group_builder &&required(bool yes = true) && {
            required_ = yes;
            return std::move(*this);
        }

        /** \brief When any member is used, \p other becomes required. */
        constexpr group_builder &&requires_(std::string_view other) && {
            requirements_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief When any member is used, every id in \p others becomes required. */
        template<string_view_range R>
        constexpr group_builder &&requires_all(R &&others) && {
            for (std::string_view other: others) requirements_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `requires_all` from a braced list. */
        constexpr group_builder &&requires_all(std::initializer_list<std::string_view> others) && {
            return std::move(*this).requires_all<std::initializer_list<std::string_view> &>(others);
        }

        /** \brief No member may appear alongside \p other. */
        constexpr group_builder &&conflicts_with(std::string_view other) && {
            conflicts_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief No member may appear alongside any of \p others. */
        template<string_view_range R>
        constexpr group_builder &&conflicts_with_all(R &&others) && {
            for (std::string_view other: others) conflicts_.emplace_back(other);
            return std::move(*this);
        }

        /** \brief `conflicts_with_all` from a braced list. */
        constexpr group_builder &&
        conflicts_with_all(std::initializer_list<std::string_view> others) && {
            return std::move(*this).conflicts_with_all<std::initializer_list<std::string_view> &>(
                others);
        }

        /**
         * \name Reflection
         * \{
         */

        /** \brief The group's unique name. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::string_view get_id() const noexcept { return id_; }

        /** \brief The member ids, in declaration order. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_args() const noexcept {
            return std::span<const std::string>{args_};
        }

        /** \brief The ids this group requires. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_requires() const noexcept {
            return std::span<const std::string>{requirements_};
        }

        /** \brief The ids this group conflicts with. \warning Borrows `*this`. */
        [[nodiscard]] constexpr std::span<const std::string> get_conflicts() const noexcept {
            return std::span<const std::string>{conflicts_};
        }

        /** \brief Whether \p name is already a member. */
        [[nodiscard]] constexpr bool contains(std::string_view name) const noexcept {
            return std::ranges::contains(args_, name);
        }

        /** \brief Whether the group has no members yet. */
        [[nodiscard]] constexpr bool empty() const noexcept { return args_.empty(); }

        /** \brief How many members the group has. */
        [[nodiscard]] constexpr std::size_t size() const noexcept { return args_.size(); }

        /** \brief Reports required(). */
        [[nodiscard]] constexpr bool is_required_set() const noexcept { return required_; }

        /**
         * \brief Reports multiple().
         *
         * \note Non-`const`, unlike every other accessor here, would match clap — its
         *       `ArgGroup::is_multiple` takes `&mut self` for no reason anyone has
         *       written down. clapp makes it `const`; nothing observable changes.
         */
        [[nodiscard]] constexpr bool is_multiple() const noexcept { return multiple_; }

        /** \} */

        /**
         * \brief Validate and promote into a group_spec in static storage.
         * \return Frozen group (strings in static storage; id unbound to any id_table).
         * \note Rejects an empty id, self-member, or duplicate member. Empty member list
         *       is allowed (command_of may create the group before members join).
         */
        [[nodiscard]] consteval group_spec freeze() const {
            if (id_.empty()) std::abort();
            for (std::size_t i = 0; i < args_.size(); ++i) {
                if (args_[i] == id_) std::abort();
                for (std::size_t j = i + 1; j < args_.size(); ++j) {
                    if (args_[i] == args_[j]) std::abort();
                }
            }

            const std::span<const arg_id> members = detail::promote_ids(args_);
            const std::span<const arg_id> required = detail::promote_ids(requirements_);
            const std::span<const arg_id> conflicts = detail::promote_ids(conflicts_);

            return group_spec{
                .id = make_static_id(id_),
                .arg_data = members.data(),
                .arg_count = members.size(),
                .requires_data = required.data(),
                .requires_count = required.size(),
                .conflict_data = conflicts.data(),
                .conflict_count = conflicts.size(),
                .required = required_,
                .multiple = multiple_,
            };
        }

    private:
        std::string id_;
        std::vector<std::string> args_;
        bool required_ = false;
        std::vector<std::string> requirements_;
        std::vector<std::string> conflicts_;
        bool multiple_ = false;
    };

    namespace detail {
        /**
         * Compile-time contract: a clapp::group_spec must stay structural, or an array
         * of groups cannot be promoted into `.rodata` alongside the arguments.
         */
        template<group_spec>
        struct group_spec_probe {
        };

        /** \brief Proof that clapp::group_spec is a structural type. */
        using group_spec_is_structural = group_spec_probe<group_spec{}>;

        /**
         * A default-constructed group is the neutral one: no members, not required,
         * not multiple. `command_of<T>()` relies on that when it creates a group before
         * it knows who joins it.
         */
        static_assert(group_spec{}.empty());
        static_assert(!group_spec{}.is_required_set());
        static_assert(!group_spec{}.is_multiple());
        static_assert(!group_spec{}.contains("anything"));
    } // namespace detail
} // namespace clapp
