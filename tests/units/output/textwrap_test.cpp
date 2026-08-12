#include <clapp/output/textwrap.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::display_width;
    using clapp::indent;
    using clapp::line_wrapper;
    using clapp::style;
    using clapp::style_class;
    using clapp::styled_str;
    using clapp::styles;
    using clapp::wrap;
    using namespace std::string_view_literals;

    /// Every complete ANSI escape sequence in \p text, in order — an oracle written from
    /// ECMA-48 rather than from clapp::detail::escape_sequence_length(), so that the two
    /// cannot be wrong together. A sequence that does not terminate before the end of
    /// \p text is reported as the empty string, which is what makes a split visible: the
    /// half before the break has no final byte and the half after starts with no `ESC`.
    [[nodiscard]] std::vector<std::string> sequences_of(std::string_view text) {
        std::vector<std::string> found;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\x1B') continue;
            const std::size_t start = i;
            std::string one;
            if (i + 1 >= text.size()) {
                found.emplace_back();  // a bare trailing ESC
                break;
            }
            const unsigned char introducer = static_cast<unsigned char>(text[i + 1]);
            i += 2;
            if (introducer == '[') {
                while (i < text.size() && !(static_cast<unsigned char>(text[i]) >= 0x40 &&
                                            static_cast<unsigned char>(text[i]) <= 0x7E))
                    ++i;
                if (i >= text.size()) {
                    found.emplace_back();
                    break;
                }
            } else if (introducer == ']' || introducer == 'P' || introducer == 'X' ||
                       introducer == '^' || introducer == '_') {
                while (i < text.size() && text[i] != '\x07' &&
                       !(text[i] == '\x1B' && i + 1 < text.size() && text[i + 1] == '\\'))
                    ++i;
                if (i >= text.size()) {
                    found.emplace_back();
                    break;
                }
                if (text[i] == '\x1B') ++i;
            } else if (introducer >= 0x20 && introducer <= 0x2F) {
                while (i < text.size() && static_cast<unsigned char>(text[i]) >= 0x20 &&
                       static_cast<unsigned char>(text[i]) <= 0x2F)
                    ++i;
                if (i >= text.size()) {
                    found.emplace_back();
                    break;
                }
            } else {
                --i;  // a two-byte escape: the introducer was the whole of it
            }
            one.assign(text.substr(start, i - start + 1));
            found.push_back(std::move(one));
        }
        return found;
    }

    // ---------------------------------------------------------------------------
    // Display width — clap's textwrap/core.rs
    // ---------------------------------------------------------------------------

    consteval bool clap_display_width_cases() {
        // clap: `display_width_works` — "é" is two bytes, one cell.
        return display_width("Café Plain") == 10 &&
               "Café Plain"sv.size() == 11
               // clap: `display_width_narrow_emojis`
               && display_width("⁉") == 1
               // clap: `display_width_narrow_emojis_variant_selector`
               && display_width("⁉️") == 1
               // clap: `display_width_emojis`
               && display_width("😂😭🥺🤣✨😍🙏🥰😊🔥") == 20;
    }

    static_assert(clap_display_width_cases());

    consteval bool ascii_and_latin1_emoji_are_one_cell() {
        // clap: `emojis_have_correct_width`, first half — everything in the Basic Latin
        // and Latin-1 Supplement blocks is one column, including the characters Unicode
        // classifies as emoji.
        return display_width("#") == 1 && display_width("©") == 1 && display_width("®") == 1 &&
               display_width("™") == 1;
    }

    static_assert(ascii_and_latin1_emoji_are_one_cell());

    consteval bool east_asian_text_is_two_cells_per_character() {
        return display_width("中文") == 4         // CJK Unified Ideographs
               && display_width("ｆｕｌｌ") == 8  // Halfwidth and Fullwidth Forms
               && display_width("あ") == 2        // Hiragana
               && display_width("한") == 2        // Hangul Syllables
               && display_width("😂") == 2        // emoji, SMP
               && display_width("中a文") == 5;    // mixed
    }

    static_assert(east_asian_text_is_two_cells_per_character());

    consteval bool combining_marks_and_format_characters_are_free() {
        return display_width("é") == 1                // e + COMBINING ACUTE ACCENT
               && display_width("​") == 0           // ZERO WIDTH SPACE
               && display_width("‍") == 0           // ZERO WIDTH JOINER
               && display_width("­") == 1             // SOFT HYPHEN — terminals draw it
               && display_width("👨‍🦰") == 4;  // ZWJ sequence: 2 + 0 + 2
    }

    static_assert(combining_marks_and_format_characters_are_free());

    consteval bool control_bytes_occupy_no_cell() {
        // clap charges these one cell each and then swallows every character up to the
        // next 'm'; see difference 1 in the header's file comment.
        return display_width("\n") == 0 && display_width("\t") == 0 && display_width("\x7F") == 0 &&
               display_width("a\tb") == 2 && display_width("a\nb") == 2;
    }

    static_assert(control_bytes_occupy_no_cell());

    // The tables are generated, and a regeneration that shifted every range by one
    // would still be sorted and disjoint. Pin the first and last entry of each, plus
    // one code point on each side of a boundary.
    consteval bool table_boundaries_are_where_they_should_be() {
        using clapp::char_display_width;
        return char_display_width(U'ჿ') == 1      // just below Hangul Jamo initial
               && char_display_width(U'ᄀ') == 2  // first wide range, first code point
               && char_display_width(U'ᅟ') == 2  // first wide range, last code point
               && char_display_width(U'ᅠ') == 0   // Jamo medial: composes, no cell
               && char_display_width(U'ᇿ') == 0   // Jamo final, last code point
               && char_display_width(U'ሀ') == 1   // Ethiopic, back to one cell
               && char_display_width(U'˿') == 1   // just below the first zero range
               && char_display_width(U'̀') == 0    // COMBINING GRAVE ACCENT
               && char_display_width(U'ͯ') == 0    // last of that block
               && char_display_width(U'Ͱ') == 1;
    }

    static_assert(table_boundaries_are_where_they_should_be());

    consteval bool ansi_escape_sequences_take_no_room() {
        return display_width("\x1B[1mbold\x1B[0m") == 4            // SGR, the common case
               && display_width("\x1B[38;5;214mtext\x1B[0m") == 4  // 256-colour SGR
               && display_width("\x1B]0;title\x07x") == 1          // OSC ended by BEL
               && display_width("\x1B]0;title\x1B\\x") == 1        // OSC ended by ST
               && display_width("\x1B(Bx") == 1                    // two-byte escape
               && display_width("\x1B") == 0                       // lone ESC at the end
               && display_width("\x1B[") == 0;                     // truncated CSI
    }

    static_assert(ansi_escape_sequences_take_no_room());

    consteval bool ill_formed_bytes_cost_one_cell_each() {
        // One cell per maximal ill-formed subpart, which is one U+FFFD per subpart —
        // the same accounting clapp::lossy_utf8 uses, so a measured width and a
        // displayed string agree. `"\xFF" "a"` rather than `"\xFFa"`: a hex escape is
        // greedy and would swallow the 'a'.
        return display_width("\xFF") == 1 &&
               display_width("a\xFF"
                             "b") == 3 &&
               display_width("\xE0\x80\x80") == 3      // three maximal subparts
               && display_width("\xED\xA0\x80") == 1;  // unpaired surrogate, WTF-8
    }

    static_assert(ill_formed_bytes_cost_one_cell_each());

    // ---------------------------------------------------------------------------
    // The ECMA-48 byte classes — every boundary, from both sides
    // ---------------------------------------------------------------------------
    //
    // clapp::detail::escape_sequence_length() is nine byte-range comparisons and a
    // terminator search, and every one of those ranges is a place an edit can be off by
    // one. M6's test-integrity review mutated each of those endpoints, and both control
    // ranges in char_display_width(), by one in each direction and reported **five
    // surviving mutants — no ctest target failed for any of them**. The cases above are
    // why: they only ever feed sequences that sit comfortably inside a range, and
    // `ESC [ 1 m` says nothing about whether the parameter class ends at 0x3F or at 0x3E.
    //
    // This section was written against a re-run of that sweep, widened to 52 mutants —
    // both directions on every endpoint, plus the five string introducers, the BEL and ST
    // terminators, and the CSI introducer itself. It kills 49 of them. The three it does
    // not kill are equivalent mutants and are listed after it.
    //
    // Each endpoint is pinned twice, with the byte on each side of it, and the two sides
    // are chosen so that the *reading* changes and not merely the length: a mutant that
    // keeps the length by accident is a mutant that survives. The comment on each line
    // names the mutant it kills.
    //
    // The introducer classes, for reference (ECMA-48 §5.3 and §5.4):
    //
    //   0x00-0x1F  C0 control    not an introducer — a stray ESC, length 1
    //   0x20-0x2F  nF            intermediates, then one final byte in 0x30-0x7E
    //   0x30-0x3F  Fp            private two-byte escapes: ESC 7, ESC =
    //   0x40-0x5F  Fe            two-byte, except CSI/DCS/SOS/PM/APC which run on
    //   0x60-0x7E  Fs            two-byte: ESC c
    //   0x7F-0xFF  DEL, 8-bit    not an introducer — a stray ESC, length 1

    /// clapp::detail::escape_sequence_length() applied to `ESC <introducer> <tail>`,
    /// assembled byte by byte because a string literal cannot spell an arbitrary
    /// introducer without a hex escape swallowing the byte that follows it.
    [[nodiscard]] consteval std::size_t esc_len(unsigned introducer, std::string_view tail) {
        std::string text;
        text.push_back('\x1B');
        text.push_back(static_cast<char>(introducer));
        for (const char byte : tail) text.push_back(byte);
        return clapp::detail::escape_sequence_length(std::string_view{text}, 0);
    }

    consteval bool escape_byte_classes_start_and_end_where_ecma48_says() {
        return
                // --- nF, lower edge: 0x20 is an intermediate, 0x1F is a C0 control ---
                esc_len(0x20, "B") == 3     // nF from 0x21 would give 1
                && esc_len(0x1F, "B") == 1  // nF from 0x1F would give 3
                // --- nF, upper edge: 0x2F is an intermediate, 0x30 is a final byte ---
                && esc_len(0x2F, "B") == 3  // nF to 0x2E would give 1
                && esc_len(0x30, "B") == 2  // nF to 0x30 would give 3
                // --- inside an nF sequence: more intermediates, then one final byte ---
                && esc_len(0x28, " B") == 4    // intermediate low 0x21 would give 2
                && esc_len(0x28, "/B") == 4    // intermediate high 0x2E would give 2
                && esc_len(0x28, "0") == 3     // final low 0x31 would give 2
                && esc_len(0x28, "~") == 3     // final high 0x7D would give 2
                && esc_len(0x28, "\x7F") == 2  // final high 0x7F would give 3
                // --- the two-byte class: 0x30 is the first final byte, 0x7E the last ---
                && esc_len(0x7E, "x") == 2  // upper 0x7D would give 1
                && esc_len(0x7F, "x") == 1  // upper 0x7F would give 2 (DEL is not final)
                // --- above 0x7F nothing is an introducer: this is the A1 defect ---
                && esc_len(0x80, "x") == 1 &&
                esc_len(0xE4, "\xB8\xAD") == 1  // a WTF-8 lead byte, not an introducer
                && esc_len(0xFF, "x") == 1
                // --- below 0x20 nothing is either: this is the A2 defect ---
                && esc_len(0x00, "x") == 1 &&
                esc_len(0x0A, "b") == 1       // the newline two paragraphs are separated by
                && esc_len(0x1B, "[mx") == 1  // an ESC introduces nothing but is not eaten
                // --- CSI is 0x5B and only 0x5B ---
                && esc_len(0x5B, "1m") == 4 &&
                esc_len(0x5A, "1m") == 2     // ESC Z is DECID, two bytes
                && esc_len(0x5C, "1m") == 2  // ESC \ is ST, two bytes
                // --- CSI parameters, 0x30-0x3F ---
                && esc_len(0x5B, "?@") == 4   // upper 0x3E would give 2
                && esc_len(0x5B, "@m") == 3   // upper 0x40 would give 4: a final byte must
                                              // not be eaten as a parameter
                && esc_len(0x5B, "0m") == 4   // lower 0x31 would give 2
                && esc_len(0x5B, "/0@") == 3  // lower 0x2F would give 5: 0x2F is an
                                              // intermediate, and intermediates may not
                                              // be followed by a parameter
                // --- CSI intermediates, 0x20-0x2F ---
                && esc_len(0x5B, "1 q") == 5    // lower 0x21 would give 3 (DECSCUSR)
                && esc_len(0x5B, "\x1Fm") == 2  // lower 0x1F would give 4: a C0 control is
                                                // not an intermediate byte
                && esc_len(0x5B, "/@") == 4     // upper 0x2E would give 2
                && esc_len(0x5B, " 0@") == 3    // upper 0x30 would give 5
                // --- CSI final bytes, 0x40-0x7E ---
                && esc_len(0x5B, "@") == 3     // lower 0x41 would give 2
                && esc_len(0x5B, " ?") == 3    // lower 0x3F would give 4: after an
                                               // intermediate, a parameter byte is not
                                               // a final byte and ends nothing
                && esc_len(0x5B, "~") == 3     // upper 0x7D would give 2
                && esc_len(0x5B, "\x7F") == 2  // upper 0x7F would give 3
                // --- the five string introducers, each exactly ---
                && esc_len(0x50, "q\x1B\\") == 5  // DCS
                && esc_len(0x58, "q\x1B\\") == 5  // SOS
                && esc_len(0x5D, "q\x1B\\") == 5  // OSC
                && esc_len(0x5E, "q\x1B\\") == 5  // PM
                && esc_len(0x5F, "q\x1B\\") == 5  // APC
                // ...and their neighbours, which are plain two-byte escapes
                && esc_len(0x4F, "q\x1B\\") == 2 && esc_len(0x51, "q\x1B\\") == 2 &&
                esc_len(0x57, "q\x1B\\") == 2 && esc_len(0x59, "q\x1B\\") == 2 &&
                esc_len(0x60, "q\x1B\\") == 2
                // --- BEL terminates a string sequence, 0x06 and 0x08 do not ---
                && esc_len(0x5D, "0;t\x07x") == 6 && esc_len(0x5D, "0;t\x06x") == 7 &&
                esc_len(0x5D, "0;t\x08x") == 7
                // --- so does ST, which is ESC 0x5C and no other second byte ---
                && esc_len(0x5D, "t\x1B\\x") == 5 &&
                esc_len(0x5D, "t\x1B]x") == 6
                // --- a bare ESC at the very end takes only itself ---
                && clapp::detail::escape_sequence_length("\x1B", 0) == 1 &&
                clapp::detail::escape_sequence_length("x\x1B", 1) == 1;
    }

    static_assert(escape_byte_classes_start_and_end_where_ecma48_says(),
                  "clapp: an ECMA-48 byte-class boundary moved. Every line above names the "
                  "mutant it kills; do not relax one to make an edit compile.");

    // Three of the 52 mutants above cannot be killed, because they are equivalent — the
    // byte they newly admit is unreachable at that point in the scan. They are listed so
    // that the next person to run a mutation tool does not spend an afternoon on them:
    //
    //   * `nF final byte >= 0x30` lowered to `>= 0x2F`. The intermediate loop just above
    //     has already consumed every byte in 0x20-0x2F, so the byte the final test sees is
    //     never 0x2F.
    //   * `two-byte introducer >= 0x30` lowered to `>= 0x2F`. The nF branch returns first
    //     for every introducer in 0x20-0x2F, so 0x2F never reaches that line.
    //   * `char_display_width`'s `value < 0x300` shortcut lowered to `< 0x2FF`. U+02FF is
    //     in neither range table, so the lookup it falls through to also answers 1.
    //
    // Those three are equivalent by exhaustive comparison and not merely by argument. The
    // measurement: run each mutant beside the original over every byte string of length
    // <= 4 that begins with ESC — 16,777,216 inputs, which is the whole reachable input
    // space for a scan that only ever starts at an ESC and never looks more than four
    // bytes ahead — and over all 1,114,112 code points for the third. Zero differences.
    // Reproduce it by pasting the mutated function next to the real one in a scratch main
    // and looping; it runs in under a second at -O2.

    consteval bool control_width_boundaries_are_pinned() {
        using clapp::char_display_width;
        return char_display_width(U'\0') == 0 &&
               char_display_width(char32_t{0x1F}) == 0     // last C0 control
               && char_display_width(char32_t{0x20}) == 1  // SPACE occupies a cell
               && char_display_width(char32_t{0x7E}) == 1  // '~'
               && char_display_width(char32_t{0x7F}) == 0  // DEL
               && char_display_width(char32_t{0x9F}) == 0  // last C1 control
               && char_display_width(char32_t{0xA0}) == 1  // NO-BREAK SPACE draws a cell
               // The early return that skips both table lookups. Raising it by one would
               // charge U+0300 a cell; lowering it is an equivalent mutant, because
               // U+02FF is in neither table and the lookup returns 1 anyway.
               && char_display_width(char32_t{0x2FF}) == 1 &&
               char_display_width(char32_t{0x300}) == 0;
    }

    static_assert(control_width_boundaries_are_pinned(),
                  "clapp: a control range in char_display_width() moved. C0/C1 are zero "
                  "cells and the two printable bytes on either side are one.");

    consteval bool only_0x1B_starts_a_sequence() {
        // The byte display_width() and strip_escapes() dispatch on. 0x1A and 0x1C are
        // controls worth zero cells, so a mutant that shifted the test would still return
        // a plausible number — these two make it return a different one.
        return display_width("\x1B[m") == 0 && display_width("\x1A[m") == 2 &&
               display_width("\x1C[m") == 2 && clapp::strip_escapes("\x1A[m") == "\x1A[m"sv;
    }

    static_assert(only_0x1B_starts_a_sequence());

    // ---------------------------------------------------------------------------
    // A stray ESC, and the one definition of "what will be printed"
    // ---------------------------------------------------------------------------

    consteval bool a_stray_escape_never_eats_a_character() {
        // The A1 defect: the fallback consumed two bytes whatever the second one was, so
        // an ESC in front of a CJK character ate its lead byte and left `B8 AD` — not
        // valid UTF-8 at all. Asserted through validate_utf8 as well as by value, because
        // "the result is still a string" is the part that matters downstream.
        const std::string stripped = clapp::strip_escapes("\x1B\u{4E2D}");
        return stripped == "\u{4E2D}"sv &&
               clapp::validate_utf8(std::string_view{stripped}).has_value() &&
               display_width(std::string_view{stripped}) == 2
               // The A2 defect: a newline is a C0 control, not an introducer, so the two
               // paragraphs it separates stay separated.
               && clapp::strip_escapes("a\x1B\nb") == "a\nb"sv &&
               clapp::strip_escapes("para one.\x1B\n\npara two.") == "para one.\n\npara two."sv
               // A second ESC keeps its own sequence: this used to strip to "[mx", which
               // puts the CSI body on screen as text.
               && clapp::strip_escapes("\x1B\x1B[mx") == "x"sv
               // And the real two-byte escapes still go whole.
               && clapp::strip_escapes("\x1B"
                                       "7save\x1B"
                                       "8x") == "savex"sv
               // Through the wrapper as well as through the stripper: a stray ESC is one
               // zero-width byte of the word it sits in, so the character behind it is
               // neither split by a break nor swallowed by the ESC.
               && clapp::strip_escapes(wrap("aaa \x1B\u{4E2D} bbb", 5)) == "aaa\n\u{4E2D}\nbbb"sv;
    }

    static_assert(a_stray_escape_never_eats_a_character());

    /// The A3 invariant: clapp::display_width measures the bytes clapp::strip_escapes
    /// emits. Written as an identity rather than as expected numbers, because the failure
    /// mode is the two answers drifting apart — either one can be "right" on its own.
    consteval bool width_agrees_with_what_is_printed(std::string_view text) {
        const std::string stripped = clapp::strip_escapes(text);
        return display_width(text) == display_width(std::string_view{stripped});
    }

    consteval bool measuring_and_stripping_cannot_disagree() {
        return width_agrees_with_what_is_printed("\x1B[1m--help\x1B[0m")
               // The case that fails when the two use separate scanners: removing the SGR
               // rejoins C3 and A9 into `é`, so the raw bytes measure two replacement
               // glyphs and the printed bytes measure one cell. help.hpp measures first
               // and strips last, so the description column landed a cell off.
               && width_agrees_with_what_is_printed("\xC3\x1B[m\xA9") &&
               width_agrees_with_what_is_printed("\xE4\xB8\x1B[m\xAD") &&
               width_agrees_with_what_is_printed("\xF0\x9F\x1B[m\x98\x82") &&
               width_agrees_with_what_is_printed("\x1B\xE4\xB8\xAD") &&
               width_agrees_with_what_is_printed("a\x1B\nb") &&
               width_agrees_with_what_is_printed("\x1B]0;My Title\x07 x") &&
               width_agrees_with_what_is_printed("a\x1B[1 qb") &&
               width_agrees_with_what_is_printed("\xFF\x1B[m\xFF") &&
               width_agrees_with_what_is_printed("\x1B(B\xC3\x1B(B\xA9")
               // ...and the values themselves, so that "they agree" cannot be satisfied by
               // both of them being wrong in the same direction.
               && display_width("\xC3\x1B[m\xA9") == 1          // é
               && display_width("\xE4\xB8\x1B[m\xAD") == 2      // 中
               && display_width("\xF0\x9F\x1B[m\x98\x82") == 2  // 😂
               && clapp::strip_escapes("\xE4\xB8\x1B[m\xAD") == "\u{4E2D}"sv
               // No escape, no rejoin: two ill-formed subparts stay two.
               && display_width("\xFF\x1B[m\xFF") == 2;
    }

    static_assert(measuring_and_stripping_cannot_disagree(),
                  "clapp: display_width() and strip_escapes() must walk one scanner — the "
                  "renderer measures before it strips, so a disagreement is a column that "
                  "lands in the wrong place.");

    // ---------------------------------------------------------------------------
    // The word definition — clap's textwrap/word_separators.rs
    // ---------------------------------------------------------------------------

    consteval bool words_are(std::string_view line,
                             std::initializer_list<std::string_view> expected) {
        std::size_t pos   = 0;
        std::size_t index = 0;
        while (pos < line.size()) {
            if (index >= expected.size()) return false;
            const std::size_t end = clapp::detail::space_word_end(line, pos);
            if (end <= pos) return false;  // no progress: the wrapper would never finish
            if (line.substr(pos, end - pos) != expected.begin()[index]) return false;
            pos = end;
            ++index;
        }
        return index == expected.size();
    }

    consteval bool clap_word_separator_cases() {
        return words_are("", {})                                        // ascii_space_empty
               && words_are("foo", {"foo"})                             // ascii_single_word
               && words_are("foo bar", {"foo ", "bar"})                 // ascii_two_words
               && words_are("x y z", {"x ", "y ", "z"})                 // ascii_multiple_words
               && words_are(" ", {" "}) && words_are("    ", {"    "})  // ascii_only_whitespace
               && words_are("foo   bar", {"foo   ", "bar"})             // inter_word_whitespace
               && words_are("foo   ", {"foo   "})                       // trailing_whitespace
               && words_are("   foo", {"   ", "foo"})                   // leading_whitespace
               && words_are("\U0001F920", {"\U0001F920"})               // multi_column_char
               && words_are("foo-bar", {"foo-bar"})                     // ascii_hyphens
               && words_are("foo- bar", {"foo- ", "bar"}) &&
               words_are("foo - bar", {"foo ", "- ", "bar"}) &&
               words_are("foo -bar", {"foo ", "-bar"}) &&
               words_are("foo\nbar", {"foo\nbar"})     // ascii_newline
               && words_are("foo\tbar", {"foo\tbar"})  // ascii_tab
               && words_are("foo bar", {"foo bar"});   // non_breaking_space
    }

    static_assert(clap_word_separator_cases());

    consteval bool ill_formed_bytes_never_look_like_a_separator() {
        // Every byte of a multi-byte sequence is >= 0x80, so splitting on U+0020 is
        // safe for input that never decoded in the first place.
        return words_are("a\xFF"
                         " b",
                         {"a\xFF"
                          " ",
                          "b"});
    }

    static_assert(ill_formed_bytes_never_look_like_a_separator());

    // ---------------------------------------------------------------------------
    // Wrapping — clap's textwrap/mod.rs
    // ---------------------------------------------------------------------------
    //
    // clap's shim trims the result and splits it on '\n' before comparing, which hides
    // whether trailing whitespace survived. These assert the raw string, so the answer
    // is visible; `trailing_whitespace` below is where it matters.

    consteval bool clap_wrap_cases() {
        return wrap("foo", 10) == "foo"sv                      // no_wrap
               && wrap("foo bar baz", 5) == "foo\nbar\nbaz"sv  // wrap_simple
               && wrap("To be, or not to be, that is the question.", 10) ==
                          "To be, or\nnot to be,\nthat is\nthe\nquestion."sv  // to_be_or_not
               && wrap("foo bar baz", 10) == "foo bar\nbaz"sv  // multiple_words_on_first_line
               && wrap("foo", 0) == "foo"sv                    // long_word
               && wrap("foo bar", 0) == "foo\nbar"sv           // long_words
               && wrap("  foo bar", 6) == "  foo\n  bar"sv     // leading_whitespace
               && wrap(" foobar baz", 6) == " foobar\n baz"sv  // leading_whitespace_empty_first
               && wrap("aaabbbccc x yyyzzzwww", 9) == "aaabbbccc\nx\nyyyzzzwww"sv  // issue_99
               && wrap("x – x", 1) == "x\n–\nx"sv;                                 // issue_129
    }

    static_assert(clap_wrap_cases());

    consteval bool clap_max_width_case() {
        // clap: `max_width`. usize::MAX here is clapp::unbounded_width, and the point is
        // that the width test must not overflow into "every line is full".
        const std::string_view text = "Hello there! This is some English text. "
                                      "It should not be wrapped given the extents below.";
        return wrap("foo bar", clapp::unbounded_width) == "foo bar"sv &&
               wrap(text, clapp::unbounded_width) == text;
    }

    static_assert(clap_max_width_case());

    consteval bool clap_trailing_whitespace_case() {
        // clap: `trailing_whitespace`. Whitespace is dropped at a break and kept at the
        // end of the input — clap's shim trims the tail before comparing, so the two
        // trailing spaces below are invisible in clap's own test and asserted here.
        return wrap("foo     bar     baz  ", 5) == "foo\nbar\nbaz  "sv &&
               clapp::trim_end(wrap("foo     bar     baz  ", 5)) == "foo\nbar\nbaz"sv;
    }

    static_assert(clap_trailing_whitespace_case());

    // ---------------------------------------------------------------------------
    // The failure modes
    // ---------------------------------------------------------------------------

    consteval bool a_word_wider_than_the_line_gets_a_line_to_itself() {
        // Must not split the word (a URL has to stay copy-pasteable), must not drop it,
        // and must not spin: this whole function is evaluated by the compiler, so a
        // wrapper that failed to advance would blow the constexpr step limit instead of
        // hanging a test process.
        return wrap("supercalifragilistic", 3) == "supercalifragilistic"sv &&
               wrap("a supercalifragilistic b", 3) == "a\nsupercalifragilistic\nb"sv &&
               wrap("aaaa bbbb", 1) == "aaaa\nbbbb"sv;
    }

    static_assert(a_word_wider_than_the_line_gets_a_line_to_itself());

    consteval bool a_line_narrower_than_one_wide_character_still_works() {
        // Two cells will not fit in one column and there is nothing to be done about it;
        // the requirement is that the character survives intact and each word still gets
        // its own line.
        return wrap("中", 1) == "中"sv && wrap("中 文", 1) == "中\n文"sv &&
               wrap("中文 中文", 1) == "中文\n中文"sv && wrap("中 文", 0) == "中\n文"sv;
    }

    static_assert(a_line_narrower_than_one_wide_character_still_works());

    consteval bool embedded_newlines_are_honoured_not_reflowed() {
        // A '\n' the caller wrote is a line break, is never removed, and resets the
        // column count — so the text after it gets a full line, not the remainder of the
        // one before it.
        return wrap("ab\ncd", 10) == "ab\ncd"sv && wrap("a b\nc d", 3) == "a b\nc d"sv &&
               wrap("aaaa\nbb cc", 5) == "aaaa\nbb cc"sv &&
               wrap("aa bb\ncc dd", 2) == "aa\nbb\ncc\ndd"sv &&
               wrap("\n\n", 5) == "\n\n"sv
               // The indent is re-detected per line rather than carried across one.
               && wrap("  a b\nc d", 3) == "  a\n  b\nc d"sv;
    }

    static_assert(embedded_newlines_are_honoured_not_reflowed());

    consteval bool ill_formed_bytes_pass_through_unharmed() {
        return wrap("a\xFF"
                    " b",
                    2) == "a\xFF"
                          "\nb"sv &&
               wrap("\xFF\xFF\xFF", 1) == "\xFF\xFF\xFF"sv;
    }

    static_assert(ill_formed_bytes_pass_through_unharmed());

    // The property that matters more than any single expected string: wrapping only
    // inserts line breaks and indentation and only removes whitespace. Nothing visible
    // may be lost, reordered or duplicated.
    /// \p text with every whitespace *character* removed — characters and not bytes,
    /// because the wrapper retracts a `U+00A0` whole and a byte filter would see two
    /// orphaned bytes go missing and call it a lost character.
    consteval std::string without_whitespace(std::string_view text) {
        std::string out;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const clapp::detail::scan_result scan = clapp::detail::scan_one(text, pos);
            const bool blank                      = scan.ok && clapp::detail::is_unicode_space(
                                                                       static_cast<char32_t>(scan.code_point));
            for (std::size_t i = 0; !blank && i < scan.length; ++i) out.push_back(text[pos + i]);
            pos += scan.length;
        }
        return out;
    }

    consteval bool keeps_every_visible_byte(std::string_view input, std::size_t width) {
        return without_whitespace(input) ==
               without_whitespace(std::string_view{wrap(input, width)});
    }

    consteval bool nothing_visible_is_ever_lost() {
        // The sweep is deliberately short. clang-p2996's default constexpr step limit is
        // 1048576 and a `std::string` built one `push_back` at a time is expensive, so a
        // wider sweep over a longer paragraph fails to *evaluate* rather than failing to
        // hold — which reads as an infinite-loop diagnostic and sends the reader hunting
        // for a bug that is not there. The runtime case at the bottom of this file
        // sweeps further. Widths 0 and 1 are the ones that matter here anyway: they are
        // where a break happens on every word.
        const std::string_view mixed = "To be 中文 or a supercalifragilistic word.";
        for (std::size_t width = 0; width <= 8; ++width) {
            if (!keeps_every_visible_byte(mixed, width)) return false;
        }
        return keeps_every_visible_byte("  indented\ttext\nsecond line", 6) &&
               keeps_every_visible_byte("a\xFF"
                                        " b\xFF\xFF c",
                                        3) &&
               keeps_every_visible_byte("aaa\u{00A0} bbb\u{3000} ccc", 4) &&
               keeps_every_visible_byte(mixed, clapp::unbounded_width);
    }

    static_assert(nothing_visible_is_ever_lost());

    // ---------------------------------------------------------------------------
    // clapp::line_wrapper — the incremental form
    // ---------------------------------------------------------------------------

    consteval bool wrapper_carries_the_column_across_calls() {
        // A fragment boundary is not a word boundary: "123" + "45" must be charged five
        // cells, not treated as two words of two and three.
        std::string out;
        line_wrapper wrapper{20};
        wrapper.wrap_into("12345 12345 ", out);
        wrapper.wrap_into("12345 12345", out);
        return out == "12345 12345 12345\n12345"sv && wrapper.line_width() == 5;
    }

    static_assert(wrapper_carries_the_column_across_calls());

    consteval bool wrapper_resets_at_a_newline_not_at_a_call_boundary() {
        // Difference 2 in the header's file comment: clap resets only between the lines
        // *inside* one call, so a chunk ending in '\n' leaks its column count into the
        // next chunk and the first line after the boundary wraps early. Here the second
        // call starts at column zero, as it should.
        std::string out;
        line_wrapper wrapper{5};
        wrapper.wrap_into("aaaaa\n", out);
        if (wrapper.line_width() != 0) return false;
        wrapper.wrap_into("bbbbb", out);
        return out == "aaaaa\nbbbbb"sv;
    }

    static_assert(wrapper_resets_at_a_newline_not_at_a_call_boundary());

    consteval bool wrapper_adopts_leading_spaces_as_a_hanging_indent() {
        std::string out;
        line_wrapper wrapper{6};
        wrapper.wrap_into("  foo bar", out);
        return wrapper.indentation() == "  "sv && out == "  foo\n  bar"sv;
    }

    static_assert(wrapper_adopts_leading_spaces_as_a_hanging_indent());

    consteval bool a_bare_newline_is_not_an_indent() {
        // Difference 3: clap adopts any first word that trims to empty, so a line that
        // is nothing but "\n" becomes the indent for everything after it.
        std::string out;
        line_wrapper wrapper{4};
        wrapper.wrap_into("\n", out);
        wrapper.wrap_into("aa bb", out);
        return out == "\naa\nbb"sv;
    }

    static_assert(a_bare_newline_is_not_an_indent());

    consteval bool reset_forgets_the_indent_and_the_column() {
        line_wrapper wrapper{10};
        std::string out;
        wrapper.wrap_into("  ab", out);
        if (wrapper.line_width() == 0 || wrapper.indentation().empty()) return false;
        wrapper.reset();
        return wrapper.line_width() == 0 && wrapper.indentation().empty() &&
               wrapper.hard_width() == 10;
    }

    static_assert(reset_forgets_the_indent_and_the_column());

    // ---------------------------------------------------------------------------
    // Indentation and the hanging indent
    // ---------------------------------------------------------------------------

    consteval bool indent_prefixes_the_first_line_and_every_later_one() {
        return indent("a\nb\nc", "> ", "| ") == "> a\n| b\n| c"sv &&
               indent("a\nb", "", "  ") == "a\n  b"sv && indent("", "> ", "| ") == "> "sv &&
               indent("a\n", "", "..") == "a\n.."sv;  // a trailing newline still indents
    }

    static_assert(indent_prefixes_the_first_line_and_every_later_one());

    consteval bool wrap_hanging_lines_continuations_up_under_the_first() {
        //  0        1
        //  1234567890123456
        //  -v, --verbose  Say much
        //                 more
        const std::string_view help = "Say much more about what is happening";
        return clapp::wrap_hanging(help, 36, 17) ==
               "Say much more about\n                 what is happening"sv;
    }

    static_assert(wrap_hanging_lines_continuations_up_under_the_first());

    consteval bool wrap_hanging_saturates_when_the_indent_swallows_the_width() {
        // clap's saturating_sub: an available width of zero puts every word on its own
        // line, which is ugly but readable. The alternative — treating it as unbounded —
        // produces one line far wider than the terminal.
        return clapp::wrap_hanging("a b c", 4, 10) == "a\n          b\n          c"sv;
    }

    static_assert(wrap_hanging_saturates_when_the_indent_swallows_the_width());

    // ---------------------------------------------------------------------------
    // trim_end
    // ---------------------------------------------------------------------------

    consteval bool trim_end_removes_trailing_whitespace() {
        return clapp::trim_end("abc  \t\n\r") == "abc"sv && clapp::trim_end("   ").empty() &&
               clapp::trim_end("").empty() && clapp::trim_end("  abc") == "  abc"sv;
    }

    static_assert(trim_end_removes_trailing_whitespace());

    // This assertion used to be named `trim_end_removes_ascii_whitespace_only` and the
    // name was the specification: clapp trimmed the six ASCII whitespace bytes and left
    // U+00A0 and its friends on the end of the line. 差异清单 #26 claimed that agreed with
    // clap's `str::trim_end` "on every input clap itself produces", and it does not —
    // measured against clap 4.5, a retracted U+00A0 stayed put and made the line one cell
    // wider than the width it had been computed for. The set below is clap's, enumerated
    // under rustc 1.98.0-nightly rather than remembered; #26 now records the one thing
    // that *does* still differ, which is the metric and not the set.
    consteval bool trim_end_uses_claps_whitespace_set() {
        return clapp::trim_end("abc\u{00A0}") == "abc"sv        // NO-BREAK SPACE
               && clapp::trim_end("abc\u{0085}") == "abc"sv     // NEXT LINE
               && clapp::trim_end("abc\u{1680}") == "abc"sv     // OGHAM SPACE MARK
               && clapp::trim_end("abc\u{2000}") == "abc"sv     // EN QUAD, first of a run
               && clapp::trim_end("abc\u{200A}") == "abc"sv     // HAIR SPACE, last of it
               && clapp::trim_end("abc\u{2028}") == "abc"sv     // LINE SEPARATOR
               && clapp::trim_end("abc\u{2029}") == "abc"sv     // PARAGRAPH SEPARATOR
               && clapp::trim_end("abc\u{202F}") == "abc"sv     // NARROW NO-BREAK SPACE
               && clapp::trim_end("abc\u{205F}") == "abc"sv     // MEDIUM MATHEMATICAL SPACE
               && clapp::trim_end("abc\u{3000}") == "abc"sv     // IDEOGRAPHIC SPACE
               && clapp::trim_end("abc\u{00A0} \t") == "abc"sv  // a mixed run, all of it
               // The two that look like whitespace and are not: both lost the White_Space
               // property, both are kept by Rust's trim_end, and trimming either would
               // delete a character clap prints. Verified with rustc 1.98.0-nightly.
               && clapp::trim_end("abc\u{200B}") == "abc\u{200B}"sv  // ZERO WIDTH SPACE
               && clapp::trim_end("abc\u{180E}") == "abc\u{180E}"sv  // MONGOLIAN VOWEL SEP
               // Just outside the ranges, so a boundary that slipped by one shows up.
               && clapp::trim_end("abc\u{0084}") == "abc\u{0084}"sv &&
               clapp::trim_end("abc\u{0086}") == "abc\u{0086}"sv &&
               clapp::trim_end("abc\u{009F}") == "abc\u{009F}"sv &&
               clapp::trim_end("abc\u{00A1}") == "abc\u{00A1}"sv &&
               clapp::trim_end("abc\u{1FFF}") == "abc\u{1FFF}"sv &&
               clapp::trim_end("abc\u{200B}") == "abc\u{200B}"sv &&
               clapp::trim_end("abc\u{2027}") == "abc\u{2027}"sv &&
               clapp::trim_end("abc\u{202A}") == "abc\u{202A}"sv &&
               clapp::trim_end("abc\u{202E}") == "abc\u{202E}"sv &&
               clapp::trim_end("abc\u{2030}") == "abc\u{2030}"sv &&
               clapp::trim_end("abc\u{205E}") == "abc\u{205E}"sv &&
               clapp::trim_end("abc\u{2060}") == "abc\u{2060}"sv &&
               clapp::trim_end("abc\u{2FFF}") == "abc\u{2FFF}"sv &&
               clapp::trim_end("abc\u{3001}") == "abc\u{3001}"sv
               // Ill-formed bytes are never whitespace, whatever they would decode to.
               && clapp::trim_end("abc\xC2") == "abc\xC2"sv &&
               clapp::trim_end("abc\xA0") == "abc\xA0"sv;
    }

    static_assert(trim_end_uses_claps_whitespace_set());

    consteval bool trim_end_retracts_a_wide_space_from_a_break() {
        // The defect 差异清单 #26 used to deny, as a wrap rather than as a trim: with the
        // ASCII rule this was "aaa\u{00A0}\nbbb", a four-cell first line for a width of
        // three. clap 4.5 gives "aaa\nbbb" here; measured with a standalone Rust probe
        // using unicode-width 0.2.2 and rustc 1.98.0-nightly.
        return wrap("aaa\u{00A0} bbb", 3) == "aaa\nbbb"sv &&
               wrap("aaa\u{00A0} bbb", 7) == "aaa\nbbb"sv
               // Widths 2..7 and 9 agree with clap byte for byte. Width 8 is the one
               // clap breaks and clapp does not, because clap adds the *byte* length of
               // the retracted run to a counter of cells — the residue #26 now records.
               && wrap("aaa\u{00A0} bbb", 9) == "aaa\u{00A0} bbb"sv &&
               wrap("aaa\u{00A0} bbb", 8) == "aaa\u{00A0} bbb"sv
               // A U+00A0 that is not trailing is content and never a break point: this
               // is clap's `ascii_non_breaking_space` word case, seen through the wrapper.
               && wrap("aaa\u{00A0}bbb ccc", 4) == "aaa\u{00A0}bbb\nccc"sv;
    }

    static_assert(trim_end_retracts_a_wide_space_from_a_break());

    consteval bool trim_end_drops_fragments_that_become_empty() {
        styled_str message;
        message.push(style_class::literal, "--help");
        message.push_plain("   ");
        const styled_str trimmed = clapp::trim_end(message);
        return trimmed.span_count() == 1 && trimmed.to_string() == "--help" &&
               trimmed.spans()[0].class_ == style_class::literal &&
               clapp::trim_end(styled_str{"   "}).empty();
    }

    static_assert(trim_end_drops_fragments_that_become_empty());

    // ---------------------------------------------------------------------------
    // Wrapping a styled message — clap's builder/styled_str.rs
    // ---------------------------------------------------------------------------

    consteval bool clap_wrap_unstyled_case() {
        // clap: `wrap_unstyled`. One text run, so the break can retract the space that
        // preceded it.
        const styled_str message{"12345 12345 12345 12345"};
        return wrap(message, 20).to_string() == "12345 12345 12345\n12345";
    }

    static_assert(clap_wrap_unstyled_case());

    consteval bool clap_wrap_styled_case() {
        // clap's `wrap_styled`: the default literal style is bold, so each literal is a
        // separate text run. The wrapper carries its column across those runs, but a
        // break at the first word of a new run cannot retract the plain space emitted by
        // the previous run. The palette therefore changes the bytes even after colour is
        // stripped. With a plain palette all fragments coalesce into one effective run
        // and the ordinary unstyled answer remains unchanged.
        styled_str message;
        for (int i = 0; i < 4; ++i) {
            if (i > 0) message.push_plain(" ");
            message.push(style_class::literal, "12345");
        }
        const styled_str styled = wrap(message, 20, styles::styled());
        const styled_str plain  = wrap(message, 20, styles::plain());
        return styled.to_string() == "12345 12345 12345 \n12345" &&
               styled.text_of(style_class::literal) == "123451234512345\n12345" &&
               styled.text_of(style_class::plain) == "   " &&
               plain.to_string() == "12345 12345 12345\n12345";
    }

    static_assert(clap_wrap_styled_case());

    consteval bool custom_palette_boundaries_preserve_spaces_and_classes() {
        styled_str message;
        message.push(style_class::context, "aa ");
        message.push(style_class::context_value, "bbbbb");
        message.push_plain(" ccccc");

        const styles palette     = styles::plain()
                                           .with(style_class::context, style{}.bold())
                                           .with(style_class::context_value, style{}.underline());
        const styled_str wrapped = wrap(message, 6, palette);
        return wrapped.to_string() == "aa \nbbbbb\nccccc" &&
               wrapped.text_of(style_class::context) == "aa " &&
               wrapped.text_of(style_class::context_value) == "\nbbbbb" &&
               wrapped.text_of(style_class::plain) == "\nccccc";
    }

    static_assert(custom_palette_boundaries_preserve_spaces_and_classes());

    consteval bool styled_run_boundaries_can_split_one_semantic_word() {
        // The plain spelling is one word and therefore stays intact. With the default
        // palette, `literal` and `invalid` each produce an ANSI-delimited run in clap;
        // the incremental wrapper may break at both run boundaries.
        styled_str message;
        message.push_plain("x ");
        message.push(style_class::literal, "--he");
        message.push(style_class::invalid, "lp");

        const styled_str styled = wrap(message, 5, styles::styled());
        const styled_str plain  = wrap(message, 5, styles::plain());
        return styled.to_string() == "x \n--he\nlp" &&
               styled.text_of(style_class::literal) == "\n--he" &&
               styled.text_of(style_class::invalid) == "\nlp" && plain.to_string() == "x\n--help";
    }

    static_assert(styled_run_boundaries_can_split_one_semantic_word());

    consteval bool styled_wrap_charges_a_split_word_its_whole_width() {
        // "--he" + "lp" is one six-cell word, not two words of four and two: had the
        // wrapper restarted its column count at the fragment boundary the result would be
        // "x\n--he\nlp".
        //
        // The stated reason used to be half a rule. The expectation was "x \n--help",
        // with the space kept, because retraction stopped at a fragment boundary too — so
        // this case pinned the word-extent rule and the retraction defect at once and
        // could not tell them apart. Both cross the boundary now, so the space goes, and
        // this is byte for byte `wrap("x --help", 5)` on the flattened text — asserted
        // below rather than described, since "the styled overload agrees with the plain
        // one" is the actual claim.
        styled_str message;
        message.push_plain("x ");
        message.push(style_class::literal, "--he");
        message.push(style_class::literal, "lp");
        return message.span_count() == 2 && wrap(message, 5).to_string() == "x\n--help" &&
               wrap(message, 5).to_string() == wrap(message.to_string(), 5);
    }

    static_assert(styled_wrap_charges_a_split_word_its_whole_width());

    consteval bool styled_wrap_agrees_with_the_plain_wrapper_on_every_boundary() {
        // The general form of the claim above, and the one a future fragmentation change
        // has to keep true: for the same bytes, the fragment layout must not be
        // observable in the output. Six spans, boundaries deliberately placed mid-word,
        // after a space and before a space.
        styled_str message;
        message.push(style_class::literal, "--origin");
        message.push_plain(", ");
        message.push(style_class::literal, "--pa");
        message.push(style_class::literal, "th");
        message.push_plain(", ");
        message.push(style_class::literal, "--tryfrom");
        message.push_plain(", source");
        const std::string flat = message.to_string();
        for (std::size_t width = 1; width <= 34; ++width) {
            if (wrap(message, width).to_string() != clapp::trim_end(wrap(flat, width)))
                return false;
        }
        return true;
    }

    static_assert(styled_wrap_agrees_with_the_plain_wrapper_on_every_boundary());

    consteval bool styled_wrap_trims_the_tail() {
        const styled_str message{"abc   "};
        return wrap(message, 40).to_string() == "abc";
    }

    static_assert(styled_wrap_trims_the_tail());

    consteval bool styled_indent_keeps_the_indent_unstyled() {
        styled_str message;
        message.push(style_class::literal, "--flag\nmore");
        const styled_str indented = indent(message, "", "  ");
        return indented.to_string() == "--flag\n  more" &&
               indented.text_of(style_class::plain) == "  " &&
               indented.text_of(style_class::literal) == "--flag\nmore";
    }

    static_assert(styled_indent_keeps_the_indent_unstyled());

    // ---------------------------------------------------------------------------
    // Choosing the width
    // ---------------------------------------------------------------------------

    consteval bool parse_terminal_width_accepts_what_rust_accepts() {
        using clapp::parse_terminal_width;
        return parse_terminal_width("80") == std::optional<std::size_t>{80} &&
               parse_terminal_width("+80") == std::optional<std::size_t>{80} &&
               parse_terminal_width("") == std::nullopt &&
               parse_terminal_width("+") == std::nullopt &&
               parse_terminal_width(" 80") == std::nullopt &&
               parse_terminal_width("80 ") == std::nullopt &&
               parse_terminal_width("80x") == std::nullopt &&
               parse_terminal_width("-80") == std::nullopt &&
               parse_terminal_width("0") == std::nullopt &&
               parse_terminal_width("99999999999999999999999999") == std::nullopt;
    }

    static_assert(parse_terminal_width_accepts_what_rust_accepts());

    consteval bool resolve_wrap_width_follows_claps_order() {
        using clapp::default_terminal_width;
        using clapp::resolve_wrap_width;
        using opt = std::optional<std::size_t>;

        return
                // An explicit term_width wins, and is not capped by max_term_width.
                resolve_wrap_width(opt{120}, opt{80}, opt{200}) == 120
                // term_width == 0 means "never wrap".
                && resolve_wrap_width(opt{0}, opt{80}, opt{200}) == clapp::unbounded_width
                // No term_width: the terminal decides, capped by max_term_width.
                && resolve_wrap_width(std::nullopt, opt{80}, opt{200}) == 80 &&
                resolve_wrap_width(std::nullopt, opt{200}, opt{80}) == 80
                // An unset or zero cap is no cap.
                && resolve_wrap_width(std::nullopt, std::nullopt, opt{200}) == 200 &&
                resolve_wrap_width(std::nullopt, opt{0}, opt{200}) == 200
                // No terminal: clap's 100, still capped.
                && resolve_wrap_width(std::nullopt, std::nullopt, std::nullopt) ==
                           default_terminal_width &&
                resolve_wrap_width(std::nullopt, opt{60}, std::nullopt) == 60 &&
                resolve_wrap_width(std::nullopt, opt{200}, std::nullopt) == default_terminal_width;
    }

    static_assert(resolve_wrap_width_follows_claps_order());

    // ---------------------------------------------------------------------------
    // Runtime
    // ---------------------------------------------------------------------------
    //
    // Arguments are bound to named objects first, never passed as literals: GCC 16.1.0
    // miscompiles some constexpr functions called directly on string literals and
    // stored into a variable, so a runtime call can disagree with its own
    // static_assert. See CLAUDE.md, known toolchain workarounds.

    constexpr std::string_view prose      = "To be, or not to be, that is the question.";
    constexpr std::string_view wide_text  = "中 文";
    constexpr std::string_view ill_formed = "a\xFF"
                                            " b";

    CLAPP_TEST("textwrap: display_width agrees with its compile-time self") {
        constexpr std::string_view cafe  = "Café Plain";
        constexpr std::string_view emoji = "😂😭🥺🤣✨😍🙏🥰😊🔥";
        CLAPP_CHECK(display_width(cafe) == 10);
        CLAPP_CHECK(cafe.size() == 11);
        CLAPP_CHECK(display_width(emoji) == 20);
        CLAPP_CHECK(display_width(wide_text) == 5);
        CLAPP_CHECK(display_width(ill_formed) == 4);
    }

    CLAPP_TEST("textwrap: wrap agrees with its compile-time self") {
        CLAPP_CHECK(wrap(prose, 10) == "To be, or\nnot to be,\nthat is\nthe\nquestion.");
        CLAPP_CHECK(wrap(wide_text, 1) == "中\n文");
        CLAPP_CHECK(wrap(ill_formed, 2) == "a\xFF"
                                           "\nb");
    }

    CLAPP_TEST("textwrap: a styled message keeps its classes through a wrap") {
        styled_str message;
        message.push(style_class::literal, "--verbose");
        message.push_plain(" ");
        message.push(style_class::placeholder, "<LEVEL>");
        const styled_str wrapped = wrap(message, 10);
        // The separating space is retracted at the break even though it is a fragment of
        // its own — the message is wrapped as one stream. This read "--verbose \n<LEVEL>"
        // while retraction stopped at fragment boundaries.
        CLAPP_CHECK(wrapped.to_string() == "--verbose\n<LEVEL>");
        CLAPP_CHECK(wrapped.text_of(style_class::literal) == "--verbose");
        // The inserted '\n' joins the fragment it was emitted inside, exactly as clap's
        // `[1m\n12345[0m` snapshot shows. A newline carries no colour, so this is
        // invisible in the rendered output and only observable through text_of().
        CLAPP_CHECK(wrapped.text_of(style_class::placeholder) == "\n<LEVEL>");
    }

    CLAPP_TEST("textwrap: wrap_hanging lays a description out under its option") {
        constexpr std::string_view help = "Say much more about what is happening";
        const std::string body          = clapp::wrap_hanging(help, 36, 17);
        CLAPP_CHECK(body == "Say much more about\n                 what is happening");
    }

    CLAPP_TEST("textwrap: parse_terminal_width rejects everything but a bare number") {
        constexpr std::string_view eighty = "80";
        constexpr std::string_view junk   = "80 columns";
        CLAPP_CHECK(clapp::parse_terminal_width(eighty) == std::optional<std::size_t>{80});
        CLAPP_CHECK(clapp::parse_terminal_width(junk) == std::nullopt);
    }

    CLAPP_TEST("textwrap: detect_terminal_width answers without a terminal") {
        // The only I/O in the header. Under ctest stdout is a pipe, so this is normally
        // nullopt — but a developer running the binary from a terminal gets a real
        // width, and COLUMNS may be exported either way. All this can assert is that the
        // call is safe and that whatever it returns is usable: never zero, and never a
        // value clapp::resolve_wrap_width would then mishandle.
        const std::optional<std::size_t> detected = clapp::detect_terminal_width();
        if (detected.has_value()) {
            CLAPP_CHECK(detected.value() > 0);
            CLAPP_CHECK(clapp::resolve_wrap_width(std::nullopt, std::nullopt, detected) ==
                        detected.value());
        } else {
            CLAPP_CHECK(clapp::resolve_wrap_width(std::nullopt, std::nullopt, detected) ==
                        clapp::default_terminal_width);
        }
    }

    CLAPP_TEST("textwrap: no width ever splits an escape sequence") {
        // The compile-time contract in <clapp/output/textwrap.hpp> pins three shapes at one
        // width each. This sweeps every width, which is what an off-by-one in the fits test
        // would need to show itself, and it checks the property against an *independent*
        // scanner: `sequences_of` below knows nothing about escape_sequence_length() and
        // reads the sequence boundary straight off ECMA-48, so a bug in the header's scanner
        // cannot make both sides agree.
        //
        // Two of the payloads carry a U+0020 *inside* the sequence — a CSI intermediate byte
        // and an OSC string — which is the only way clap's word definition can be led into
        // one. The third is the SGR shape that never had the bug, kept as the control.
        for (const std::string_view input : {"aaaa \x1B[1 q bbbb cccc dddd"sv,
                                             "\x1B]0;My Long Window Title\x07 aaaa bbbb cccc"sv,
                                             "\x1B[31mred\x1B[0m green blue yellow"sv}) {
            const std::vector<std::string> expected = sequences_of(input);
            CLAPP_CHECK(!expected.empty());
            for (std::size_t width = 1; width <= 30; ++width) {
                const std::string wrapped = wrap(input, width);
                // The same sequences, whole, in the same order — no newline inside any of
                // them and no byte of one left behind.
                CLAPP_CHECK(sequences_of(wrapped) == expected);
            }
        }
    }

    CLAPP_TEST("textwrap: strip_escapes removes whole sequences and nothing else") {
        CLAPP_CHECK(clapp::strip_escapes("\x1B[1m--help\x1B[0m x"sv) == "--help x");
        CLAPP_CHECK(clapp::strip_escapes("a\x1B[1 qb"sv) == "ab");
        CLAPP_CHECK(clapp::strip_escapes("\x1B]0;a title\x07z"sv) == "z");
        CLAPP_CHECK(clapp::strip_escapes("a\tb\nc"sv) == "a\tb\nc");
        CLAPP_CHECK(clapp::strip_escapes("plain"sv) == "plain");

        styled_str dirty;
        dirty.push(style_class::header, "Options:").push_plain(" \x1B[1mx\x1B[0m");
        styled_str clean;
        clean.push(style_class::header, "Options:").push_plain(" x");
        CLAPP_CHECK(clapp::strip_escapes(dirty) == clean);
    }

    CLAPP_TEST("textwrap: wrapping a paragraph never exceeds the width it was given") {
        // The one guarantee a greedy wrapper can make: a line is over-wide only when a
        // single word is. Checked across every width rather than at one, because an
        // off-by-one in the fits/does-not-fit test shows up at exactly one width.
        const std::string_view paragraph = "The quick brown fox jumps over the lazy dog "
                                           "and then keeps running for quite a while.";
        for (std::size_t width = 1; width <= 45; ++width) {
            const std::string wrapped = wrap(paragraph, width);
            std::size_t start         = 0;
            while (start <= wrapped.size()) {
                const std::size_t newline = wrapped.find('\n', start);
                const std::size_t end     = newline == std::string::npos ? wrapped.size() : newline;
                const std::string_view line{wrapped.data() + start, end - start};
                const std::size_t measured = display_width(clapp::detail::trim_space_end(line));
                if (measured > width) {
                    // Over-wide is only allowed when the line holds exactly one word.
                    CLAPP_CHECK(clapp::detail::space_word_end(line, 0) >= line.size());
                }
                if (newline == std::string::npos) break;
                start = newline + 1;
            }
        }
    }

}  // namespace
