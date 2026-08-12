/**
 * \file
 * \brief Platform-neutral view over raw command-line bytes: clapp::os_str /
 *        clapp::os_string, plus the WTF-8 / UTF-8 validators they rest on.
 */

#pragma once


#include <compare>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <expected>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace clapp {

    /**
     * \brief Host OS character type: `char` on POSIX, `wchar_t` (UTF-16) on Windows.
     * \note Only place the OS encoding difference is visible; everything else is WTF-8.
     */
#ifdef _WIN32
    using native_char = wchar_t;
#else
    using native_char = char;
#endif

    /** \brief Owning string in the host operating system's own encoding. */
    using native_string = std::basic_string<native_char>;

    /** \brief Non-owning view in the host operating system's own encoding. */
    using native_string_view = std::basic_string_view<native_char>;

    /** \brief Why a byte sequence failed UTF-8 / WTF-8 validation. */
    enum class encoding_error : unsigned char {
        /** Continuation byte where a lead was expected. */
        unexpected_continuation,
        /** Lead that never starts a sequence (`0xF8`..`0xFF`). */
        invalid_lead,
        /** Input ended mid multi-byte sequence. */
        truncated,
        /** Lead not followed by a continuation byte. */
        missing_continuation,
        /** Overlong form (could smuggle slash or NUL past a byte filter). */
        overlong,
        /** U+D800..U+DFFF: invalid in UTF-8, valid in WTF-8. */
        surrogate,
        /** Code point above U+10FFFF. */
        out_of_range,
        /** WTF-8: unmerged high+low surrogate pair (breaks bijective UTF-16 map). */
        surrogate_pair,
    };

    /**
     * \brief A human-readable name for \p kind, for error messages.
     * \param kind The failure to describe.
     * \return A view into static storage; safe to keep indefinitely.
     */
    [[nodiscard]] constexpr std::string_view describe(encoding_error kind) noexcept {
        switch (kind) {
        case encoding_error::unexpected_continuation:
            return "continuation byte without a lead byte";
        case encoding_error::invalid_lead:
            return "byte that cannot start a UTF-8 sequence";
        case encoding_error::truncated:
            return "input ended inside a multi-byte sequence";
        case encoding_error::missing_continuation:
            return "lead byte not followed by a continuation byte";
        case encoding_error::overlong:
            return "overlong encoding";
        case encoding_error::surrogate:
            return "encoded surrogate code point (U+D800..U+DFFF)";
        case encoding_error::out_of_range:
            return "code point above U+10FFFF";
        case encoding_error::surrogate_pair:
            return "unmerged surrogate pair (not well-formed WTF-8)";
        }
        return "unknown encoding error";
    }

    /**
     * \brief Error side of decoding: offset of the bad sequence plus its length.
     * \note Shaped like Rust `Utf8Error`; #valid_up_to is the longest valid prefix.
     */
    struct invalid_encoding {
        /** \brief Byte offset of the first bad sequence (valid-prefix length). */
        std::size_t valid_up_to = 0;

        /**
         * \brief Maximal-subpart length of the rejected sequence (Unicode §3.9).
         * \note `0` means truncated (more bytes might still help); non-zero means
         *       definitive failure — resync at `valid_up_to + error_len`.
         */
        std::size_t error_len = 0;

        /** \brief What exactly was wrong. */
        encoding_error kind = encoding_error::truncated;

        /** \brief A human-readable description of #kind. */
        [[nodiscard]] constexpr std::string_view message() const noexcept { return describe(kind); }

        /** \brief Member-wise equality: same offset, same length, same kind. */
        [[nodiscard]] constexpr bool operator==(const invalid_encoding&) const noexcept = default;
    };

    namespace detail {

        /** \brief Which of the two encodings a validator is enforcing. */
        enum class encoding_form : unsigned char {
            /** Strict UTF-8 (reject surrogates). */
            utf8,
            /** WTF-8 (accept surrogates; reject unmerged high+low pairs). */
            wtf8,
        };

        /**
         * \brief Read byte \p i of \p text as an unsigned value.
         *
         * \warning Never inspect `text[i]` directly. Plain `char` is signed on every
         *          platform clapp targets, so `text[i] >= 0x80` is false for *every*
         *          non-ASCII byte and the validator would silently accept garbage.
         */
        [[nodiscard]] constexpr std::uint8_t byte_at(std::string_view text,
                                                     std::size_t i) noexcept {
            return static_cast<std::uint8_t>(text[i]);
        }

        /** \brief Result of decoding one sequence. */
        struct scan_result {
            /** Decoded code point when #ok. */
            std::uint32_t code_point = 0;
            /** Bytes consumed; on failure, resync skip (≥ 1). */
            std::size_t length = 1;
            /** Whether decoding succeeded. */
            bool ok = true;
            /** Failure kind when !#ok. */
            encoding_error failure = encoding_error::invalid_lead;
        };

        /**
         * \brief Inclusive legal range for the first continuation after \p lead.
         * \param lead Lead byte in `0xC2`..`0xF4`.
         * \return `{lowest, highest}`.
         * \note Tightens second-byte bounds for overlong/out-of-range; leaves `ED`
         *       open for surrogates (WTF-8). Strict surrogate rejection is in
         *       validate_encoded().
         */
        [[nodiscard]] constexpr std::pair<std::uint8_t, std::uint8_t>
        continuation_range(std::uint8_t lead) noexcept {
            if (lead == 0xE0u) return {0xA0u, 0xBFu};
            if (lead == 0xF0u) return {0x90u, 0xBFu};
            if (lead == 0xF4u) return {0x80u, 0x8Fu};
            return {0x80u, 0xBFu};
        }

        /**
         * \brief Decode one generalized-UTF-8 sequence at \p i (surrogates OK).
         * \param text Bytes to decode.
         * \param i Lead-byte offset.
         * \pre `i < text.size()`.
         * \return Code point and length, or failure plus resync distance.
         * \note On failure `length` is the maximal subpart (Unicode §3.9), not the
         *       lead's nominal width — matches Rust `Utf8Error` / `from_utf8_lossy`.
         */
        [[nodiscard]] constexpr scan_result scan_one(std::string_view text,
                                                     std::size_t i) noexcept {
            const std::size_t size  = text.size();
            const std::uint8_t lead = byte_at(text, i);

            if (lead < 0x80u) return {.code_point = lead, .length = 1, .ok = true};
            if (lead < 0xC0u)
                return {.length  = 1,
                        .ok      = false,
                        .failure = encoding_error::unexpected_continuation};
            // 0xC0 / 0xC1 could only ever encode a value below U+0080.
            if (lead < 0xC2u)
                return {.length = 1, .ok = false, .failure = encoding_error::overlong};

            std::size_t length       = 0;
            std::uint32_t code_point = 0;
            if (lead < 0xE0u) {
                length     = 2;
                code_point = lead & 0x1Fu;
            } else if (lead < 0xF0u) {
                length     = 3;
                code_point = lead & 0x0Fu;
            } else if (lead < 0xF5u) {
                length     = 4;
                code_point = lead & 0x07u;
            } else if (lead < 0xF8u) {
                // 0xF5..0xF7 would start a code point at U+140000 or beyond.
                return {.length = 1, .ok = false, .failure = encoding_error::out_of_range};
            } else {
                return {.length = 1, .ok = false, .failure = encoding_error::invalid_lead};
            }

            const auto [lowest, highest] = continuation_range(lead);

            // Raw loop rather than a ranges pipeline: the accumulator carries state
            // across iterations and three distinct failure modes exit early, which a
            // fold would only obscure.
            for (std::size_t k = 1; k < length; ++k) {
                if (i + k >= size)
                    return {.length = size - i, .ok = false, .failure = encoding_error::truncated};
                const std::uint8_t continuation = byte_at(text, i + k);
                if ((continuation & 0xC0u) != 0x80u)
                    return {.length  = k,
                            .ok      = false,
                            .failure = encoding_error::missing_continuation};
                // The first continuation byte carries the high bits, so it is where
                // an overlong or out-of-range sequence becomes undeniable. Deciding
                // here rather than after the loop is what keeps `length` a maximal
                // subpart, and it is also what keeps `truncated` honest: without this
                // test, `E0 80` at end of input would be reported as "might still
                // become valid" when no completion of it ever can.
                if (k == 1 && (continuation < lowest || continuation > highest))
                    return {.length  = 1,
                            .ok      = false,
                            .failure = lead == 0xF4u ? encoding_error::out_of_range
                                                     : encoding_error::overlong};
                code_point = (code_point << 6U) | (continuation & 0x3Fu);
            }

            // No post-loop overlong / out-of-range test: continuation_range() already
            // makes both unrepresentable. `C2`..`DF` bottoms out at U+0080, `E0 A0 80`
            // at U+0800, `F0 90 80 80` at U+10000, and `F4 8F BF BF` tops out at
            // U+10FFFF exactly.
            return {.code_point = code_point, .length = length, .ok = true};
        }

        /** \brief Whether \p code_point is a high (leading) surrogate. */
        [[nodiscard]] constexpr bool is_high_surrogate(std::uint32_t code_point) noexcept {
            return code_point >= 0xD800u && code_point <= 0xDBFFu;
        }

        /** \brief Whether \p code_point is a low (trailing) surrogate. */
        [[nodiscard]] constexpr bool is_low_surrogate(std::uint32_t code_point) noexcept {
            return code_point >= 0xDC00u && code_point <= 0xDFFFu;
        }

        /**
         * \brief Validate \p text as strict UTF-8 or WTF-8.
         * \param text Bytes to check.
         * \param form Encoding form.
         * \return Success, or first failure (offset, length, kind).
         */
        [[nodiscard]] constexpr std::expected<void, invalid_encoding>
        validate_encoded(std::string_view text, encoding_form form) noexcept {
            bool previous_was_high         = false;
            std::size_t previous_seq_start = 0;

            // Raw loop rather than a ranges pipeline: the stride is data-dependent
            // (scan_one decides how far to step), and the surrogate-pair rule needs the
            // *previous* sequence's offset in order to report the pair as one error —
            // neither of which an element of a pipeline can see.
            for (std::size_t i = 0; i < text.size();) {
                const scan_result scan = scan_one(text, i);
                if (!scan.ok) {
                    // `ED A0`..`ED BF` at end of input is completable in WTF-8 and never
                    // in strict UTF-8, so reporting it as `truncated`/`error_len == 0`
                    // ("more bytes could still help") would be a lie in the strict form.
                    // scan_one cannot make this call: it does not know which form is in
                    // force, and the surrogate rule lives here for exactly that reason.
                    if (form == encoding_form::utf8 && scan.failure == encoding_error::truncated &&
                        byte_at(text, i) == 0xEDu && i + 1 < text.size() &&
                        byte_at(text, i + 1) >= 0xA0u)
                        return std::unexpected(invalid_encoding{.valid_up_to = i,
                                                                .error_len   = text.size() - i,
                                                                .kind = encoding_error::surrogate});
                    return std::unexpected(invalid_encoding{
                            .valid_up_to = i,
                            .error_len =
                                    scan.failure == encoding_error::truncated ? 0 : scan.length,
                            .kind = scan.failure});
                }

                const bool high = is_high_surrogate(scan.code_point);
                const bool low  = is_low_surrogate(scan.code_point);

                if (form == encoding_form::utf8 && (high || low))
                    return std::unexpected(invalid_encoding{.valid_up_to = i,
                                                            .error_len   = scan.length,
                                                            .kind = encoding_error::surrogate});

                if (form == encoding_form::wtf8 && low && previous_was_high)
                    return std::unexpected(
                            invalid_encoding{.valid_up_to = previous_seq_start,
                                             .error_len   = 6,
                                             .kind        = encoding_error::surrogate_pair});

                previous_was_high  = high;
                previous_seq_start = i;
                i += scan.length;
            }
            return {};
        }

        /**
         * \brief Append \p code_point to \p out as (W)TF-8.
         * \pre `code_point <= 0x10FFFF`; surrogates emit `ED xx xx` (WTF-8).
         */
        constexpr void append_encoded(std::string& out, std::uint32_t code_point) {
            if (code_point < 0x80u) {
                out.push_back(static_cast<char>(code_point));
            } else if (code_point < 0x800u) {
                out.push_back(static_cast<char>(0xC0u | (code_point >> 6U)));
                out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
            } else if (code_point < 0x10000u) {
                out.push_back(static_cast<char>(0xE0u | (code_point >> 12U)));
                out.push_back(static_cast<char>(0x80u | ((code_point >> 6U) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
            } else {
                out.push_back(static_cast<char>(0xF0u | (code_point >> 18U)));
                out.push_back(static_cast<char>(0x80u | ((code_point >> 12U) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | ((code_point >> 6U) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
            }
        }

        /**
         * \brief Convert UTF-16 code units to WTF-8 (lossless).
         * \tparam Unit 16-bit unit type (`char16_t` / Windows `wchar_t`).
         * \param units Code units.
         * \return WTF-8; pairs merge, unpaired surrogates stand alone.
         */
        template<class Unit>
            requires(sizeof(Unit) == 2)
        [[nodiscard]] constexpr std::string utf16_to_wtf8(std::basic_string_view<Unit> units) {
            std::string out;
            out.reserve(units.size());

            // Raw loop: a surrogate pair consumes two input units to produce one
            // output code point, so the step is data-dependent. `views::chunk_by`
            // would express the grouping but not more clearly.
            for (std::size_t i = 0; i < units.size(); ++i) {
                std::uint32_t code_point =
                        static_cast<std::uint32_t>(static_cast<std::uint16_t>(units[i]));
                if (is_high_surrogate(code_point) && i + 1 < units.size()) {
                    const auto trail =
                            static_cast<std::uint32_t>(static_cast<std::uint16_t>(units[i + 1]));
                    if (is_low_surrogate(trail)) {
                        code_point = 0x10000u + ((code_point - 0xD800u) << 10U) +
                                     (trail - 0xDC00u);
                        ++i;
                    }
                }
                append_encoded(out, code_point);
            }
            return out;
        }

        /**
         * \brief Convert WTF-8 to UTF-16 (lossless when well-formed).
         * \tparam Unit 16-bit unit type.
         * \param text Bytes (need not be well-formed; bad bytes become U+FFFD).
         * \return UTF-16 code units.
         */
        template<class Unit>
            requires(sizeof(Unit) == 2)
        [[nodiscard]] constexpr std::basic_string<Unit> wtf8_to_utf16(std::string_view text) {
            std::basic_string<Unit> out;
            out.reserve(text.size());

            // Raw loop: the stride is whatever scan_one consumed (1..4 bytes), and one
            // input sequence emits either one code unit or a surrogate pair. A pipeline
            // would need both a data-dependent step and a 1-to-2 fan-out.
            for (std::size_t i = 0; i < text.size();) {
                const scan_result scan   = scan_one(text, i);
                std::uint32_t code_point = scan.ok ? scan.code_point : 0xFFFDu;
                if (code_point < 0x10000u) {
                    out.push_back(static_cast<Unit>(code_point));
                } else {
                    code_point -= 0x10000u;
                    out.push_back(static_cast<Unit>(0xD800u + (code_point >> 10U)));
                    out.push_back(static_cast<Unit>(0xDC00u + (code_point & 0x3FFu)));
                }
                i += scan.length;
            }
            return out;
        }

        /**
         * \brief Copy \p text as strict UTF-8, replacing bad sequences with U+FFFD.
         * \note One U+FFFD per maximal subpart (`E0 80 80` → three); WTF-8 surrogates
         *       collapse to one — see os_str::to_string_lossy().
         */
        [[nodiscard]] constexpr std::string lossy_utf8(std::string_view text) {
            std::string out;
            out.reserve(text.size());

            // Raw loop: same data-dependent stride as wtf8_to_utf16, and the emitted
            // text is either a copied slice or a fixed replacement, chosen per step.
            for (std::size_t i = 0; i < text.size();) {
                const scan_result scan = scan_one(text, i);
                const bool usable      = scan.ok && !is_high_surrogate(scan.code_point) &&
                                         !is_low_surrogate(scan.code_point);
                if (usable)
                    out.append(text.substr(i, scan.length));
                else
                    out.append("\xEF\xBF\xBD");  // U+FFFD
                i += scan.length;
            }
            return out;
        }

    }  // namespace detail

    /**
     * \brief Check that \p text is strict UTF-8 (surrogates rejected).
     * \param text Bytes to check.
     * \return Success, or first failure (offset/length/kind).
     */
    [[nodiscard]] constexpr std::expected<void, invalid_encoding>
    validate_utf8(std::string_view text) noexcept {
        return detail::validate_encoded(text, detail::encoding_form::utf8);
    }

    /**
     * \brief Check that \p text is well-formed WTF-8.
     * \param text Bytes to check.
     * \return Success, or first failure.
     * \note Accepts surrogates; rejects unmerged high+low pairs (keeps WTF-8↔UTF-16
     *       bijective so Windows argv equality matches).
     */
    [[nodiscard]] constexpr std::expected<void, invalid_encoding>
    validate_wtf8(std::string_view text) noexcept {
        return detail::validate_encoded(text, detail::encoding_form::wtf8);
    }

    class os_str_split_view;

    /**
     * \brief Non-owning view of one command-line argument as WTF-8 bytes.
     * \note Byte-wise and locale-independent (Rust `&OsStr`). Not implicitly
     *       convertible to `std::string_view` — use to_string_view() or chars().
     * \warning Does not own bytes; valid only while the buffer (argv / os_string)
     *          lives. Building from a temporary `std::string` dangles silently.
     */
    class os_str {
    public:
        /** \brief Stored byte type. */
        using value_type = char;
        /** \brief Unsigned size and index type. */
        using size_type = std::size_t;
        /** \brief Read-only byte iterator. */
        using const_iterator = const char*;
        /** \brief Alias for the read-only iterator; the view is immutable. */
        using iterator = const_iterator;

        /**
         * \brief "To the end" sentinel for substr().
         * \note Searches use `std::optional`, not #npos, for misses.
         */
        static constexpr size_type npos = static_cast<size_type>(-1);

        /** \brief An empty argument. */
        constexpr os_str() noexcept = default;

        /**
         * \brief View the bytes of \p text (implicit for literal comparisons).
         * \note Embedded NULs are kept; use an explicit-length view when needed.
         */
        constexpr os_str(std::string_view text) noexcept : text_(text) {}

        /**
         * \brief View the NUL-terminated bytes at \p text.
         * \param text May be null, which yields an empty os_str.
         * \note Stops at the first NUL. Use the `std::string_view` overload for
         *       payloads that contain NUL bytes.
         */
        constexpr os_str(const char* text) noexcept
            : text_(text == nullptr ? std::string_view{} : std::string_view{text}) {}

        /**
         * \brief View the bytes of \p text.
         * \warning Does not extend \p text 's lifetime. Binding to a temporary
         *          `std::string` leaves a dangling view.
         */
        constexpr os_str(const std::string& text) noexcept : text_(text) {}

        /**
         * \brief View \p raw as command-line bytes.
         *
         * \note Not `constexpr`: the `std::byte` <-> `char` conversion needs
         *       `reinterpret_cast`, which constant evaluation forbids. Use the
         *       `std::string_view` constructor in `constexpr` code.
         */
        [[nodiscard]] static os_str from_bytes(std::span<const std::byte> raw) noexcept {
            return os_str(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
        }

        /**
         * \name Raw access
         * \{
         */

        /** \brief Stored bytes with no validity claim (lexer's workhorse). */
        [[nodiscard]] constexpr std::string_view chars() const noexcept { return text_; }

        /**
         * \brief Stored bytes as `std::byte`.
         * \note Not `constexpr`; prefer chars() in constant evaluation.
         */
        [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
            return std::as_bytes(std::span<const char>(text_.data(), text_.size()));
        }

        /** \brief Pointer to the first byte; possibly null when empty(). */
        [[nodiscard]] constexpr const char* data() const noexcept { return text_.data(); }

        /** \brief Length in **bytes**, not in code points. */
        [[nodiscard]] constexpr size_type size() const noexcept { return text_.size(); }

        /** \brief Whether the argument is zero bytes long, i.e. the `""` argument. */
        [[nodiscard]] constexpr bool empty() const noexcept { return text_.empty(); }

        /** \brief First byte. Iteration is byte-wise; decoding is to_string_view()'s job. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept { return text_.data(); }

        /** \brief One past the last byte. */
        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return text_.data() + text_.size();
        }

        /**
         * \brief Byte \p index, as written.
         * \pre `index < size()`.
         */
        [[nodiscard]] constexpr char operator[](size_type index) const noexcept {
            return text_[index];
        }

        /**
         * \brief Byte \p index as an unsigned value.
         * \pre `index < size()`.
         * \warning Prefer this over `operator[]` for anything that classifies bytes.
         *          `char` is signed here, so `s[0] >= 0x80` is false for every
         *          non-ASCII byte and such a test silently never fires.
         */
        [[nodiscard]] constexpr std::uint8_t byte_at(size_type index) const noexcept {
            return detail::byte_at(text_, index);
        }

        /** \} */

        /**
         * \name Decoding
         * \{
         */

        /**
         * \brief Borrow content as strict UTF-8 text (zero-copy on success).
         * \return Same bytes as `std::string_view`, or first failure.
         * \note Surrogates from Windows WTF-8 are errors, not leaked text.
         */
        [[nodiscard]] constexpr std::expected<std::string_view, invalid_encoding>
        to_string_view() const noexcept {
            return validate_utf8(text_).transform([this] { return text_; });
        }

        /** \brief Whether the content is valid strict UTF-8. */
        [[nodiscard]] constexpr bool is_utf8() const noexcept {
            return validate_utf8(text_).has_value();
        }

        /**
         * \brief Whether the content is well-formed WTF-8.
         * \note True for Windows from_native() (UTF-16→WTF-8); not guaranteed on POSIX.
         * \warning On POSIX from_native() copies argv verbatim — arbitrary bytes may
         *          fail is_wtf8(). Skipping validation silently: wtf8_to_utf16() is
         *          total and substitutes U+FFFD with no report.
         */
        [[nodiscard]] constexpr bool is_wtf8() const noexcept {
            return validate_wtf8(text_).has_value();
        }

        /**
         * \brief Diagnostic UTF-8 with undecodable sequences as U+FFFD (no round-trip).
         * \return Always valid strict UTF-8.
         * \note One U+FFFD per maximal subpart; WTF-8 surrogates collapse to one
         *       (platform-uniform WTF-8, ADR-0003) — diverges from POSIX Rust lossy.
         */
        [[nodiscard]] constexpr std::string to_string_lossy() const {
            return detail::lossy_utf8(text_);
        }

        /**
         * \brief Convert to the host OS encoding (POSIX: copy; Windows: WTF-8→UTF-16).
         */
        [[nodiscard]] constexpr native_string to_native() const {
#ifdef _WIN32
            return detail::wtf8_to_utf16<native_char>(text_);
#else
            return native_string(text_);
#endif
        }

        /** \} */

        /**
         * \name Byte-wise string operations
         * Safe on non-UTF-8 content. Non-ASCII needles may match mid-sequence.
         * \{
         */

        /**
         * \brief Whether content begins with \p prefix, byte-wise.
         * \param prefix Prefix bytes.
         */
        [[nodiscard]] constexpr bool starts_with(os_str prefix) const noexcept {
            return text_.starts_with(prefix.text_);
        }

        /**
         * \brief Whether the first byte is \p prefix.
         * \return `false` when empty().
         */
        [[nodiscard]] constexpr bool starts_with(char prefix) const noexcept {
            return text_.starts_with(prefix);
        }

        /** \brief Whether content ends with \p suffix, byte-wise. */
        [[nodiscard]] constexpr bool ends_with(os_str suffix) const noexcept {
            return text_.ends_with(suffix.text_);
        }

        /**
         * \brief Whether the last byte is \p suffix.
         * \return `false` when empty().
         */
        [[nodiscard]] constexpr bool ends_with(char suffix) const noexcept {
            return text_.ends_with(suffix);
        }

        /**
         * \brief First offset of \p needle at or after \p pos, or `std::nullopt`.
         * \note Empty needle matches at \p pos. Non-ASCII may hit mid-sequence.
         */
        [[nodiscard]] constexpr std::optional<size_type> find(os_str needle,
                                                              size_type pos = 0) const noexcept {
            const size_type at = text_.find(needle.text_, pos);
            if (at == std::string_view::npos) return std::nullopt;
            return at;
        }

        /**
         * \brief First offset of byte \p needle at or after \p pos, or `std::nullopt`.
         */
        [[nodiscard]] constexpr std::optional<size_type> find(char needle,
                                                              size_type pos = 0) const noexcept {
            const size_type at = text_.find(needle, pos);
            if (at == std::string_view::npos) return std::nullopt;
            return at;
        }

        /** \brief Whether \p needle occurs anywhere, byte-wise. */
        [[nodiscard]] constexpr bool contains(os_str needle) const noexcept {
            return find(needle).has_value();
        }

        /** \brief Whether byte \p needle occurs anywhere. */
        [[nodiscard]] constexpr bool contains(char needle) const noexcept {
            return find(needle).has_value();
        }

        /**
         * \brief Remainder after \p prefix, or `std::nullopt` if not a prefix.
         */
        [[nodiscard]] constexpr std::optional<os_str> strip_prefix(os_str prefix) const noexcept {
            if (!starts_with(prefix)) return std::nullopt;
            return os_str(text_.substr(prefix.size()));
        }

        /**
         * \brief Remainder before \p suffix, or `std::nullopt` if not a suffix.
         */
        [[nodiscard]] constexpr std::optional<os_str> strip_suffix(os_str suffix) const noexcept {
            if (!ends_with(suffix)) return std::nullopt;
            return os_str(text_.substr(0, size() - suffix.size()));
        }

        /**
         * \brief Split once on \p separator (e.g. `--key=value`); drop separator.
         * \return `{before, after}`, or `std::nullopt` if absent.
         */
        [[nodiscard]] constexpr std::optional<std::pair<os_str, os_str>>
        split_once(os_str separator) const noexcept {
            const std::optional<size_type> at = find(separator);
            if (!at.has_value()) return std::nullopt;
            return std::pair{os_str(text_.substr(0, *at)),
                             os_str(text_.substr(*at + separator.size()))};
        }

        /**
         * \brief Split once on byte \p separator; drop separator.
         * \return `{before, after}`, or `std::nullopt` if absent.
         */
        [[nodiscard]] constexpr std::optional<std::pair<os_str, os_str>>
        split_once(char separator) const noexcept {
            const std::optional<size_type> at = find(separator);
            if (!at.has_value()) return std::nullopt;
            return std::pair{os_str(text_.substr(0, *at)), os_str(text_.substr(*at + 1))};
        }

        /**
         * \brief Split on every \p separator (value_delimiter); empties kept.
         * \param separator Bytes to split on (not coalesced).
         * \pre Prefer non-empty; empty separator yields one element (no infinite loop).
         * \return Lazy non-empty forward range of os_str.
         * \warning Borrows this view's bytes; dangles under the same conditions.
         */
        [[nodiscard]] constexpr os_str_split_view split(os_str separator) const noexcept;

        /**
         * \brief Cut at \p index (clamped): `[0, index)` and `[index, size())`.
         * \note Byte cut; mid-sequence halves are well-defined but may not be UTF-8.
         */
        [[nodiscard]] constexpr std::pair<os_str, os_str> split_at(size_type index) const noexcept {
            const size_type cut = index < size() ? index : size();
            return {os_str(text_.substr(0, cut)), os_str(text_.substr(cut))};
        }

        /**
         * \brief Subrange from \p pos, at most \p count bytes (#npos = to end).
         * \note \p pos is clamped instead of reporting an out-of-range access like string_view.
         */
        [[nodiscard]] constexpr os_str substr(size_type pos,
                                              size_type count = npos) const noexcept {
            const size_type start = pos < size() ? pos : size();
            return os_str(text_.substr(start, count));
        }

        /** \} */

    private:
        std::string_view text_{};
    };

    /**
     * \brief Byte-wise equality (namespace scope so os_string converts in).
     */
    [[nodiscard]] constexpr bool operator==(os_str lhs, os_str rhs) noexcept {
        return lhs.chars() == rhs.chars();
    }

    /**
     * \brief Byte-wise ordering (`memcmp` / unsigned, locale-independent).
     */
    [[nodiscard]] constexpr std::strong_ordering operator<=>(os_str lhs, os_str rhs) noexcept {
        return lhs.chars() <=> rhs.chars();
    }

    /**
     * \brief Pieces from os_str::split(); empty subject yields one empty piece.
     * \note Hand-written so `""` splits to `{""}` (not `std::views::split`'s empty
     *       range) — required for `--paths=` with a delimiter. Forward view, not sized.
     * \warning Borrowed; elements dangle with the source os_str.
     */
    class os_str_split_view : public std::ranges::view_interface<os_str_split_view> {
    public:
        /** \brief Forward cursor over pieces (value; copy is independent). */
        class iterator {
        public:
            using value_type = os_str;
            using difference_type = std::ptrdiff_t;
            using iterator_concept = std::forward_iterator_tag;

            /** \brief Exhausted cursor (end). */
            constexpr iterator() noexcept = default;

            /**
             * \brief Cursor over \p subject split on \p separator.
             * \param subject Bytes to split.
             * \param separator Empty means never matches.
             */
            constexpr iterator(os_str subject, os_str separator) noexcept
                : rest_(subject), separator_(separator), live_(true) {
                step();
            }

            /** \brief Current piece. \pre Not exhausted. */
            [[nodiscard]] constexpr os_str operator*() const noexcept { return current_; }

            /** \brief Advance to next piece. */
            constexpr iterator& operator++() noexcept {
                if (final_)
                    *this = iterator{};
                else
                    step();
                return *this;
            }

            /** \brief Post-increment. */
            constexpr iterator operator++(int) noexcept {
                const iterator previous = *this;
                ++*this;
                return previous;
            }

            /** \brief Same-position equality. */
            [[nodiscard]] constexpr bool operator==(const iterator&) const noexcept = default;

            /** \brief Whether exhausted. */
            [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const noexcept {
                return !live_;
            }

        private:
            /** Take the next piece out of #rest_, marking the last one. */
            constexpr void step() noexcept {
                const std::optional<std::pair<os_str, os_str>> split =
                        separator_.empty() ? std::nullopt : rest_.split_once(separator_);
                if (split.has_value()) {
                    current_ = split->first;
                    rest_    = split->second;
                    final_   = false;
                } else {
                    current_ = rest_;
                    rest_    = os_str{};
                    final_   = true;
                }
            }

            os_str current_{};
            os_str rest_{};
            os_str separator_{};
            bool final_ = false;
            bool live_  = false;
        };

        /** \brief Empty view. */
        constexpr os_str_split_view() noexcept = default;

        /**
         * \brief Split \p subject on every \p separator.
         * \param subject Bytes to split.
         * \param separator Empty yields one piece.
         */
        constexpr os_str_split_view(os_str subject, os_str separator) noexcept
            : subject_(subject), separator_(separator) {}

        /** \brief First piece (always dereferenceable). */
        [[nodiscard]] constexpr iterator begin() const noexcept {
            return iterator{subject_, separator_};
        }

        /** \brief End sentinel. */
        [[nodiscard]] static constexpr std::default_sentinel_t end() noexcept {
            return std::default_sentinel;
        }

        /** \brief Bytes being split. */
        [[nodiscard]] constexpr os_str subject() const noexcept { return subject_; }

        /** \brief Separator bytes. */
        [[nodiscard]] constexpr os_str separator() const noexcept { return separator_; }

    private:
        os_str subject_{};
        os_str separator_{};
    };

    constexpr os_str_split_view os_str::split(os_str separator) const noexcept {
        return os_str_split_view{*this, separator};
    }

    /**
     * \brief Owning counterpart of os_str (WTF-8 in `std::string`; NULs allowed).
     */
    class os_string {
    public:
        using size_type = std::size_t;

        constexpr os_string() = default;

        /** \brief Copy \p text (embedded NULs kept). */
        constexpr os_string(std::string_view text) : bytes_(text) {}

        /** \brief Copy NUL-terminated \p text; null yields empty. */
        constexpr os_string(const char* text)
            : bytes_(text == nullptr ? std::string{} : std::string{text}) {}

        /** \brief Adopt \p text without conversion. */
        constexpr os_string(std::string text) noexcept : bytes_(std::move(text)) {}

        /**
         * \brief Copy bytes from \p text.
         * \note Explicit: allocates; avoids comparison ambiguity with os_str ops.
         */
        constexpr explicit os_string(os_str text) : bytes_(text.chars()) {}

        /**
         * \brief From OS-native encoding (POSIX: copy; Windows: UTF-16→WTF-8 here only).
         * \param native OS-spelled string.
         * \warning Well-formed WTF-8 only on Windows; POSIX is raw bytes. See is_wtf8().
         */
        [[nodiscard]] static constexpr os_string from_native(native_string_view native) {
#ifdef _WIN32
            return os_string(detail::utf16_to_wtf8<native_char>(native));
#else
            return os_string(std::string(native));
#endif
        }

        /**
         * \brief Copy \p raw as command-line bytes.
         * \note Not `constexpr`.
         */
        [[nodiscard]] static os_string from_bytes(std::span<const std::byte> raw) {
            return os_string(std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
        }

        /**
         * \brief Borrow the content.
         * \warning Invalidated by mutation or destruction of this os_string.
         */
        [[nodiscard]] constexpr os_str view() const noexcept {
            return os_str(std::string_view(bytes_));
        }

        /**
         * \brief Implicit conversion to os_str (for shared queries/comparisons).
         * \warning Does not extend lifetime — fires without spelling view().
         *          `parsed_arg{os_string{"--f"}}` and `os_str s = make_os_string()`
         *          dangle; nothing diagnoses them. Bind the os_string first.
         */
        constexpr operator os_str() const noexcept {  // NOLINT(google-explicit-constructor)
            return view();
        }

        /** \brief Owned bytes, no validity claim. */
        [[nodiscard]] constexpr const std::string& chars() const noexcept { return bytes_; }

        /** \brief Release ownership of the bytes. */
        [[nodiscard]] constexpr std::string release() && noexcept { return std::move(bytes_); }

        /** \brief Owned bytes as `std::byte`. \note Not `constexpr`. */
        [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return view().bytes(); }

        /** \brief Pointer to first byte. */
        [[nodiscard]] constexpr const char* data() const noexcept { return bytes_.data(); }

        /** \brief Length in bytes. */
        [[nodiscard]] constexpr size_type size() const noexcept { return bytes_.size(); }

        /** \brief Whether empty. */
        [[nodiscard]] constexpr bool empty() const noexcept { return bytes_.empty(); }

        /** \brief First byte. */
        [[nodiscard]] constexpr const char* begin() const noexcept { return bytes_.data(); }

        /** \brief One past last byte. */
        [[nodiscard]] constexpr const char* end() const noexcept {
            return bytes_.data() + bytes_.size();
        }

        /** \brief See os_str::to_string_view(). */
        [[nodiscard]] constexpr std::expected<std::string_view, invalid_encoding>
        to_string_view() const noexcept {
            return view().to_string_view();
        }

        /** \brief See os_str::to_string_lossy(). */
        [[nodiscard]] constexpr std::string to_string_lossy() const {
            return view().to_string_lossy();
        }

        /** \brief See os_str::to_native(). */
        [[nodiscard]] constexpr native_string to_native() const { return view().to_native(); }

        /** \brief See os_str::is_utf8(). */
        [[nodiscard]] constexpr bool is_utf8() const noexcept { return view().is_utf8(); }

        /** \brief See os_str::is_wtf8(). */
        [[nodiscard]] constexpr bool is_wtf8() const noexcept { return view().is_wtf8(); }

    private:
        std::string bytes_;
    };

}  // namespace clapp

namespace std {

    /** \brief Byte-wise hash, consistent with os_str equality. */
    template<>
    struct hash<clapp::os_str> {
        /** \brief Hash viewed bytes. */
        [[nodiscard]] std::size_t operator()(clapp::os_str value) const noexcept {
            return std::hash<std::string_view>{}(value.chars());
        }
    };

    /** \brief Byte-wise hash, equal to hashing `value.view()`. */
    template<>
    struct hash<clapp::os_string> {
        /** \brief Hash owned bytes. */
        [[nodiscard]] std::size_t operator()(const clapp::os_string& value) const noexcept {
            return std::hash<std::string_view>{}(std::string_view(value.chars()));
        }
    };

}  // namespace std
