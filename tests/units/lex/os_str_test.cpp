#include <clapp/lex/os_str.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace {

    using clapp::encoding_error;
    using clapp::invalid_encoding;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::validate_utf8;
    using clapp::validate_wtf8;

    // ---------------------------------------------------------------------------
    // Sample byte strings
    //
    // Written as hex escapes rather than as literal characters so the tests do not
    // depend on the encoding of this source file. Note that a hex escape swallows
    // every following hex digit, so a trailing ASCII letter that happens to be one
    // (a..f) must live in a separately concatenated literal.
    // ---------------------------------------------------------------------------

    constexpr std::string_view empty_text{};
    constexpr std::string_view ascii_text     = "clapp";
    constexpr std::string_view two_byte_seq   = "\xC2\xA9";          // U+00A9 (c)
    constexpr std::string_view three_byte_seq = "\xE4\xB8\xAD";      // U+4E2D
    constexpr std::string_view four_byte_seq  = "\xF0\x9F\x92\xA9";  // U+1F4A9
    constexpr std::string_view max_code_point = "\xF4\x8F\xBF\xBF";  // U+10FFFF
    constexpr std::string_view mixed_text     = "a\xC2\xA9\xE4\xB8\xAD\xF0\x9F\x92\xA9";

    // Real POSIX argv may contain NUL bytes; the length has to be explicit.
    constexpr std::string_view text_with_nul{"a\0b", 3};

    constexpr std::string_view high_surrogate_seq  = "\xED\xA0\x80";  // U+D800
    constexpr std::string_view low_surrogate_seq   = "\xED\xB0\x80";  // U+DC00
    constexpr std::string_view unmerged_pair       = "\xED\xA0\x80\xED\xB0\x80";
    constexpr std::string_view two_high_surrogates = "\xED\xA0\x80\xED\xA0\x80";

    // ---------------------------------------------------------------------------
    // Helpers that keep `std::optional` / `std::expected` accessors guarded.
    //
    // `optional::operator->` and `operator*` are not reliably usable in constant
    // evaluation once _GLIBCXX_ASSERTIONS is on (see CLAUDE.md), so every unwrap
    // below checks engagement first and goes through `value()`.
    // ---------------------------------------------------------------------------

    constexpr invalid_encoding utf8_error_of(std::string_view text) {
        const auto result = validate_utf8(text);
        return result.has_value() ? invalid_encoding{} : result.error();
    }

    constexpr invalid_encoding wtf8_error_of(std::string_view text) {
        const auto result = validate_wtf8(text);
        return result.has_value() ? invalid_encoding{} : result.error();
    }

    constexpr os_str head_of(os_str text, char separator) {
        const auto parts = text.split_once(separator);
        return parts.has_value() ? parts.value().first : os_str{};
    }

    constexpr os_str tail_of(os_str text, char separator) {
        const auto parts = text.split_once(separator);
        return parts.has_value() ? parts.value().second : os_str{};
    }

    constexpr std::u16string_view as_utf16(const auto& units) {
        return std::u16string_view{units.data(), units.size()};
    }

    // ---------------------------------------------------------------------------
    // Strict UTF-8: what must be accepted
    // ---------------------------------------------------------------------------

    static_assert(validate_utf8(empty_text).has_value(), "the empty string is valid UTF-8");
    static_assert(validate_utf8(ascii_text).has_value());
    static_assert(validate_utf8(two_byte_seq).has_value());
    static_assert(validate_utf8(three_byte_seq).has_value());
    static_assert(validate_utf8(four_byte_seq).has_value());
    static_assert(validate_utf8(max_code_point).has_value(),
                  "U+10FFFF is the last valid code point");
    static_assert(validate_utf8(mixed_text).has_value());
    static_assert(validate_utf8(text_with_nul).has_value(),
                  "an embedded NUL is U+0000, a perfectly ordinary code point");

    // The lowest and highest representative of each sequence length.
    static_assert(validate_utf8(std::string_view{"\0", 1}).has_value());  // U+0000
    static_assert(validate_utf8("\x7F").has_value());                     // U+007F
    static_assert(validate_utf8("\xC2\x80").has_value());                 // U+0080
    static_assert(validate_utf8("\xDF\xBF").has_value());                 // U+07FF
    static_assert(validate_utf8("\xE0\xA0\x80").has_value());             // U+0800
    static_assert(validate_utf8("\xEF\xBF\xBF").has_value());             // U+FFFF
    static_assert(validate_utf8("\xF0\x90\x80\x80").has_value());         // U+10000
    static_assert(validate_utf8("\xED\x9F\xBF").has_value());             // U+D7FF
    static_assert(validate_utf8("\xEE\x80\x80").has_value());             // U+E000

    // ---------------------------------------------------------------------------
    // Strict UTF-8: what must be rejected, and where
    // ---------------------------------------------------------------------------

    // Continuation byte with no lead byte.
    static_assert(utf8_error_of("\x80") ==
                  invalid_encoding{.valid_up_to = 0,
                                   .error_len   = 1,
                                   .kind        = encoding_error::unexpected_continuation});
    static_assert(utf8_error_of("\xBF") ==
                  invalid_encoding{.valid_up_to = 0,
                                   .error_len   = 1,
                                   .kind        = encoding_error::unexpected_continuation});

    // The offset is the offset of the offending byte, not of the whole argument.
    static_assert(utf8_error_of("abc\x80").valid_up_to == 3);
    static_assert(utf8_error_of(std::string_view{"a\xC2\xA9\x80", 4}).valid_up_to == 3,
                  "valid_up_to counts bytes, not code points");

    // Lead byte with no continuation at all: the input simply ended.
    static_assert(utf8_error_of("\xC2") == invalid_encoding{.valid_up_to = 0,
                                                            .error_len   = 0,
                                                            .kind = encoding_error::truncated});
    static_assert(utf8_error_of("a\xE4\xB8") ==
                  invalid_encoding{
                          .valid_up_to = 1, .error_len = 0, .kind = encoding_error::truncated});
    static_assert(utf8_error_of("\xF0\x9F\x92") ==
                  invalid_encoding{
                          .valid_up_to = 0, .error_len = 0, .kind = encoding_error::truncated});

    // Lead byte followed by something that is not a continuation byte. Distinct from
    // `truncated`: more input cannot rescue this one, hence a non-zero error_len.
    static_assert(utf8_error_of("\xC2Z") ==
                  invalid_encoding{.valid_up_to = 0,
                                   .error_len   = 1,
                                   .kind        = encoding_error::missing_continuation});
    static_assert(utf8_error_of("\xE4\xB8Z") ==
                  invalid_encoding{.valid_up_to = 0,
                                   .error_len   = 2,
                                   .kind        = encoding_error::missing_continuation});

    // Overlong encodings. `error_len` is the *maximal subpart* (Unicode 16.0 §3.9),
    // so it stops at the byte that made the sequence unrescuable rather than running
    // to the length the lead byte announced. These values are what Rust's
    // `Utf8Error::error_len` reports for the same bytes.
    static_assert(utf8_error_of("\xC0\xAF") == invalid_encoding{.valid_up_to = 0,
                                                                .error_len   = 1,
                                                                .kind = encoding_error::overlong},
                  "0xC0 can only ever begin an overlong sequence, so it is rejected as a lead");
    static_assert(utf8_error_of("\xC1\xBF") == invalid_encoding{.valid_up_to = 0,
                                                                .error_len   = 1,
                                                                .kind = encoding_error::overlong});
    static_assert(
            utf8_error_of("\xE0\x80\xAF") == invalid_encoding{.valid_up_to = 0,
                                                              .error_len   = 1,
                                                              .kind = encoding_error::overlong},
            "0xE0 followed by 0x80 is already overlong: the verdict does not wait for byte 3");
    static_assert(utf8_error_of("\xE0\x9F\xBF") ==
                          invalid_encoding{.valid_up_to = 0,
                                           .error_len   = 1,
                                           .kind        = encoding_error::overlong},
                  "U+07FF spelled in three bytes is still overlong");
    static_assert(utf8_error_of("\xF0\x80\x80\xAF") ==
                  invalid_encoding{
                          .valid_up_to = 0, .error_len = 1, .kind = encoding_error::overlong});
    static_assert(utf8_error_of("\xF0\x8F\xBF\xBF") ==
                  invalid_encoding{
                          .valid_up_to = 0, .error_len = 1, .kind = encoding_error::overlong});

    // The bytes after a rejected lead resynchronize one at a time, because each of
    // them is an orphaned continuation byte in its own right.
    static_assert(utf8_error_of("\xE0\x80\xAF").valid_up_to +
                          utf8_error_of("\xE0\x80\xAF").error_len ==
                  1);
    static_assert(utf8_error_of(std::string_view{"\xE0\x80\xAF"}.substr(1)) ==
                  invalid_encoding{.valid_up_to = 0,
                                   .error_len   = 1,
                                   .kind        = encoding_error::unexpected_continuation});

    // Beyond U+10FFFF.
    static_assert(utf8_error_of("\xF4\x90\x80\x80") ==
                          invalid_encoding{.valid_up_to = 0,
                                           .error_len   = 1,
                                           .kind        = encoding_error::out_of_range},
                  "U+110000 is one past the last code point, and 0xF4 0x90 already says so");
    static_assert(utf8_error_of("\xF5\x80\x80\x80") ==
                          invalid_encoding{.valid_up_to = 0,
                                           .error_len   = 1,
                                           .kind        = encoding_error::out_of_range},
                  "0xF5 is rejected at the lead byte: it cannot start anything below U+110000");

    // A prefix that no continuation can rescue is never reported as `truncated`.
    // `error_len == 0` promises "more bytes could still help", and these are the
    // cases where that promise would be a lie: E0 80..9F, F0 80..8F, F4 90..BF.
    static_assert(utf8_error_of("\xE0\x80") == invalid_encoding{.valid_up_to = 0,
                                                                .error_len   = 1,
                                                                .kind = encoding_error::overlong});
    static_assert(utf8_error_of("\xF0\x80") == invalid_encoding{.valid_up_to = 0,
                                                                .error_len   = 1,
                                                                .kind = encoding_error::overlong});
    static_assert(utf8_error_of("\xF4\x90") ==
                  invalid_encoding{
                          .valid_up_to = 0, .error_len = 1, .kind = encoding_error::out_of_range});
    static_assert(wtf8_error_of("\xE0\x80").error_len == 1, "the WTF-8 validator agrees");
    static_assert(wtf8_error_of("\xF4\x90").error_len == 1);

    // The prefixes that *are* completable keep reporting `truncated`, and each of
    // them really does have a valid completion.
    static_assert(utf8_error_of("\xE0\xA0").kind == encoding_error::truncated);
    static_assert(utf8_error_of("\xE0\xA0").error_len == 0);
    static_assert(validate_utf8("\xE0\xA0\x80").has_value());
    static_assert(utf8_error_of("\xF0\x90").error_len == 0);
    static_assert(validate_utf8("\xF0\x90\x80\x80").has_value());
    static_assert(utf8_error_of("\xF4\x80").error_len == 0);
    static_assert(validate_utf8("\xF4\x80\x80\x80").has_value());

    // `ED A0`..`ED BF` is the one prefix whose completability depends on the form:
    // WTF-8 can finish it as a lone surrogate, strict UTF-8 can never finish it at
    // all, so only the strict validator is allowed to call it definitively wrong.
    static_assert(wtf8_error_of("\xED\xA0") == invalid_encoding{.valid_up_to = 0,
                                                                .error_len   = 0,
                                                                .kind = encoding_error::truncated});
    static_assert(validate_wtf8("\xED\xA0\x80").has_value());
    static_assert(utf8_error_of("\xED\xA0") == invalid_encoding{.valid_up_to = 0,
                                                                .error_len   = 2,
                                                                .kind = encoding_error::surrogate},
                  "no third byte can make ED A0 valid strict UTF-8, so it is not `truncated`");
    static_assert(utf8_error_of("\xED\x9F").error_len == 0, "ED 9F still completes to U+D7FF");

    // Bytes that are not UTF-8 at all.
    static_assert(utf8_error_of("\xF8") == invalid_encoding{.valid_up_to = 0,
                                                            .error_len   = 1,
                                                            .kind = encoding_error::invalid_lead});
    static_assert(utf8_error_of("\xFF") == invalid_encoding{.valid_up_to = 0,
                                                            .error_len   = 1,
                                                            .kind = encoding_error::invalid_lead});

    // Surrogates: the difference between the two validators.
    static_assert(utf8_error_of(high_surrogate_seq) ==
                  invalid_encoding{
                          .valid_up_to = 0, .error_len = 3, .kind = encoding_error::surrogate});
    static_assert(utf8_error_of(low_surrogate_seq) ==
                  invalid_encoding{
                          .valid_up_to = 0, .error_len = 3, .kind = encoding_error::surrogate});
    static_assert(utf8_error_of("\xED\xBF\xBF") ==
                          invalid_encoding{.valid_up_to = 0,
                                           .error_len   = 3,
                                           .kind        = encoding_error::surrogate},
                  "U+DFFF is the last surrogate");

    // `valid_up_to + error_len` is a resynchronization point.
    static_assert(validate_utf8(std::string_view{"abc\x80"
                                                 "def"}
                                        .substr(utf8_error_of("abc\x80"
                                                              "def")
                                                        .valid_up_to +
                                                utf8_error_of("abc\x80"
                                                              "def")
                                                        .error_len))
                          .has_value());

    // ---------------------------------------------------------------------------
    // WTF-8: everything strict UTF-8 accepts, plus lone surrogates
    // ---------------------------------------------------------------------------

    static_assert(validate_wtf8(empty_text).has_value());
    static_assert(validate_wtf8(mixed_text).has_value());
    static_assert(validate_wtf8(text_with_nul).has_value());
    static_assert(validate_wtf8(max_code_point).has_value());

    static_assert(validate_wtf8(high_surrogate_seq).has_value(),
                  "an unpaired high surrogate is exactly what WTF-8 exists to carry");
    static_assert(validate_wtf8(low_surrogate_seq).has_value());
    static_assert(validate_wtf8(two_high_surrogates).has_value(),
                  "two high surrogates in a row are not a pair");
    static_assert(validate_wtf8("\xED\xB0\x80\xED\xA0\x80").has_value(),
                  "low followed by high is not a pair either");

    // A high/low pair encoded separately is *not* well-formed WTF-8: it has to be
    // stored merged, otherwise WTF-8 <-> UTF-16 stops being one-to-one.
    static_assert(wtf8_error_of(unmerged_pair) ==
                  invalid_encoding{.valid_up_to = 0,
                                   .error_len   = 6,
                                   .kind        = encoding_error::surrogate_pair});
    static_assert(wtf8_error_of(std::string_view{"a\xED\xA0\x80\xED\xB0\x80"}).valid_up_to == 1);

    // WTF-8 is no laxer than UTF-8 anywhere else.
    static_assert(wtf8_error_of("\x80").kind == encoding_error::unexpected_continuation);
    static_assert(wtf8_error_of("\xC0\xAF").kind == encoding_error::overlong);
    static_assert(wtf8_error_of("\xF4\x90\x80\x80").kind == encoding_error::out_of_range);
    static_assert(wtf8_error_of("\xE4\xB8").kind == encoding_error::truncated);

    // ---------------------------------------------------------------------------
    // os_str: size, emptiness, iteration, indexing
    // ---------------------------------------------------------------------------

    static_assert(os_str{}.empty());
    static_assert(os_str{}.size() == 0);
    static_assert(os_str{""}.empty());
    static_assert(os_str{static_cast<const char*>(nullptr)}.empty(),
                  "a null const char* yields an empty os_str rather than undefined behaviour");

    static_assert(os_str{"--flag"}.size() == 6);
    static_assert(!os_str{"--flag"}.empty());
    static_assert(os_str{text_with_nul}.size() == 3,
                  "an embedded NUL does not terminate the value");
    static_assert(os_str{"a"} != os_str{text_with_nul});

    // Both iterators must come from the SAME object: subtracting pointers into two
    // distinct temporaries is undefined behavior, even when the literals are identical.
    // GCC accepts it because it pools equal string literals; clang-p2996 correctly
    // rejects it as a non-constant expression. Caught by the second implementation.
    static_assert([] {
        constexpr os_str s{"abc"};
        return s.end() - s.begin();
    }() == 3);
    static_assert(os_str{"abc"}[1] == 'b');
    static_assert(os_str{"\x80"}.byte_at(0) == 0x80,
                  "byte_at() is unsigned regardless of whether plain char is");
    static_assert(static_cast<unsigned char>(os_str{"\x80"}[0]) == 0x80);

    static_assert([] {
        std::size_t total = 0;
        for (const char byte : os_str{"abc"}) total += static_cast<std::size_t>(byte);
        return total;
    }() == static_cast<std::size_t>('a') + 'b' + 'c');

    // ---------------------------------------------------------------------------
    // os_str: comparison is byte-wise and locale-free
    // ---------------------------------------------------------------------------

    static_assert(os_str{"abc"} == os_str{"abc"});
    static_assert(os_str{"abc"} == "abc",
                  "implicit conversion from a literal keeps call sites short");
    static_assert("abc" == os_str{"abc"});
    static_assert(os_str{"abc"} != os_str{"abd"});
    static_assert(os_str{"abc"} < os_str{"abd"});
    static_assert(os_str{"ab"} < os_str{"abc"});

    // The load-bearing one: 0x80 must sort *after* 0x7F. A hand-rolled loop over
    // `char` would order these the other way round, because char is signed here.
    static_assert(os_str{"\x80"} > os_str{"\x7F"});
    static_assert(os_str{"\xFF"} > os_str{"\x01"});
    static_assert((os_str{"\x80"} <=> os_str{"\x7F"}) == std::strong_ordering::greater);

    // Ordering ignores anything a locale might have to say about accents.
    static_assert(os_str{"Z"} < os_str{"a"}, "ASCII order, not dictionary order");
    static_assert(os_str{two_byte_seq} > os_str{"z"});

    // Embedded NUL participates in comparison instead of ending it.
    static_assert(os_str{std::string_view{"a\0a", 3}} < os_str{std::string_view{"a\0b", 3}});

    // ---------------------------------------------------------------------------
    // os_str: decoding
    // ---------------------------------------------------------------------------

    static_assert(os_str{mixed_text}.is_utf8());
    static_assert(os_str{mixed_text}.is_wtf8());
    static_assert(!os_str{high_surrogate_seq}.is_utf8());
    static_assert(os_str{high_surrogate_seq}.is_wtf8());
    static_assert(!os_str{"\xFF"}.is_utf8());
    static_assert(!os_str{"\xFF"}.is_wtf8());

    static_assert([] {
        const auto decoded = os_str{mixed_text}.to_string_view();
        return decoded.has_value() && decoded.value() == mixed_text;
    }());

    // to_string_view() must hand back the *same* bytes, never a copy.
    static_assert(
            [] {
                const os_str value{mixed_text};
                const auto decoded = value.to_string_view();
                return decoded.has_value() && decoded.value().data() == value.data() &&
                       decoded.value().size() == value.size();
            }(),
            "to_string_view() is zero-copy on valid UTF-8");

    static_assert([] {
        const auto decoded = os_str{"a\xFF"}.to_string_view();
        return !decoded.has_value() && decoded.error().valid_up_to == 1 &&
               decoded.error().kind == encoding_error::invalid_lead;
    }());

    static_assert(
            [] {
                const auto decoded = os_str{high_surrogate_seq}.to_string_view();
                return !decoded.has_value() && decoded.error().kind == encoding_error::surrogate;
            }(),
            "to_string_view() is strict UTF-8, so WTF-8 surrogates do not leak out as text");

    // to_string_lossy() replaces one *maximal subpart* with one U+FFFD, which is what
    // Rust's String::from_utf8_lossy does and therefore what clap prints.
    constexpr std::size_t replacement_count(std::string_view text) {
        constexpr std::string_view fffd = "\xEF\xBF\xBD";
        const std::string rendered      = clapp::detail::lossy_utf8(text);
        const std::string_view view{rendered};
        std::size_t found = 0;
        for (std::size_t i = 0; i + fffd.size() <= view.size(); ++i)
            if (view.substr(i, fffd.size()) == fffd) ++found;
        return found;
    }

    static_assert([] {
        return os_str{"a\x80"
                      "b"}
                       .to_string_lossy() == std::string{"a\xEF\xBF\xBD"
                                                         "b"};
    }());
    static_assert([] { return os_str{mixed_text}.to_string_lossy() == std::string{mixed_text}; }());
    static_assert(
            [] { return clapp::validate_utf8(os_str{"\xFF\xFE"}.to_string_lossy()).has_value(); }(),
            "the lossy rendering is always valid UTF-8");

    // An overlong or out-of-range sequence decomposes: the lead is one error and each
    // orphaned continuation byte is another. Same counts as `String::from_utf8_lossy`.
    static_assert(replacement_count("\xC0\x80") == 2);
    static_assert(replacement_count("\xE0\x80\x80") == 3);
    static_assert(replacement_count("\xE0\x9F\xBF") == 3);
    static_assert(replacement_count("\xF0\x80\x80\x80") == 4);
    static_assert(replacement_count("\xF0\x8F\xBF\xBF") == 4);
    static_assert(replacement_count("\xF4\x90\x80\x80") == 4);
    static_assert(replacement_count("\xE0\x80/") == 2, "a following ASCII byte is not swallowed");
    static_assert([] {
        return os_str{"-a\xE0\x80\x80"
                      "b"}
                       .to_string_lossy() == std::string{"-a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"
                                                         "b"};
    }());

    // The one deliberate divergence from a byte-oriented reader: an encoded surrogate
    // is a well-formed WTF-8 *sequence* here, so it renders as a single U+FFFD where
    // Rust's from_utf8_lossy would emit three.
    static_assert(
            [] {
                return os_str{high_surrogate_seq}.to_string_lossy() == std::string{"\xEF\xBF\xBD"};
            }(),
            "one replacement character per WTF-8 surrogate, because clapp stores WTF-8");
    static_assert(replacement_count(unmerged_pair) == 2);

    // No byte below 0x80 can be swallowed by a rejected sequence, so a lossy rendering
    // can never lose a `/` or a NUL and smuggle a different path past a filter.
    static_assert([] {
        return clapp::detail::lossy_utf8(std::string_view{"\xF0\x80\x80\0", 4}) ==
               std::string{"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\0", 10};
    }());

    // ---------------------------------------------------------------------------
    // os_str: byte-wise string operations (clap_lex's OsStrExt)
    // ---------------------------------------------------------------------------

    static_assert(os_str{"--flag"}.starts_with("--"));
    static_assert(os_str{"--flag"}.starts_with('-'));
    static_assert(!os_str{"-x"}.starts_with("--"));
    static_assert(!os_str{""}.starts_with('-'));
    static_assert(os_str{"file.txt"}.ends_with(".txt"));
    static_assert(os_str{"file.txt"}.ends_with('t'));

    static_assert(os_str{"bananas"}.contains("nana"));
    static_assert(!os_str{"bananas"}.contains("apples"));
    static_assert(os_str{"bananas"}.contains('b'));

    static_assert(os_str{"cfg=foo"}.find('=').value() == 3);
    static_assert(!os_str{"cfg"}.find('=').has_value());
    static_assert(os_str{"a=b=c"}.find('=', 2).value() == 3, "find() honours the start offset");
    static_assert(os_str{"lion::tiger"}.find("::").value() == 4);

    static_assert(os_str{"foo:bar"}.strip_prefix("foo:").value() == os_str{"bar"});
    static_assert(!os_str{"foo:bar"}.strip_prefix("bar").has_value());
    static_assert(os_str{"foofoo"}.strip_prefix("foo").value() == os_str{"foo"});
    static_assert(os_str{"file.txt"}.strip_suffix(".txt").value() == os_str{"file"});

    // `--key=value` splitting, the reason split_once() exists.
    static_assert(head_of("--key=value", '=') == os_str{"--key"});
    static_assert(tail_of("--key=value", '=') == os_str{"value"});
    static_assert(head_of("--key=", '=') == os_str{"--key"});
    static_assert(tail_of("--key=", '=').empty());
    static_assert(tail_of("cfg=foo=bar", '=') == os_str{"foo=bar"},
                  "split on the *first* separator");
    static_assert(!os_str{"cfg"}.split_once('=').has_value());

    // ---------------------------------------------------------------------------
    // split() — the value-delimiter machinery, ported case for case from the doc
    // comments on clap_lex's OsStrExt::split (clap_lex/src/ext.rs:82-168).
    // ---------------------------------------------------------------------------

    /** How many pieces \p subject splits into. */
    constexpr std::size_t piece_count(os_str subject, os_str separator) {
        std::size_t pieces = 0;
        for (const os_str piece : subject.split(separator)) {
            (void)piece;
            ++pieces;
        }
        return pieces;
    }

    /** Piece number \p index, or a marker no test expects. */
    constexpr os_str piece_at(os_str subject, os_str separator, std::size_t index) {
        for (const os_str piece : subject.split(separator)) {
            if (index == 0) return piece;
            --index;
        }
        return os_str{"<out of range>"};
    }

    /** Whether \p subject splits into exactly \p expected, in order. */
    constexpr bool
    splits_into(os_str subject, os_str separator, std::initializer_list<os_str> expected) {
        return std::ranges::equal(subject.split(separator), expected);
    }

    static_assert(splits_into("Mary had a little lamb",
                              " ",
                              {"Mary", "had", "a", "little", "lamb"}));
    static_assert(splits_into("lion::tiger::leopard", "::", {"lion", "tiger", "leopard"}));

    // Contiguous separators leave empty pieces between them, and separators at either
    // end leave empty pieces there. There is always exactly one more piece than there
    // were separators — that invariant is what makes `--paths=a,,b` three values.
    static_assert(splits_into("lionXXtigerXleopard", "X", {"lion", "", "tiger", "leopard"}));
    static_assert(splits_into("(///)", "/", {"(", "", "", ")"}));
    static_assert(splits_into("010", "0", {"", "1", ""}));
    static_assert(piece_count("||||a||b|c", "|") == 8);
    static_assert(piece_at("||||a||b|c", "|", 4) == os_str{"a"});

    // The case `std::views::split` gets differently, and the reason os_str_split_view
    // exists: an empty subject is one empty piece, not zero pieces.
    static_assert(splits_into("", "X", {""}));
    static_assert(piece_count("", "X") == 1);

    // A separator that does not occur, or cannot fit, yields the whole subject.
    static_assert(splits_into("cfg", "=", {"cfg"}));
    static_assert(splits_into("a", "aa", {"a"}));
    static_assert(splits_into("abc", "abc", {"", ""}));

    // Rust asserts on an empty separator; clapp has no exceptions, so it degrades to
    // "the separator never matches" rather than looping forever.
    static_assert(splits_into("rust", "", {"rust"}));

    // Bytes that are not text split exactly like bytes that are — the whole reason
    // the delimiter cannot go through to_string_view().
    static_assert(splits_into("/tmp/a,/tmp/\xFF"
                              "b",
                              ",",
                              {"/tmp/a",
                               "/tmp/\xFF"
                               "b"}));
    static_assert(!piece_at("/tmp/a,/tmp/\xFF"
                            "b",
                            ",",
                            1)
                           .is_utf8());

    // The view is a value: cheap to copy, re-iterable, and never empty.
    static_assert(std::ranges::forward_range<clapp::os_str_split_view>);
    static_assert(std::ranges::view<clapp::os_str_split_view>);
    static_assert(
            [] {
                const clapp::os_str_split_view pieces = os_str{"a,b"}.split(",");
                return !pieces.empty() && pieces.front() == os_str{"a"} &&
                       piece_count(pieces.subject(), pieces.separator()) == 2;
            }(),
            "walking the view twice gives the same answer both times");

    // split() and split_once() agree on the first piece whenever the separator occurs.
    static_assert(piece_at("cfg=foo=bar", "=", 0) == head_of("cfg=foo=bar", '='));

    // split_at() is what peels one flag byte off a `-abc` cluster.
    static_assert(os_str{"-abc"}.split_at(1).first == os_str{"-"});
    static_assert(os_str{"-abc"}.split_at(1).second == os_str{"abc"});
    static_assert(os_str{"-abc"}.split_at(0).first.empty());
    static_assert(os_str{"-abc"}.split_at(99).second.empty(), "an out-of-range index is clamped");

    // Cutting a multi-byte sequence in half stays well-defined; only the halves stop
    // being text, which to_string_view() then reports.
    static_assert(os_str{three_byte_seq}.split_at(1).first.size() == 1);
    static_assert(!os_str{three_byte_seq}.split_at(1).second.is_utf8());

    static_assert(os_str{"abcdef"}.substr(2) == os_str{"cdef"});
    static_assert(os_str{"abcdef"}.substr(2, 3) == os_str{"cde"});
    static_assert(os_str{"abcdef"}.substr(99).empty(), "substr() clamps instead of throwing");
    static_assert(os_str{"abcdef"}.substr(2, os_str::npos) == os_str{"cdef"});

    // ---------------------------------------------------------------------------
    // WTF-8 <-> UTF-16
    //
    // The conversion is exercised through `char16_t`, which is bit-for-bit the same
    // path Windows takes with `wchar_t`; only the instantiation differs. See the
    // notes at the end of this file.
    // ---------------------------------------------------------------------------

    constexpr std::array<char16_t, 4> pair_utf16{u'a', 0xD83D, 0xDCA9, u'b'};  // U+1F4A9
    constexpr std::array<char16_t, 1> lone_high_utf16{0xD800};
    constexpr std::array<char16_t, 1> lone_low_utf16{0xDFFF};
    constexpr std::array<char16_t, 3> lone_high_in_text{u'a', 0xD800, u'b'};
    constexpr std::array<char16_t, 2> two_highs_utf16{0xD800, 0xD800};

    static_assert(
            [] {
                return clapp::detail::utf16_to_wtf8<char16_t>(as_utf16(pair_utf16)) ==
                       std::string{"a\xF0\x9F\x92\xA9"
                                   "b"};
            }(),
            "a surrogate pair is merged into the four-byte form");

    static_assert(
            [] {
                return clapp::detail::utf16_to_wtf8<char16_t>(as_utf16(lone_high_utf16)) ==
                       std::string{high_surrogate_seq};
            }(),
            "a lone surrogate survives as the three-byte WTF-8 form");

    static_assert([] {
        return clapp::detail::utf16_to_wtf8<char16_t>(as_utf16(lone_low_utf16)) ==
               std::string{"\xED\xBF\xBF"};
    }());

    static_assert([] {
        return clapp::detail::wtf8_to_utf16<char16_t>("a\xF0\x9F\x92\xA9"
                                                      "b") == std::u16string{as_utf16(pair_utf16)};
    }());

    static_assert([] {
        return clapp::detail::wtf8_to_utf16<char16_t>(high_surrogate_seq) ==
               std::u16string{as_utf16(lone_high_utf16)};
    }());

    // Round-trip, in both directions, for every awkward shape.
    constexpr bool utf16_round_trips(std::u16string_view units) {
        return clapp::detail::wtf8_to_utf16<char16_t>(
                       clapp::detail::utf16_to_wtf8<char16_t>(units)) == std::u16string{units};
    }

    static_assert(utf16_round_trips(as_utf16(pair_utf16)));
    static_assert(utf16_round_trips(as_utf16(lone_high_utf16)));
    static_assert(utf16_round_trips(as_utf16(lone_low_utf16)));
    static_assert(utf16_round_trips(as_utf16(lone_high_in_text)));
    static_assert(utf16_round_trips(as_utf16(two_highs_utf16)));
    static_assert(utf16_round_trips(std::u16string_view{}));

    constexpr bool wtf8_round_trips(std::string_view text) {
        return clapp::detail::utf16_to_wtf8<char16_t>(
                       clapp::detail::wtf8_to_utf16<char16_t>(text)) == std::string{text};
    }

    static_assert(wtf8_round_trips(mixed_text));
    static_assert(wtf8_round_trips(high_surrogate_seq));
    static_assert(wtf8_round_trips(max_code_point));
    static_assert(wtf8_round_trips(text_with_nul));
    static_assert(wtf8_round_trips(two_high_surrogates));

    // Whatever comes out of UTF-16 is always well-formed WTF-8 — that is what makes
    // the `surrogate_pair` rule safe to enforce.
    static_assert(validate_wtf8(clapp::detail::utf16_to_wtf8<char16_t>(as_utf16(pair_utf16)))
                          .has_value());
    static_assert(validate_wtf8(clapp::detail::utf16_to_wtf8<char16_t>(as_utf16(lone_high_in_text)))
                          .has_value());
    static_assert(validate_wtf8(clapp::detail::utf16_to_wtf8<char16_t>(as_utf16(two_highs_utf16)))
                          .has_value());

    // Invalid input cannot make the decoder diverge or read out of bounds; it
    // degrades to U+FFFD instead.
    static_assert([] {
        const std::u16string decoded = clapp::detail::wtf8_to_utf16<char16_t>("a\xFF\xFE"
                                                                              "b");
        return decoded.size() == 4 && decoded[0] == u'a' && decoded[1] == 0xFFFD &&
               decoded[2] == 0xFFFD && decoded[3] == u'b';
    }());

    // ---------------------------------------------------------------------------
    // os_string
    // ---------------------------------------------------------------------------

    static_assert(os_string{}.empty());
    static_assert(os_string{"abc"}.size() == 3);
    static_assert(os_string{static_cast<const char*>(nullptr)}.empty());
    static_assert([] { return os_string{text_with_nul}.size() == 3; }());
    static_assert([] { return os_string{os_str{"abc"}} == os_str{"abc"}; }(),
                  "os_string compares through its implicit conversion to os_str");
    static_assert([] { return os_string{"abc"} == os_string{"abc"}; }());
    static_assert([] { return os_string{"abc"} < os_string{"abd"}; }());
    static_assert([] { return os_string{"abc"}.view() == os_str{"abc"}; }());
    static_assert([] { return os_string{high_surrogate_seq}.is_wtf8(); }());
    static_assert([] { return !os_string{high_surrogate_seq}.is_utf8(); }());

    // An os_string keeps its bytes after the buffer they were copied from is gone,
    // and release() hands them over intact. Both are decidable at compile time: the
    // string is created and destroyed inside a single evaluation.
    constexpr bool os_string_owns_its_bytes() {
        os_string owned;
        {
            std::string scratch{"--output=file.txt"};
            owned = os_string{scratch};
            scratch.clear();
        }
        if (owned != os_str{"--output=file.txt"}) return false;
        const auto split = owned.view().split_once('=');
        if (!split.has_value() || split.value().second != os_str{"file.txt"}) return false;
        return std::move(owned).release() == "--output=file.txt";
    }

    static_assert(os_string_owns_its_bytes());

    // from_native() and to_native() are inverses on this platform, for text, for a
    // lone surrogate, and for bytes that are neither.
    constexpr bool native_round_trips(std::string_view bytes) {
        const os_string owned{bytes};
        return os_string::from_native(owned.to_native()) == owned;
    }

    static_assert(native_round_trips(mixed_text));
    static_assert(native_round_trips(high_surrogate_seq));
    static_assert(native_round_trips(std::string_view{"\xFF\x00\xFE", 3}));
    static_assert(native_round_trips(empty_text));

