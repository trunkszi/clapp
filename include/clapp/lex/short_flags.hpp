/**
 * \file
 * \brief Short-option cluster cursor (clapp::short_flags) and number-shape test for `-1`.
 */

#pragma once

#include <clapp/lex/os_str.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>

namespace clapp {

    namespace detail {

        /**
         * \brief Whether \p text has clap's unsigned-decimal shape (not a parse).
         * \param text Bytes; any non-ASCII → false.
         * \return Digits, optional trailing `.`, optional `e`/`E` + digits; no sign/hex.
         * \warning **Empty string is accepted** (clap_lex) — makes `"-"` a negative
         *          number. Callers needing "≥1 digit" must check emptiness first.
         */
        [[nodiscard]] constexpr bool is_number(std::string_view text) noexcept {
            // Raw loop rather than a ranges pipeline: two flags carry state across
            // iterations, the legality of `.` and `e` depends on the index, and the
            // final verdict needs the exponent's position. A fold would hide all three.
            bool seen_dot           = false;
            bool seen_exponent      = false;
            std::size_t exponent_at = 0;

            for (std::size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (c >= '0' && c <= '9') continue;
                if (c == '.' && !seen_dot && !seen_exponent && i > 0) {
                    seen_dot = true;
                    continue;
                }
                if ((c == 'e' || c == 'E') && !seen_exponent && i > 0) {
                    seen_exponent = true;
                    exponent_at   = i;
                    continue;
                }
                return false;
            }

            // `1e` has an exponent marker but no exponent, so it is not a number.
            // `exponent_at > 0` is guaranteed above, hence no underflow here.
            if (!seen_exponent) return true;
            return exponent_at + 1 != text.size();
        }

        /**
         * \brief Length of the longest strict-UTF-8 prefix of \p text.
         * \return `text.size()` if fully valid; else first bad-byte offset (boundary).
         */
        [[nodiscard]] constexpr std::size_t utf8_prefix_length(std::string_view text) noexcept {
            const std::expected<void, invalid_encoding> checked = validate_utf8(text);
            return checked.has_value() ? text.size() : checked.error().valid_up_to;
        }

    }  // namespace detail

    /**
     * \brief Peel a leading `=` from a short option's attached value.
     * \param attached From short_flags::next_value_os() (keeps `=`).
     * \return `{value, true}` if `=` was stripped; else `{attached, false}`.
     * \note Distinguishes `-o=v` from `-ov` for `require_equals`. No clap_lex twin.
     */
    [[nodiscard]] constexpr std::pair<os_str, bool>
    split_attached_equals(os_str attached) noexcept {
        const std::optional<os_str> stripped = attached.strip_prefix(os_str{"="});
        if (stripped.has_value()) return {*stripped, true};
        return {attached, false};
    }

    /**
     * \brief Forward cursor over one `-abc` cluster (no leading `-`).
     * \note From parsed_arg::to_short(). next_flag() yields scalars; next_value_os()
     *       takes the remainder. Invalid UTF-8 suffix is one error value at the end.
     *       Copy is an independent cursor for lookahead.
     * \warning Non-owning; valid only while the argument buffer lives.
     */
    class short_flags {
    public:
        /**
         * \brief One flag as a Unicode scalar (`char32_t`, not C++ `char`).
         * \note Avoids splitting multi-byte flags (e.g. `-é`) or truncating them.
         */
        using flag_type = char32_t;

        /** \brief One step: decoded flag, or undecodable tail. */
        using flag_result = std::expected<flag_type, os_str>;

        /** \brief Exhausted empty cluster. */
        constexpr short_flags() noexcept = default;

        /**
         * \brief Cursor over \p cluster body.
         * \warning \p cluster must **already lack the leading `-`**. Passing the whole
         *          argument silently makes `-` the first flag. Prefer to_short().
         */
        constexpr explicit short_flags(os_str cluster) noexcept
            : cluster_(cluster),
              valid_prefix_(detail::utf8_prefix_length(cluster.chars())),
              suffix_pending_(valid_prefix_ < cluster.size()) {}

