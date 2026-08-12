/**
 * \file
 * \brief Compile-time naming helpers: word split, rename, affix strip, did-you-mean.
 */

#pragma once

#include <clapp/lex/os_str.hpp>
#include <clapp/meta/annotations.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace clapp {
    // =======================================================================
    // Word segmentation
    // =======================================================================

    /**
     * \brief `[begin, end)` byte range of one word (offsets for affix splicing).
     */
    struct word_span {
        std::size_t begin = 0; /**< First byte. */
        std::size_t end = 0;   /**< One past last. */

        /**
         * \brief Whether empty (word_cursor end sentinel).
         */
        [[nodiscard]] constexpr bool empty() const noexcept { return begin >= end; }

        /** \brief Length in bytes. */
        [[nodiscard]] constexpr std::size_t size() const noexcept {
            return empty() ? 0 : end - begin;
        }

        /** \brief Subview of \p text for this span. */
        [[nodiscard]] constexpr std::string_view of(std::string_view text) const noexcept {
            if (empty() || begin >= text.size()) return {};
            return std::string_view{text.data() + begin, size()};
        }

        [[nodiscard]] constexpr bool operator==(const word_span &) const noexcept = default;
    };

    namespace detail {
        /**
         * \brief Append \p piece via `push_back` (consteval-safe under ubsan).
         * \warning **Do not use `+=` / `append` / `string{ptr,n}`.** libstdc++
         *          `_M_mutate` tests the source pointer; under `-fsanitize=null` GCC
         *          refuses that cross-base compare in consteval (trap 10). Only this
         *          helper should append promoted views in consteval paths.
         */
        constexpr void append_bytes(std::string &out, std::string_view piece) {
            for (const char byte: piece) out.push_back(byte);
        }

        /**
         * \brief Append \p value in decimal (consteval; format/to_string are not).
         */
        constexpr void append_decimal(std::string &out, std::ptrdiff_t value) {
            if (value == 0) {
                out.push_back('0');
                return;
            }
            using unsigned_type = std::make_unsigned_t<std::ptrdiff_t>;
            const bool negative = value < 0;
            // Negate in the unsigned domain: -PTRDIFF_MIN overflows a signed negation.
            unsigned_type magnitude = negative
                                          ? unsigned_type{0} - static_cast<unsigned_type>(value)
                                          : static_cast<unsigned_type>(value);

            char digits[24]{};
            std::size_t count = 0;
            while (magnitude > 0) {
                digits[count] = static_cast<char>('0' + static_cast<char>(magnitude % 10));
                ++count;
                magnitude /= 10;
            }
            if (negative) out.push_back('-');
            while (count > 0) {
                --count;
                out.push_back(digits[count]);
            }
        }

        /**
         * \brief Word byte: ASCII alnum or any `>= 0x80` (UTF-8 stays one piece).
         * \note `_`/`-`/punct are separators (`auto_` drops trailing `_`).
         */
        [[nodiscard]] constexpr bool is_word_byte(char c) noexcept {
            const auto u = static_cast<unsigned char>(c);
            return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                   u >= 0x80;
        }

        /** \brief ASCII-only lowercase check. Bytes >= 0x80 are neither upper nor lower. */
        [[nodiscard]] constexpr bool is_ascii_lower(char c) noexcept {
            const auto u = static_cast<unsigned char>(c);
            return u >= 'a' && u <= 'z';
        }

        /** \brief ASCII-only uppercase check. Bytes >= 0x80 are neither upper nor lower. */
        [[nodiscard]] constexpr bool is_ascii_upper(char c) noexcept {
            const auto u = static_cast<unsigned char>(c);
            return u >= 'A' && u <= 'Z';
        }

        /**
         * \brief ASCII lowercasing only (not locale `tolower`).
         */
        [[nodiscard]] constexpr char to_lower(char c) noexcept {
            return is_ascii_upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
        }

        /** \brief ASCII-only uppercasing; any other byte passes through untouched. */
        [[nodiscard]] constexpr char to_upper(char c) noexcept {
            return is_ascii_lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
        }

        /** \brief Byte-wise, ASCII-case-insensitive equality. */
        [[nodiscard]] constexpr bool equals_ignore_ascii_case(std::string_view a,
                                                              std::string_view b) noexcept {
            return std::ranges::equal(
                a, b, [](char x, char y) { return to_lower(x) == to_lower(y); });
        }

        /** \brief State the word scanner carries across one byte. */
        enum class word_mode : unsigned char {
            boundary, /**< Nothing case-bearing seen yet in the current word. */
            lower, /**< The previous byte was ASCII lowercase. */
            upper, /**< The previous byte was ASCII uppercase. */
        };
    } // namespace detail

    /**
     * \brief Split an identifier into words (heck / clap_derive rules).
     * \note lower→upper opens a word; last capital of a run joins the next when
     *       followed by lower (`HTTPServer` → `HTTP` `Server`). Separators dropped.
     * \note next() returns empty span, not optional (optional ops not consteval under
     *       `_GLIBCXX_ASSERTIONS`).
     */
    class word_cursor {
    public:
        /**
         * \brief Scan \p identifier.
         * \warning Stores a view; must not outlive \p identifier.
         */
        constexpr explicit word_cursor(std::string_view identifier) noexcept : text_(identifier) {
        }

        /** \brief Identifier being segmented. */
        [[nodiscard]] constexpr std::string_view text() const noexcept { return text_; }

        /** \brief Whether all words were produced. */
        [[nodiscard]] constexpr bool exhausted() const noexcept { return pos_ >= text_.size(); }

        /**
         * \brief Next word span, or empty when exhausted.
         */
        [[nodiscard]] constexpr word_span next() noexcept {
            // Skip separators.
            while (pos_ < text_.size() && !detail::is_word_byte(text_[pos_])) ++pos_;
            if (pos_ >= text_.size()) return {};

            const std::size_t start = pos_;
            detail::word_mode mode = detail::word_mode::boundary;

            // A hand-written loop: the scan is a state machine whose next step depends
            // on both the current byte and its successor, and whose exit condition is
            // "stop *before* this byte" in one of the two cases. No views pipeline
            // expresses that without reintroducing the same state by hand.
            while (pos_ < text_.size() && detail::is_word_byte(text_[pos_])) {
                const char current = text_[pos_];

                const bool has_next =
                        pos_ + 1 < text_.size() && detail::is_word_byte(text_[pos_ + 1]);
                if (!has_next) {
                    // last byte of the run: it closes the word
                    ++pos_;
                    break;
                }

                const char next_byte = text_[pos_ + 1];
                const detail::word_mode next_mode =
                        detail::is_ascii_lower(current)
                            ? detail::word_mode::lower
                            : detail::is_ascii_upper(current)
                                  ? detail::word_mode::upper
                                  : mode;

                // Rule 1: lower -> upper. The boundary falls *after* `current`.
                if (next_mode == detail::word_mode::lower && detail::is_ascii_upper(next_byte)) {
                    ++pos_;
                    break;
                }

                // Rule 2: UPPER UPPER lower. The boundary falls *before* `current`,
                // handing that last capital to the next word. `mode` can only be
                // `upper` after at least one byte was consumed, so the word produced
                // here is never empty.
                if (mode == detail::word_mode::upper && detail::is_ascii_upper(current) &&
                    detail::is_ascii_lower(next_byte)) {
                    break;
                }

                mode = next_mode;
                ++pos_;
            }
            return {.begin = start, .end = pos_};
        }

    private:
        std::string_view text_;
        std::size_t pos_ = 0;
    };

    /**
     * \brief All words of \p identifier as views (transient in consteval).
     */
    [[nodiscard]] constexpr std::vector<std::string_view> words(std::string_view identifier) {
        std::vector<std::string_view> out;
        word_cursor cur{identifier};
        for (word_span w = cur.next(); !w.empty(); w = cur.next()) out.push_back(w.of(identifier));
        return out;
    }

    // =======================================================================
    // Case conversion
    // =======================================================================

    namespace detail {
        /** \brief How one word is re-cased when it is emitted. */
        enum class word_case : unsigned char {
            lower, /**< `HTTP` -> `http` */
            upper, /**< `Http` -> `HTTP` */
            capital, /**< `HTTP` -> `Http` (first byte up, the rest down) */
        };

        /** \brief The recipe for one clapp::naming style. */
        struct style_format {
            char separator = '\0'; /**< `'\0'` means the words are simply concatenated. */
            word_case first_word = word_case::lower; /**< Case conversion for the first word. */
            word_case rest = word_case::lower; /**< Case conversion for later words. */
        };

        /** \brief Recipe for \p style (not verbatim). */
        [[nodiscard]] constexpr style_format format_for(naming style) noexcept {
            switch (style) {
                case naming::kebab:
                    return {.separator = '-', .first_word = word_case::lower, .rest = word_case::lower};
                case naming::snake:
                    return {.separator = '_', .first_word = word_case::lower, .rest = word_case::lower};
                case naming::screaming_snake:
                    return {.separator = '_', .first_word = word_case::upper, .rest = word_case::upper};
                case naming::camel:
                    return {
                        .separator = '\0',
                        .first_word = word_case::lower,
                        .rest = word_case::capital
                    };
                case naming::pascal:
                    return {
                        .separator = '\0',
                        .first_word = word_case::capital,
                        .rest = word_case::capital
                    };
                case naming::lower:
                    return {
                        .separator = '\0',
                        .first_word = word_case::lower,
                        .rest = word_case::lower
                    };
                case naming::upper:
                    return {
                        .separator = '\0',
                        .first_word = word_case::upper,
                        .rest = word_case::upper
                    };
                case naming::verbatim:
                    break;
            }
            return {};
        }

        /**
         * \brief Drop trailing `_` (keyword avoidance); applied for every naming style.
         */
        [[nodiscard]] constexpr std::string_view
        strip_trailing_underscores(std::string_view identifier) noexcept {
            std::size_t end = identifier.size();
            while (end > 0 && identifier[end - 1] == '_') --end;
            identifier.remove_suffix(identifier.size() - end);
            return identifier;
        }

        /** \brief Append \p word to \p out, re-cased according to \p style. */
        constexpr void append_word(std::string &out, std::string_view word, word_case style) {
            if (word.empty()) return;
            switch (style) {
                case word_case::lower:
                    std::ranges::copy(word | std::views::transform(to_lower), std::back_inserter(out));
                    return;
                case word_case::upper:
                    std::ranges::copy(word | std::views::transform(to_upper), std::back_inserter(out));
                    return;
                case word_case::capital:
                    out.push_back(to_upper(word.front()));
                    std::ranges::copy(word | std::views::drop(1) | std::views::transform(to_lower),
                                      std::back_inserter(out));
                    return;
            }
        }
    } // namespace detail

    /**
     * \brief Exact `rename(identifier, style).size()` without allocating.
     */
    [[nodiscard]] constexpr std::size_t rename_size(std::string_view identifier,
                                                    naming style) noexcept {
        if (style == naming::verbatim) {
            return detail::strip_trailing_underscores(identifier).size();
        }
        const bool separated = detail::format_for(style).separator != '\0';
        std::size_t total = 0;
        std::size_t count = 0;
        word_cursor cur{identifier};
        for (word_span w = cur.next(); !w.empty(); w = cur.next()) {
            total += w.size();
            ++count;
        }
        if (count == 0) return 0;
        return separated ? total + count - 1 : total;
    }

    /**
     * \brief Rewrite a C++ identifier into a command-line name (kebab/snake/…).
     * \param identifier Usually `identifier_of`.
     * \param style From `rename_all` / `rename_all_env`.
     * \return Owning string; empty if no word bytes. Verbatim still strips trailing `_`.
     * \warning Consteval result is transient — promote with `define_static_string`
     *          before returning a view or storing as constexpr.
     */
    [[nodiscard]] constexpr std::string rename(std::string_view identifier, naming style) {
        if (style == naming::verbatim) {
            return std::string{detail::strip_trailing_underscores(identifier)};
        }

        const detail::style_format fmt = detail::format_for(style);

        std::string out;
        out.reserve(rename_size(identifier, style));

        word_cursor cur{identifier};
        bool first = true;
        for (word_span w = cur.next(); !w.empty(); w = cur.next()) {
            if (!first && fmt.separator != '\0') out.push_back(fmt.separator);
            detail::append_word(out, w.of(identifier), first ? fmt.first_word : fmt.rest);
            first = false;
        }
        return out;
    }

    /**
     * \brief First byte of `rename(identifier, style)` (short flag; cased long name).
     * \return `'\0'` if the name is empty.
     */
    [[nodiscard]] constexpr char rename_initial(std::string_view identifier,
                                                naming style) noexcept {
        if (style == naming::verbatim) {
            const std::string_view trimmed = detail::strip_trailing_underscores(identifier);
            return trimmed.empty() ? '\0' : trimmed.front();
        }
        const detail::style_format fmt = detail::format_for(style);
        word_cursor cur{identifier};
        const word_span first = cur.next();
        if (first.empty()) return '\0';
        const char head = first.of(identifier).front();
        switch (fmt.first_word) {
            case detail::word_case::lower:
                return detail::to_lower(head);
            case detail::word_case::upper:
            case detail::word_case::capital:
                return detail::to_upper(head);
        }
        return head;
    }

    // =======================================================================
    // Type-name affix stripping
    // =======================================================================

    /**
     * \brief Strip whole-word `cmd`/`args` affixes from a type name (case-insensitive).
     * \param type_name Usually `identifier_of(^^T)`.
     * \return Subview of \p type_name; unchanged if <2 words, no match, or empty result.
     * \note Fixed list (not configurable); still needs rename() after.
     */
    [[nodiscard]] constexpr std::string_view
    strip_type_affixes(std::string_view type_name) noexcept {
        word_span first{};
        word_span second{};
        word_span penultimate{};
        word_span last{};
        std::size_t count = 0;

        // Raw loop: only four of the N words matter, so materialising them all into a
        // container (which would also allocate) buys nothing.
        word_cursor cur{type_name};
        for (word_span w = cur.next(); !w.empty(); w = cur.next()) {
            if (count == 0)
                first = w;
            else if (count == 1)
                second = w;
            penultimate = last;
            last = w;
            ++count;
        }

        if (count < 2) return type_name;

        const std::string_view head = first.of(type_name);
        const std::string_view tail = last.of(type_name);

        const bool drop_head = detail::equals_ignore_ascii_case(head, "cmd");
        const bool drop_tail = detail::equals_ignore_ascii_case(tail, "cmd") ||
                               detail::equals_ignore_ascii_case(tail, "args");
        if (!drop_head && !drop_tail) return type_name;

        const std::size_t begin = drop_head ? second.begin : first.begin;
        const std::size_t end = drop_tail ? penultimate.end : last.end;
        if (begin >= end) return type_name; // e.g. `cmd_args`: nothing would survive
        return std::string_view{type_name.data() + begin, end - begin};
    }

    /**
     * \brief Subcommand name: strip_type_affixes then rename (promote if consteval).
     */
    [[nodiscard]] constexpr std::string subcommand_name(std::string_view type_name,
                                                        naming style = naming::kebab) {
        return rename(strip_type_affixes(type_name), style);
    }

    // =======================================================================
    // Similarity metrics
    // =======================================================================

    /**
     * \brief Unrestricted Damerau-Levenshtein distance (bytes; not OSA).
     * \return Edit distance; `"ca"`→`"abc"` is 2 (OSA would be 3).
     */
    [[nodiscard]] constexpr std::size_t damerau_levenshtein(std::string_view a,
                                                            std::string_view b) {
        const std::size_t m = a.size();
        const std::size_t n = b.size();
        if (m == 0) return n;
        if (n == 0) return m;

        // Lowrance-Wagner: the table is indexed from -1, so every index is shifted by
        // one and `at(i, j)` denotes the classical d[i-1][j-1].
        const std::size_t infinity = m + n;
        const std::size_t stride = n + 2;
        std::vector<std::size_t> table((m + 2) * stride, 0);
        const auto at = [&table, stride](std::size_t i, std::size_t j) -> std::size_t & {
            return table[i * stride + j];
        };

        at(0, 0) = infinity;
        for (std::size_t i = 0; i <= m; ++i) {
            at(i + 1, 0) = infinity;
            at(i + 1, 1) = i;
        }
        for (std::size_t j = 0; j <= n; ++j) {
            at(0, j + 1) = infinity;
            at(1, j + 1) = j;
        }

        // last_row[c] is the largest i such that a[i-1] == c; 0 when c is unseen.
        // A 256-entry table rather than a map: bytes are the alphabet, and this keeps
        // the whole function allocation-light enough for constant evaluation.
        std::array<std::size_t, 256> last_row{};

        for (std::size_t i = 1; i <= m; ++i) {
            std::size_t last_col = 0;
            for (std::size_t j = 1; j <= n; ++j) {
                const std::size_t k = last_row[static_cast<unsigned char>(b[j - 1])];
                const std::size_t l = last_col;
                std::size_t cost = 1;
                if (a[i - 1] == b[j - 1]) {
                    cost = 0;
                    last_col = j;
                }
                at(i + 1, j + 1) = std::ranges::min({
                    at(i, j) + cost, // substitution
                    at(i + 1, j) + 1, // insertion
                    at(i, j + 1) + 1, // deletion
                    at(k, l) + (i - k - 1) + 1 + (j - l - 1), // transposition
                });
            }
            last_row[static_cast<unsigned char>(a[i - 1])] = i;
        }
        return at(m + 1, n + 1);
    }

    namespace detail {
        /**
         * \brief Unicode scalar units for jaro() (malformed byte → `0x110000+byte`).
         * \warning **Must not compare raw bytes** — wrong match window for CJK and
         *          false matches on shared UTF-8 continuations (diverges from strsim).
         */
        [[nodiscard]] constexpr std::vector<char32_t> similarity_units(std::string_view text) {
            std::vector<char32_t> units;
            units.reserve(text.size());
            for (std::size_t i = 0; i < text.size();) {
                const scan_result scan = scan_one(text, i);
                if (scan.ok) {
                    units.push_back(static_cast<char32_t>(scan.code_point));
                } else {
                    // scan.length is the maximal subpart, always >= 1, so this
                    // terminates and every byte of a malformed run gets its own unit.
                    for (std::size_t k = 0; k < scan.length && i + k < text.size(); ++k)
                        units.push_back(static_cast<char32_t>(0x11'0000u + byte_at(text, i + k)));
                }
                i += scan.length;
            }
            return units;
        }
    } // namespace detail

    /**
     * \brief Jaro similarity in `[0.0, 1.0]` matching `strsim::generic_jaro`.
     * \note Units are Unicode scalars (not bytes). Transpositions = half the
     *       matched-subsequence mismatches (integer divide). Window is
     *       saturating `max/2 - 1`. No Jaro-Winkler (clap#4660).
     */
    [[nodiscard]] constexpr double jaro(std::string_view a, std::string_view b) {
        const std::vector<char32_t> av = detail::similarity_units(a);
        const std::vector<char32_t> bv = detail::similarity_units(b);

        const std::size_t la = av.size();
        const std::size_t lb = bv.size();
        if (la == 0 && lb == 0) return 1.0;
        if (la == 0 || lb == 0) return 0.0;

        const std::size_t half = std::max(la, lb) / 2;
        const std::size_t window = half == 0 ? 0 : half - 1;

        // Which units took part in a match, on each side. strsim keeps both flag
        // arrays; the transposition rule below needs `a`'s as much as `b`'s.
        std::vector<char> a_matched(la, char{0});
        std::vector<char> b_matched(lb, char{0});
        std::size_t matches = 0;

        for (std::size_t i = 0; i < la; ++i) {
            const std::size_t lo = i > window ? i - window : 0;
            const std::size_t hi = std::min(lb - 1, i + window);
            if (lo > hi) continue;
            for (std::size_t j = lo; j <= hi; ++j) {
                if (av[i] != bv[j] || b_matched[j] != 0) continue;
                a_matched[i] = 1;
                b_matched[j] = 1;
                ++matches;
                break;
            }
        }

        if (matches == 0) return 0.0;

        // strsim's transposition rule, verbatim: zip the two matched subsequences —
        // both hold exactly `matches` entries — count the positions whose units
        // differ, then halve. The halving is integer division, and it is what makes a
        // single swapped pair cost one transposition rather than two.
        std::size_t transpositions = 0;
        std::size_t j = 0;
        for (std::size_t i = 0; i < la; ++i) {
            if (a_matched[i] == 0) continue;
            while (j < lb && b_matched[j] == 0) ++j;
            if (j >= lb) break; // unreachable: the two flag counts are equal.
            if (av[i] != bv[j]) ++transpositions;
            ++j;
        }
        transpositions /= 2;

        const double m = static_cast<double>(matches);
        return (m / static_cast<double>(la) + m / static_cast<double>(lb) +
                (m - static_cast<double>(transpositions)) / m) /
               3.0;
    }

    /**
     * \brief Jaro score must **exceed** this (0.7, strict) for a suggestion.
     */
    inline constexpr double did_you_mean_threshold = 0.7;

    /** \brief Range of elements convertible to `std::string_view`. */
    template<class R>
    concept string_view_range =
            std::ranges::input_range<R> &&
            std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>;

    /**
     * \brief Candidates above threshold, ascending by score (best last; clap order).
     * \warning Results alias \p candidates; nothing is copied.
     */
    template<string_view_range R>
    [[nodiscard]] constexpr std::vector<std::string_view> did_you_mean(std::string_view input,
                                                                       R &&candidates) {
        using scored = std::pair<double, std::string_view>;
        std::vector<scored> ranked;

        for (const std::string_view candidate: candidates) {
            const double confidence = jaro(input, candidate);
            if (confidence <= did_you_mean_threshold) continue;
            // upper_bound gives the first strictly-greater score, so equal scores keep
            // their relative order — the same insertion point clap's binary_search_by
            // computes.
            const auto pos = std::ranges::upper_bound(ranked, confidence, {}, &scored::first);
            ranked.insert(pos, scored{confidence, candidate});
        }

        return ranked | std::views::values | std::ranges::to<std::vector>();
    }

    /**
     * \brief Best suggestion for \p input, or nullopt (later ties win; aliases input).
     * \warning Result aliases \p candidates.
     */
    template<string_view_range R>
    [[nodiscard]] constexpr std::optional<std::string_view> best_match(std::string_view input,
                                                                       R &&candidates) {
        std::optional<std::string_view> best;
        double best_confidence = 0.0;
        for (const std::string_view candidate: candidates) {
            const double confidence = jaro(input, candidate);
            if (confidence <= did_you_mean_threshold) continue;
            if (confidence >= best_confidence) {
                // `>=`: later ties win, as in clap
                best_confidence = confidence;
                best = candidate;
            }
        }
        return best;
    }
} // namespace clapp
