/**
 * \file
 * \brief clapp::arg_id and compile-time interner clapp::id_table.
 */

#pragma once

#include <clapp/detail/std_meta.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <cstdlib>
#include <functional>
#include <ranges>
#include <span>
#include <string_view>

namespace clapp {

    /**
     * \brief Identifier of an argument, group, or subcommand (structural, 16 bytes).
     * \note Public #text/#length/#slot for structurality; use member functions.
     * \warning **Structural ≠ static storage.** `arg_id{"verbose"}` is invalid as a
     *          template arg / `define_static_array` element (literal is not a variable).
     *          Promote first: `arg_id{std::string_view{std::define_static_string(name)}, slot}`.
     * \warning **Absence is zero #length, never null #text.** `text == nullptr` fails
     *          consteval under ubsan (`-fsanitize=null` refuses cross-base pointer
     *          compares). No member compares #text; optional positions use empty().
     *          A non-null sentinel does not help (still different bases).
     */
    struct arg_id {
        /** \brief #slot value meaning "not interned". */
        static constexpr std::uint32_t unbound = 0xFFFF'FFFFu;

        /**
         * \name Structural storage
         * \{
         */

        /** Name pointer; never owns; **never compared** (length sentinel). */
        const char* text = nullptr;
        /** Name length in bytes; zero means empty/absent in optional positions. */
        std::uint32_t length = 0;
        /** Slot in owning id_table, or #unbound. */
        std::uint32_t slot = unbound;

        /** \} */

        /**
         * \brief Empty unbound id (equals `""` / clap `Id::EXTERNAL`).
         */
        constexpr arg_id() = default;

        /**
         * \brief From string literal (static storage).
         * \warning **Explicit** so `id == "verbose"` is not ambiguous with
         *          `string_view` conversion.
         */
        template<std::size_t N>
        consteval explicit arg_id(const char (&literal)[N])
            : text(literal), length(static_cast<std::uint32_t>(N - 1)) {}

        /**
         * \brief From a name with static storage duration.
         * \param name Spelling. \param position Table slot.
         * \warning \p name is **borrowed**. Promote consteval results with
         *          `define_static_string` or the id dangles if it escapes.
         */
        constexpr arg_id(std::string_view name, std::uint32_t position) noexcept
            : text(name.data()), length(static_cast<std::uint32_t>(name.size())), slot(position) {}

        /**
         * \brief Unbound id from a static-storage name.
         * \warning \p name is borrowed.
         */
        constexpr explicit arg_id(std::string_view name) noexcept : arg_id(name, unbound) {}

        /**
         * \brief Spelling as string_view.
         * \note Uses #length only; never compares #text (ubsan-safe empty view).
         */
        [[nodiscard]] constexpr std::string_view name() const noexcept {
            return std::string_view{text, length};
        }

        /**
         * \brief Whether the name is empty (absent in optional positions).
         */
        [[nodiscard]] constexpr bool empty() const noexcept { return length == 0; }

        /** \brief Whether an id_table assigned #slot. */
        [[nodiscard]] constexpr bool bound() const noexcept { return slot != unbound; }

        /**
         * \brief Equality **by name** (not by #slot — slots are per-table).
         */
        [[nodiscard]] constexpr bool operator==(const arg_id& other) const noexcept {
            return name() == other.name();
        }

        /** \brief Lexicographic order by name. */
        [[nodiscard]] constexpr std::strong_ordering
        operator<=>(const arg_id& other) const noexcept {
            return name() <=> other.name();
        }

        /** \brief Compare spelling without building an arg_id. */
        [[nodiscard]] constexpr bool operator==(std::string_view other) const noexcept {
            return name() == other;
        }

        /** \brief Order against a spelling (heterogeneous lookup). */
        [[nodiscard]] constexpr std::strong_ordering
        operator<=>(std::string_view other) const noexcept {
            return name() <=> other;
        }
    };

    /**
     * \name Built-in ids (clap `Id::*`); `_id` suffix avoids `help()` collision.
     * \{
     */