        /**
         * \brief Whether flags and invalid suffix are fully consumed.
         * \note Pending invalid suffix still counts as non-empty.
         */
        [[nodiscard]] constexpr bool is_empty() const noexcept {
            return !suffix_pending_ && cursor_ >= valid_prefix_;
        }

        /**
         * \brief Whether the unread remainder looks like an unsigned decimal number.
         * \return `false` if an invalid-UTF-8 suffix is still pending.
         * \note Call before iterating; after next_flag() it sees only the rest.
         */
        [[nodiscard]] constexpr bool is_negative_number() const noexcept {
            return !suffix_pending_ && detail::is_number(remaining_prefix());
        }

        /**
         * \brief Read one flag and advance.
         * \return `nullopt` when empty; scalar while prefix lasts; once, the whole
         *         invalid tail on the unexpected side.
         */
        [[nodiscard]] constexpr std::optional<flag_result> next_flag() noexcept {
            if (cursor_ < valid_prefix_) {
                // Guaranteed to succeed: the prefix was validated at construction, and
                // `valid_prefix_` lands on a sequence boundary.
                const detail::scan_result scan = detail::scan_one(cluster_.chars(), cursor_);
                cursor_ += scan.length;
                return flag_result{static_cast<flag_type>(scan.code_point)};
            }
            if (suffix_pending_) {
                suffix_pending_ = false;
                return flag_result{std::unexpect, invalid_suffix()};
            }
            return std::nullopt;
        }

        /**
         * \brief Take unread remainder as one value and exhaust the cursor.
         * \return Remainder from current position, or `nullopt` if empty.
         * \note Leading `=` is kept (`-o=v` → `"=v"`); use split_attached_equals().
         *       Never errors — invalid bytes are valid value bytes.
         */
        [[nodiscard]] constexpr std::optional<os_str> next_value_os() noexcept {
            if (cursor_ < valid_prefix_) {
                const os_str remainder = cluster_.substr(cursor_);
                cursor_                = valid_prefix_;
                suffix_pending_        = false;
                return remainder;
            }
            if (suffix_pending_) {
                suffix_pending_ = false;
                return invalid_suffix();
            }
            return std::nullopt;
        }

        /**
         * \brief Skip \p n flags.
         * \return Success, or how many were consumed before stop/undecodable.
         * \note On failure cursor is not rewound (clap_lex).
         */
        [[nodiscard]] constexpr std::expected<void, std::size_t>
        advance_by(std::size_t n) noexcept {
            // Raw loop rather than a ranges pipeline: the cursor is mutated in place,
            // and the failure value is the iteration index at which it stopped —
            // neither of which a fold or an algorithm over a view can express.
            for (std::size_t i = 0; i < n; ++i) {
                const std::optional<flag_result> flag = next_flag();
                if (!flag.has_value()) return std::unexpected(i);
                if (!flag->has_value()) return std::unexpected(i);
            }
            return {};
        }

        /** \brief Cluster body (`-` already removed); fixed for this cursor. */
        [[nodiscard]] constexpr os_str cluster() const noexcept { return cluster_; }

        /**
         * \brief Unread remainder (non-consuming; prefer next_value_os() in parse).
         */
        [[nodiscard]] constexpr os_str rest() const noexcept {
            return is_empty() ? os_str{} : cluster_.substr(cursor_);
        }

    private:
        /** The still-unread part of the valid-UTF-8 prefix. */
        [[nodiscard]] constexpr std::string_view remaining_prefix() const noexcept {
            return cluster_.chars().substr(cursor_, valid_prefix_ - cursor_);
        }

        /**
         * The tail starting at the first byte that is not valid UTF-8.
         * \pre An invalid suffix exists, i.e. `valid_prefix_ < cluster_.size()`.
         */
        [[nodiscard]] constexpr os_str invalid_suffix() const noexcept {
            return cluster_.substr(valid_prefix_);
        }

        os_str cluster_{};
        std::size_t valid_prefix_ = 0;
        std::size_t cursor_       = 0;
        bool suffix_pending_      = false;
    };

}  // namespace clapp