#ifndef _WIN32
    // POSIX: os_string <-> native is a byte-for-byte copy, including NUL and
    // including bytes that are not valid UTF-8.
    static_assert([] {
        return os_string::from_native(text_with_nul).view() == os_str{text_with_nul};
    }());
    static_assert([] { return os_string::from_native("\xFF\xFE").view() == os_str{"\xFF\xFE"}; }());
    static_assert([] { return os_str{text_with_nul}.to_native() == std::string(text_with_nul); }());
    static_assert([] { return os_str{"\xFF"}.to_native() == std::string("\xFF"); }());

    // from_native() makes no WTF-8 promise here: `argv` is whatever execve was given,
    // so is_wtf8() is the caller's job and not an invariant.
    static_assert(!os_string::from_native("\xFF").is_wtf8());
    static_assert(!os_string::from_native("\xC0\x80").is_wtf8());
    static_assert(os_string::from_native(high_surrogate_seq).is_wtf8(),
                  "well-formed bytes in still means well-formed bytes out");
#endif

    // ---------------------------------------------------------------------------
    // Diagnostics
    // ---------------------------------------------------------------------------

    static_assert(!invalid_encoding{}.message().empty());
    static_assert(clapp::describe(encoding_error::surrogate) !=
                  clapp::describe(encoding_error::surrogate_pair));
    static_assert(utf8_error_of("\xC0\xAF").message() == clapp::describe(encoding_error::overlong));

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
//
// These either report a compile-time conclusion or exercise something that
// cannot be constant-evaluated.
// ---------------------------------------------------------------------------