    /** \brief Built-in `--help`. */
    inline constexpr arg_id help_id{"help"};
    /** \brief Built-in `--version`. */
    inline constexpr arg_id version_id{"version"};
    /** \brief External subcommand (`""`). */
    inline constexpr arg_id external_id{""};

    /** \} */

    /**
     * \brief arg_id with name lifted via `std::define_static_string`.
     * \param name Spelling (copied; may be transient).
     * \param slot Table slot or unbound.
     * \return Id whose `text` has static storage (template/`define_static_array` safe).
     */
    [[nodiscard]] consteval arg_id make_static_id(std::string_view name,
                                                  std::uint32_t slot = arg_id::unbound) {
        return arg_id{std::string_view{std::define_static_string(name)}, slot};
    }

    /**
     * \brief Fixed-capacity name→slot interner (one per command; linear lookup).
     * \tparam Cap Max distinct ids.
     * \note Slots are insertion order (display_order / digraph vertex when wired).
     * \warning **Not yet instantiated by freeze().** Builder make_static_id() leaves
     *          slots unbound (`0xFFFFFFFF`); do not treat `.slot` as a vertex index yet.
     */
    template<std::size_t Cap>
    class id_table {
    public:
        /** \brief Maximum number of ids. */
        static constexpr std::size_t capacity = Cap;

        /** \brief An empty table. */
        constexpr id_table() noexcept = default;

        /** \brief Number of interned ids. */
        [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

        /** \brief Whether nothing has been interned. */
        [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

        /** \brief Whether \p name has already been interned. */
        [[nodiscard]] constexpr bool contains(std::string_view name) const noexcept {
            return find(name).bound();
        }

        /**
         * \brief Look up \p name.
         * \return Interned id, or unbound; check bound() (empty name is special).
         */
        [[nodiscard]] constexpr arg_id find(std::string_view name) const noexcept {
            const std::span<const arg_id> interned = entries();
            const auto hit =
                    std::ranges::find(interned, name, [](const arg_id& id) { return id.name(); });
            return hit == interned.end() ? arg_id{} : *hit;
        }

        /**
         * \brief Id at \p slot, or unbound if past end.
         */
        [[nodiscard]] constexpr arg_id at(std::size_t slot) const noexcept {
            return slot < count_ ? entries_[slot] : arg_id{};
        }

        /**
         * \brief Intern \p name (idempotent).
         * \return Existing or new id (slot = previous size()).
         * \warning \p name is borrowed — promote consteval names first.
         * \note Cap overflow: aborts constant evaluation; unbound at runtime.
         */
        constexpr arg_id intern(std::string_view name) {
            const arg_id existing = find(name);
            if (existing.bound()) return existing;
            if (count_ >= Cap) {
                if consteval {
                    std::abort();
                }
                return arg_id{};
            }
            const arg_id fresh{name, static_cast<std::uint32_t>(count_)};
            entries_[count_] = fresh;
            ++count_;
            return fresh;
        }

        /**
         * \brief Interned ids in slot order.
         * \warning Borrows `*this`; promote with `define_static_array` to escape consteval.
         */
        [[nodiscard]] constexpr std::span<const arg_id> entries() const noexcept {
            return std::span<const arg_id>{entries_.data(), count_};
        }

        /** \brief Begin (slot order). */
        [[nodiscard]] constexpr const arg_id* begin() const noexcept { return entries_.data(); }
        /** \brief End. */
        [[nodiscard]] constexpr const arg_id* end() const noexcept {
            return entries_.data() + count_;
        }

        /**
         * \brief Lazy name views (for did_you_mean).
         * \warning Borrows `*this`.
         */
        [[nodiscard]] constexpr auto names() const noexcept {
            return entries() | std::views::transform([](const arg_id& id) { return id.name(); });
        }

    private:
        std::array<arg_id, Cap> entries_{};
        std::size_t count_ = 0;
    };

}  // namespace clapp

/** \brief Hash by name (consistent with operator==; ignores slot). */
template<>
struct std::hash<clapp::arg_id> {
    [[nodiscard]] std::size_t operator()(const clapp::arg_id& id) const noexcept {
        return std::hash<std::string_view>{}(id.name());
    }
};