CLAPP_TEST("UTF-8 and WTF-8 validators agree with the compile-time expectations") {
    CLAPP_CHECK(validate_utf8(mixed_text).has_value());
    CLAPP_CHECK(!validate_utf8(high_surrogate_seq).has_value());
    CLAPP_CHECK(validate_wtf8(high_surrogate_seq).has_value());
    CLAPP_CHECK(!validate_wtf8(unmerged_pair).has_value());
}

CLAPP_TEST("bytes() exposes the raw bytes (runtime only: needs reinterpret_cast)") {
    const os_str value{mixed_text};
    const std::span<const std::byte> raw = value.bytes();

    CLAPP_CHECK(raw.size() == mixed_text.size());
    CLAPP_CHECK(static_cast<unsigned char>(raw[1]) == 0xC2);
    CLAPP_CHECK(static_cast<const void*>(raw.data()) == static_cast<const void*>(value.data()));
}

CLAPP_TEST("from_bytes() round-trips through bytes(), embedded NUL included") {
    const os_string owned{text_with_nul};
    const os_str restored = os_str::from_bytes(owned.bytes());

    CLAPP_CHECK(restored == owned);
    CLAPP_CHECK(restored.size() == 3);
    CLAPP_CHECK(os_string::from_bytes(owned.bytes()) == owned);
}

CLAPP_TEST("os_string owns its bytes, outlives the source buffer, and releases them") {
    CLAPP_CHECK(os_string_owns_its_bytes());
}

CLAPP_TEST("native conversion round-trips on this platform") {
    CLAPP_CHECK(native_round_trips(mixed_text));
    CLAPP_CHECK(native_round_trips(high_surrogate_seq));
    CLAPP_CHECK(native_round_trips(std::string_view{"\xFF\x00\xFE", 3}));
}

CLAPP_TEST("std::hash agrees with byte-wise equality") {
    const os_string owned{text_with_nul};
    CLAPP_CHECK(std::hash<os_str>{}(owned.view()) == std::hash<os_string>{}(owned));
    CLAPP_CHECK(std::hash<os_str>{}(os_str{"abc"}) == std::hash<os_str>{}(os_str{"abc"}));
    CLAPP_CHECK(std::hash<os_str>{}(os_str{"abc"}) != std::hash<os_str>{}(os_str{"abd"}));
}

CLAPP_TEST("to_string_lossy() renders undecodable arguments for diagnostics") {
    CLAPP_CHECK(os_str{"--path=\xFF\xFE"}.to_string_lossy() ==
                std::string{"--path=\xEF\xBF\xBD\xEF\xBF\xBD"});
    CLAPP_CHECK(validate_utf8(os_str{unmerged_pair}.to_string_lossy()).has_value());
    CLAPP_CHECK(replacement_count("\xE0\x80\x80") == 3);
}

CLAPP_TEST("split() feeds the value delimiter, including on bytes that are not text") {
    CLAPP_CHECK(splits_into("/tmp/a,/tmp/\xFF"
                            "b",
                            ",",
                            {"/tmp/a",
                             "/tmp/\xFF"
                             "b"}));
    CLAPP_CHECK(splits_into("", ",", {""}));
    // HISTORY, AND WHY BOTH FORMS ARE HERE NOW.
    //
    // This used to be a workaround with a warning attached: passing string literals
    // straight into `piece_count` and storing the result in a variable was said to make
    // GCC 16.1.0 return 2 instead of 4, while the `static_assert` above returned 4 — one
    // function disagreeing with itself across the consteval boundary.
    //
    // **Re-measured 2026-08-10 and it no longer reproduces.** The direct form was built
    // with `g++-16 -std=c++26 -freflection` at -O0, -O1, -O2, -O3, -Os, and at -O2/-O3
    // with -DNDEBUG: seven builds, seven times `8 passed, 0 failed`.
    //
    // Both spellings stay, and the direct one is FIRST, because it is the one that would
    // fail if the defect came back — keeping only the bound form would leave the
    // revocation unguarded. The bound form stays as the regression's own witness.
    const std::size_t direct_form = piece_count("a,b,,c", ",");
    CLAPP_CHECK(direct_form == 4);

    const os_str subject   = "a,b,,c";
    const os_str separator = ",";
    CLAPP_CHECK(piece_count(subject, separator) == 4);
}
