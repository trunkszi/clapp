#include <clapp/util/str.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::naming;
    using clapp::word_cursor;
    using clapp::word_span;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // Helpers
    //
    // rename() hands back an owning std::string, which is a transient allocation in
    // a constant expression: it can be compared inside consteval but cannot escape
    // as a constexpr variable. Each helper therefore does the whole comparison in
    // one consteval call and returns only a bool.
    // ---------------------------------------------------------------------------

    consteval bool renames(std::string_view id, naming style, std::string_view want) {
        return clapp::rename(id, style) == want && clapp::rename_size(id, style) == want.size();
    }

    consteval bool splits(std::string_view id, std::span<const std::string_view> want) {
        const std::vector<std::string_view> got = clapp::words(id);
        return std::ranges::equal(got, want);
    }

    consteval bool suggests(std::string_view input,
                            std::span<const std::string_view> candidates,
                            std::span<const std::string_view> want) {
        return std::ranges::equal(clapp::did_you_mean(input, candidates), want);
    }

    // ---------------------------------------------------------------------------
    // Word segmentation
    // ---------------------------------------------------------------------------

    // Segmentation only *finds* the boundaries; re-casing happens in rename(), so the
    // expected words keep the spelling they had in the identifier.
    constexpr std::array snake_words{"output"sv, "file"sv};
    constexpr std::array camel_words{"output"sv, "File"sv};
    constexpr std::array acronym_words{"HTTP"sv, "Server"sv};
    constexpr std::array xml_words{"XML"sv, "Http"sv, "Request"sv};
    constexpr std::array one_word{"auto"sv};
    constexpr std::array digit_words{"Foo2"sv, "Bar"sv};

    static_assert(splits("output_file", snake_words));
    static_assert(splits("outputFile", camel_words));
    static_assert(splits("OutputFile", {std::array{"Output"sv, "File"sv}}));
    static_assert(splits("HTTPServer", acronym_words));
    static_assert(splits("XMLHttpRequest", xml_words));
    static_assert(splits("auto_", one_word));
    static_assert(splits("_auto", one_word));
    static_assert(splits("__auto__", one_word));
    static_assert(splits("Foo2Bar", digit_words));
    static_assert(splits("", {}));
    static_assert(splits("___", {}));

    // The cursor's sentinel is an empty span, never std::nullopt — see the note in
    // the header about optional::operator* under _GLIBCXX_ASSERTIONS.
    consteval bool cursor_terminates() {
        word_cursor cur{"aB"};
        return !cur.next().empty() && !cur.next().empty() && cur.next().empty() &&
               cur.next().empty() && cur.exhausted();
    }
    static_assert(cursor_terminates());

    // Spans are offsets into the original text, which is what makes
    // strip_type_affixes able to return a subview.
    consteval bool spans_are_offsets() {
        word_cursor cur{"cmd_add"};
        const word_span first  = cur.next();
        const word_span second = cur.next();
        return first == word_span{.begin = 0, .end = 3} &&
               second == word_span{.begin = 4, .end = 7} && second.of("cmd_add") == "add"sv &&
               second.size() == 3;
    }
    static_assert(spans_are_offsets());

    static_assert(word_span{}.empty());
    static_assert(word_span{.begin = 5, .end = 5}.empty());
    static_assert(word_span{.begin = 5, .end = 5}.size() == 0);

    // ---------------------------------------------------------------------------
    // rename() — the whole clapp::naming matrix
    //
    // The rows come straight from the enumerator documentation in
    // include/clapp/meta/annotations.hpp.
    // ---------------------------------------------------------------------------

    static_assert(renames("output_file", naming::kebab, "output-file"));
    static_assert(renames("output_file", naming::snake, "output_file"));
    static_assert(renames("output_file", naming::camel, "outputFile"));
    static_assert(renames("output_file", naming::pascal, "OutputFile"));
    static_assert(renames("output_file", naming::screaming_snake, "OUTPUT_FILE"));
    static_assert(renames("output_file", naming::lower, "outputfile"));
    static_assert(renames("output_file", naming::upper, "OUTPUTFILE"));
    static_assert(renames("output_file", naming::verbatim, "output_file"));

    // camelCase input normalises to the same names as snake_case input.
    static_assert(renames("outputFile", naming::kebab, "output-file"));
    static_assert(renames("outputFile", naming::snake, "output_file"));
    static_assert(renames("outputFile", naming::screaming_snake, "OUTPUT_FILE"));
    static_assert(renames("OutputFile", naming::kebab, "output-file"));
    static_assert(renames("output-file", naming::kebab, "output-file"));

    // Single words and three-word names.
    static_assert(renames("config", naming::kebab, "config"));
    static_assert(renames("config", naming::screaming_snake, "CONFIG"));
    static_assert(renames("config", naming::pascal, "Config"));
    static_assert(renames("dry_run_only", naming::kebab, "dry-run-only"));
    static_assert(renames("dry_run_only", naming::camel, "dryRunOnly"));

    // ---------------------------------------------------------------------------
    // The acronym rule: the last capital of a run starts the next word.
    // Documented on word_cursor; `h-t-t-p-server` is the reading being rejected.
    // ---------------------------------------------------------------------------

    static_assert(renames("HTTPServer", naming::kebab, "http-server"));
    static_assert(renames("HTTPServer", naming::snake, "http_server"));
    static_assert(renames("HTTPServer", naming::pascal, "HttpServer"));
    static_assert(renames("HTTPServer", naming::camel, "httpServer"));
    static_assert(renames("HTTPServer", naming::screaming_snake, "HTTP_SERVER"));
    static_assert(renames("XMLHttpRequest", naming::kebab, "xml-http-request"));
    static_assert(renames("parseHTML", naming::kebab, "parse-html"));
    static_assert(renames("IOError", naming::kebab, "io-error"));
    static_assert(renames("HTTP", naming::kebab, "http"));

    // ---------------------------------------------------------------------------
    // Keyword-avoidance underscores, stripped for every style including verbatim.
    // C++ writes `auto_` where Rust writes `r#auto`; clap strips the latter with
    // Ident::unraw() before casing, so this is the faithful port, not a divergence.
    // ---------------------------------------------------------------------------

    static_assert(renames("auto_", naming::kebab, "auto"));
    static_assert(renames("auto_", naming::verbatim, "auto"));
    static_assert(renames("short_", naming::kebab, "short"));
    static_assert(renames("long_", naming::screaming_snake, "LONG"));
    static_assert(renames("requires_", naming::snake, "requires"));
    static_assert(renames("value__", naming::verbatim, "value"));

    // A leading underscore is *not* keyword avoidance, so verbatim keeps it while
    // the segmenting styles drop it with every other separator.
    static_assert(renames("_private", naming::verbatim, "_private"));
    static_assert(renames("_private", naming::kebab, "private"));

    // Degenerate input.
    static_assert(renames("", naming::kebab, ""));
    static_assert(renames("___", naming::kebab, ""));
    static_assert(renames("___", naming::verbatim, ""));
    static_assert(clapp::rename_size("", naming::kebab) == 0);

    // Non-ASCII bytes are caseless and stay in one piece.
    static_assert(renames("caf\xC3\xA9_mode", naming::kebab, "caf\xC3\xA9-mode"));

    // ---------------------------------------------------------------------------
    // rename_initial() — clap's Name::translate_char
    // ---------------------------------------------------------------------------

    static_assert(clapp::rename_initial("verbose", naming::kebab) == 'v');
    static_assert(clapp::rename_initial("outputFile", naming::kebab) == 'o');
    static_assert(clapp::rename_initial("OutputFile", naming::kebab) == 'o');
    static_assert(clapp::rename_initial("output_file", naming::pascal) == 'O');
    static_assert(clapp::rename_initial("output_file", naming::screaming_snake) == 'O');
    static_assert(clapp::rename_initial("_private", naming::verbatim) == '_');
    static_assert(clapp::rename_initial("", naming::kebab) == '\0');
    static_assert(clapp::rename_initial("___", naming::kebab) == '\0');

    // ---------------------------------------------------------------------------
    // strip_type_affixes() — exactly three rules, not configurable
    // ---------------------------------------------------------------------------

    static_assert(clapp::strip_type_affixes("cmd_add") == "add"sv);
    static_assert(clapp::strip_type_affixes("add_cmd") == "add"sv);
    static_assert(clapp::strip_type_affixes("add_args") == "add"sv);
    static_assert(clapp::strip_type_affixes("cmd_add_args") == "add"sv);
    static_assert(clapp::strip_type_affixes("cmd_add_file") == "add_file"sv);
    static_assert(clapp::strip_type_affixes("add_file_args") == "add_file"sv);

    // Whole-word, ASCII-case-insensitive matching, so PascalCase behaves the same.
    static_assert(clapp::strip_type_affixes("CmdAdd") == "Add"sv);
    static_assert(clapp::strip_type_affixes("AddCmd") == "Add"sv);
    static_assert(clapp::strip_type_affixes("AddArgs") == "Add"sv);
    static_assert(clapp::strip_type_affixes("CMD_add") == "add"sv);

    // Not an affix: only a whole word counts.
    static_assert(clapp::strip_type_affixes("commands_list") == "commands_list"sv);
    static_assert(clapp::strip_type_affixes("cmdadd") == "cmdadd"sv);
    static_assert(clapp::strip_type_affixes("arguments") == "arguments"sv);

    // Nothing may be stripped down to nothing.
    static_assert(clapp::strip_type_affixes("cmd") == "cmd"sv);
    static_assert(clapp::strip_type_affixes("args") == "args"sv);
    static_assert(clapp::strip_type_affixes("cmd_args") == "cmd_args"sv);
    static_assert(clapp::strip_type_affixes("") == ""sv);

    // The result is a subview of the argument, never a copy.
    consteval bool strip_returns_subview() {
        constexpr std::string_view whole = "cmd_add";
        const std::string_view part      = clapp::strip_type_affixes(whole);
        return part.data() == whole.data() + 4 && part.size() == 3;
    }
    static_assert(strip_returns_subview());

    // ---------------------------------------------------------------------------
    // subcommand_name() — strip then rename, the composition command_of<T>() uses
    // ---------------------------------------------------------------------------

    consteval bool subcommand_is(std::string_view type_name, std::string_view want) {
        return clapp::subcommand_name(type_name) == want;
    }

    static_assert(subcommand_is("cmd_add", "add"));
    static_assert(subcommand_is("cmd_commit", "commit"));
    static_assert(subcommand_is("cmd_push", "push"));
    static_assert(subcommand_is("AddArgs", "add"));
    static_assert(subcommand_is("cmd_add_file", "add-file"));
    static_assert(subcommand_is("StashPushCmd", "stash-push"));

    // ---------------------------------------------------------------------------
    // Damerau-Levenshtein
    // ---------------------------------------------------------------------------

    static_assert(clapp::damerau_levenshtein("", "") == 0);
    static_assert(clapp::damerau_levenshtein("", "abc") == 3);
    static_assert(clapp::damerau_levenshtein("abc", "") == 3);
    static_assert(clapp::damerau_levenshtein("abc", "abc") == 0);
    static_assert(clapp::damerau_levenshtein("kitten", "sitting") == 3);
    static_assert(clapp::damerau_levenshtein("ab", "ba") == 1);  // one transposition
    static_assert(clapp::damerau_levenshtein("verbose", "verbsoe") == 1);
    static_assert(clapp::damerau_levenshtein("tst", "test") == 1);

    // Symmetric, as an edit distance must be.
    static_assert(clapp::damerau_levenshtein("verbose", "verbse") ==
                  clapp::damerau_levenshtein("verbse", "verbose"));

    // The unrestricted variant, not optimal string alignment: OSA reports 3 here
    // because it forbids editing a substring that has already been transposed.
    static_assert(clapp::damerau_levenshtein("ca", "abc") == 2);

    // ---------------------------------------------------------------------------
    // Jaro similarity — the metric clap actually uses, via strsim
    // ---------------------------------------------------------------------------

    static_assert(clapp::jaro("", "") == 1.0);
    static_assert(clapp::jaro("", "abc") == 0.0);
    static_assert(clapp::jaro("abc", "") == 0.0);
    static_assert(clapp::jaro("abc", "abc") == 1.0);
    static_assert(clapp::jaro("abc", "xyz") == 0.0);

    // "te" against "test" and "temp" score identically: 2 matches out of 2 and 4.
    static_assert(clapp::jaro("te", "test") == clapp::jaro("te", "temp"));
    static_assert(clapp::jaro("te", "test") > clapp::did_you_mean_threshold);

    // clap#4660: Jaro, unlike strsim's jaro_winkler, still separates two candidates
    // that share a long prefix.
    static_assert(clapp::jaro("alignmentScorr", "alignmentScore") >
                  clapp::jaro("alignmentScorr", "alignmentStart"));
    static_assert(clapp::jaro("hahaahahah", "test") <= clapp::did_you_mean_threshold);

    // The threshold is clap's, and the comparison against it is strict.
    static_assert(clapp::did_you_mean_threshold == 0.7);

    // ---------------------------------------------------------------------------
    // Jaro — bit-exact agreement with strsim 0.11.1
    //
    // The cases below are the ones a paraphrase of Jaro gets wrong. Two rules are
    // easy to reinvent incorrectly, and clapp reinvented both:
    //
    //   1. TRANSPOSITIONS. strsim zips the two matched subsequences, counts the
    //      positions whose elements differ, and integer-divides that count by two.
    //      clapp used to add one for every match landing at a lower index in `b`
    //      than the previous match did — a rule that is neither strsim's nor the
    //      textbook's, and that errs in *both* directions (see the two `bbaab`
    //      lines below).
    //   2. COMPARISON UNIT. clap calls `strsim::jaro(&str, &str)`, which iterates
    //      `char`. clapp used to iterate bytes, which widens the match window by
    //      the UTF-8 expansion factor and lets shared continuation bytes count as
    //      matches.
    //
    // Verified by differential testing against strsim 0.11.1 — the dependency version
    // pinned by clap at the time — over 15,229 generated pairs
    // (random over three alphabets, 1–3 successive single edits, adjacent
    // transpositions, repeated characters, lopsided lengths, CJK / Arabic / Greek
    // / emoji). Before: 1,666 divergent. After: 0. The `(m, la, lb, t)` tuples
    // spelled out below were read out of that run.
    // ---------------------------------------------------------------------------

    /**
     * \brief Jaro assembled from its four ingredients, in strsim's operation order.
     *
     * Spelling the arithmetic out rather than writing a decimal literal buys two
     * things: the comparison is bit-exact instead of `fabs(...) < eps`, and the
     * expected `matches` / `transpositions` counts are stated explicitly, so a
     * regression names which of the two rules broke.
     */
    consteval double
    jaro_from(std::size_t matches, std::size_t la, std::size_t lb, std::size_t transpositions) {
        const double m = static_cast<double>(matches);
        return (m / static_cast<double>(la) + m / static_cast<double>(lb) +
                (m - static_cast<double>(transpositions)) / m) /
               3.0;
    }

    // --- Rule 1: the transposition count. --------------------------------------

    // The canonical Jaro example. Both rules happen to agree here, so it is a
    // control, not a regression case.
    static_assert(clapp::jaro("martha", "marhta") == jaro_from(6, 6, 6, 1));
    static_assert(clapp::jaro("dixon", "dicksonx") == jaro_from(4, 5, 8, 0));
    static_assert(clapp::jaro("dwayne", "duane") == jaro_from(4, 6, 5, 0));

    // Pins the halving itself: all four matched positions disagree, and that is
    // FOUR mismatches reported as TWO transpositions. Drop the `/= 2` and this
    // becomes jaro_from(4, 4, 4, 4) == 0.666667.
    static_assert(clapp::jaro("abab", "baba") == jaro_from(4, 4, 4, 2));

    // The old rule scored these too LOW.
    static_assert(clapp::jaro("cba", "abcbabb") == jaro_from(3, 3, 7, 1));    // was 0.587302
    static_assert(clapp::jaro("baba", "aabbaabb") == jaro_from(4, 4, 8, 1));  // was 0.666667

    // ...and this one too HIGH, which is why "count the backward steps" cannot be
    // rescued by scaling. A rule that errs in one direction can be a threshold
    // choice; one that errs in both is simply a different function.
    static_assert(clapp::jaro("bbaab", "aabbaaba") == jaro_from(5, 5, 8, 2));  // was 0.808333

    // Real subcommand pairs whose score straddles the 0.7 threshold, so the old
    // rule did not merely rank differently — it suppressed a suggestion clap makes.
    // Confirmed end to end against clap itself: a `Command` carrying a `remote`
    // subcommand, given `git merge`, prints
    // "tip: a similar subcommand exists: 'remote'".
    static_assert(clapp::jaro("merge", "remote") == jaro_from(4, 5, 6, 1));  // was 0.655556
    static_assert(clapp::jaro("merge", "remote") > clapp::did_you_mean_threshold);
    static_assert(clapp::jaro("verbose", "revolve") == jaro_from(5, 7, 7, 1));  // was 0.676190
    static_assert(clapp::jaro("verbose", "revolve") > clapp::did_you_mean_threshold);

    // --- Rule 2: the comparison unit is a code point, not a byte. --------------

    // One accented letter differing is one unit differing, whatever it costs in
    // UTF-8. Byte comparison scored these 0.783333 and 0.822222.
    static_assert(clapp::jaro("café", "cafe") == jaro_from(3, 4, 4, 0));
    static_assert(clapp::jaro("naïve", "naive") == jaro_from(4, 5, 5, 0));

    // A two-character CJK string has a match window of `2 / 2 - 1 == 0`, so only
    // aligned positions may match. Byte comparison saw six bytes, a window of two,
    // and scored this 0.777778 — over the threshold, i.e. a suggestion clap never
    // makes.
    static_assert(clapp::jaro("文中", "测中") == jaro_from(1, 2, 2, 0));
    static_assert(clapp::jaro("文中", "测中") <= clapp::did_you_mean_threshold);

    // Two disjoint CJK strings share continuation bytes but no character.
    static_assert(clapp::jaro("中文", "文中") == 0.0);
    static_assert(clapp::jaro("测试配置", "配置测试") == 0.0);
    static_assert(clapp::jaro("中文", "中文") == 1.0);

    // Non-ASCII does not disturb the identities.
    static_assert(clapp::jaro("🚀", "🚀") == 1.0);
    static_assert(clapp::jaro("🚀", "") == 0.0);

    // --- The decoder is total: no byte string is rejected or dropped. ----------
    //
    // jaro() is public and takes a std::string_view, so similarity_units() has to
    // answer for every byte sequence. A malformed byte becomes one unit at
    // 0x110000 + byte, above the last code point, so it can never compare equal to
    // a real character.
    //
    // This is totality, not clap fidelity: the parser hands did_you_mean() the
    // output of parsed_arg::display() (== to_string_lossy()), which is exactly what
    // clap passes (arg_os.display().to_string()), so both sides see valid UTF-8 by
    // the time scoring happens. Measured — the only input on which the two rules
    // disagree at all is a multi-byte maximal subpart, e.g. the truncated emoji
    // lead F0 9F: clap's lossy conversion makes it one U+FFFD and scores
    // "add\xF0\x9F" against "add" at 0.916667, one unit per byte scores 0.866667.
    // Unreachable through the parser, which lossy-converts first; recorded here so
    // the next reader does not have to re-derive it.

    consteval std::size_t unit_count(std::string_view text) {
        return clapp::detail::similarity_units(text).size();
    }

    static_assert(unit_count("") == 0);
    static_assert(unit_count("abc") == 3);
    static_assert(unit_count("中文") == 2);  // 6 bytes
    static_assert(unit_count("🚀") == 1);    // 4 bytes
    static_assert(unit_count("\x80") == 1);  // orphan continuation byte
    static_assert(unit_count("\xFF\xFE") == 2);
    static_assert(unit_count("a\xC3") == 2);  // truncated two-byte lead

    // A malformed byte is its own unit, so a string containing one is still
    // identical to itself and still distinguishable from its repair.
    static_assert(clapp::jaro("a\xFF"
                              "b",
                              "a\xFF"
                              "b") == 1.0);
    static_assert(clapp::jaro("a\xFF"
                              "b",
                              "acb") == jaro_from(2, 3, 3, 0));

    // 0x110000 + 0xC3 must not collide with U+00C3, the character whose UTF-8
    // encoding begins with that byte. If it did, an invalid byte would silently
    // match a real letter.
    static_assert(clapp::jaro("\xC3", "Ã") == 0.0);

    // ---------------------------------------------------------------------------
    // did_you_mean() / best_match()
    //
    // Ported one-for-one from suggestions.rs `mod test`. Results are ascending by
    // confidence, so the best match is *last* — clap's caller ends in .pop().
    // ---------------------------------------------------------------------------

    constexpr std::array basic{"test"sv, "possible"sv, "values"sv};
    constexpr std::array ambiguous{"test"sv, "temp"sv, "possible"sv, "values"sv};
    constexpr std::array aligned{
            "test"sv, "possible"sv, "values"sv, "alignmentStart"sv, "alignmentScore"sv};

    static_assert(suggests("tst", basic, {std::array{"test"sv}}));
    static_assert(suggests("te", ambiguous, {std::array{"test"sv, "temp"sv}}));
    static_assert(suggests("hahaahahah", basic, {}));
    static_assert(suggests("alignmentScorr",
                           aligned,
                           {std::array{"alignmentStart"sv, "alignmentScore"sv}}));

    consteval bool best_is(std::string_view input,
                           std::span<const std::string_view> candidates,
                           std::string_view want) {
        const auto hit = clapp::best_match(input, candidates);
        return hit.has_value() && hit.value() == want;
    }
    consteval bool no_best(std::string_view input, std::span<const std::string_view> candidates) {
        return !clapp::best_match(input, candidates).has_value();
    }

    static_assert(best_is("tst", basic, "test"));
    static_assert(best_is("alignmentScorr", aligned, "alignmentScore"));
    static_assert(no_best("hahaahahah", basic));
    static_assert(no_best("anything", {}));

    // A tie resolves to the *last* candidate, matching clap's flag_ambiguous test.
    static_assert(best_is("te", ambiguous, "temp"));

    // Realistic option-name confusions.
    constexpr std::array options{"verbose"sv, "version"sv, "quiet"sv, "output-file"sv};
    static_assert(best_is("verbos", options, "verbose"));
    static_assert(best_is("outputfile", options, "output-file"));
    static_assert(no_best("zzzzzz", options));

}  // namespace

// ---------------------------------------------------------------------------
// Runtime cases
// ---------------------------------------------------------------------------

CLAPP_TEST("rename covers every clapp::naming style") {
    CLAPP_CHECK(clapp::rename("output_file", naming::kebab) == "output-file");
    CLAPP_CHECK(clapp::rename("output_file", naming::snake) == "output_file");
    CLAPP_CHECK(clapp::rename("output_file", naming::camel) == "outputFile");
    CLAPP_CHECK(clapp::rename("output_file", naming::pascal) == "OutputFile");
    CLAPP_CHECK(clapp::rename("output_file", naming::screaming_snake) == "OUTPUT_FILE");
    CLAPP_CHECK(clapp::rename("output_file", naming::lower) == "outputfile");
    CLAPP_CHECK(clapp::rename("output_file", naming::upper) == "OUTPUTFILE");
    CLAPP_CHECK(clapp::rename("output_file", naming::verbatim) == "output_file");
}

CLAPP_TEST("acronyms yield one word, not one word per capital") {
    CLAPP_CHECK(clapp::rename("HTTPServer", naming::kebab) == "http-server");
    CLAPP_CHECK(clapp::rename("XMLHttpRequest", naming::kebab) == "xml-http-request");
    CLAPP_CHECK(clapp::rename("IOError", naming::kebab) == "io-error");
}

CLAPP_TEST("the keyword-avoidance underscore is stripped for every style") {
    CLAPP_CHECK(clapp::rename("auto_", naming::kebab) == "auto");
    CLAPP_CHECK(clapp::rename("auto_", naming::verbatim) == "auto");
}

CLAPP_TEST("rename_size predicts the exact output length") {
    // Checked here as well as in the static_asserts because a mismatch would only
    // show up as a wasted or insufficient reserve(), never as a wrong result.
    constexpr std::array styles{naming::kebab,
                                naming::snake,
                                naming::camel,
                                naming::pascal,
                                naming::lower,
                                naming::upper,
                                naming::screaming_snake,
                                naming::verbatim};
    constexpr std::array names{"output_file"sv, "HTTPServer"sv, "auto_"sv, ""sv, "x"sv};
    for (const naming style : styles) {
        for (const std::string_view name : names) {
            CLAPP_CHECK(clapp::rename(name, style).size() == clapp::rename_size(name, style));
        }
    }
}

CLAPP_TEST("words() agrees with the cursor it wraps") {
    const std::vector<std::string_view> got = clapp::words("XMLHttpRequest");
    CLAPP_CHECK(got.size() == 3);
    CLAPP_CHECK(got[0] == "XML");
    CLAPP_CHECK(got[1] == "Http");
    CLAPP_CHECK(got[2] == "Request");
}

CLAPP_TEST("subcommand names drop cmd_ / _cmd / _args") {
    CLAPP_CHECK(clapp::subcommand_name("cmd_add") == "add");
    CLAPP_CHECK(clapp::subcommand_name("add_cmd") == "add");
    CLAPP_CHECK(clapp::subcommand_name("add_args") == "add");
    CLAPP_CHECK(clapp::subcommand_name("cmd_add_file") == "add-file");
    CLAPP_CHECK(clapp::subcommand_name("cmd") == "cmd");
}

CLAPP_TEST("did_you_mean reproduces clap's suggestion tests") {
    const std::vector<std::string_view> vals{"test", "possible", "values"};
    CLAPP_CHECK(clapp::did_you_mean("tst", vals) == std::vector<std::string_view>{"test"});
    CLAPP_CHECK(clapp::did_you_mean("hahaahahah", vals).empty());

    const std::vector<std::string_view> tied{"test", "temp", "possible", "values"};
    CLAPP_CHECK(clapp::did_you_mean("te", tied) == std::vector<std::string_view>{"test", "temp"});

    const std::vector<std::string_view> long_prefix{"alignmentScore", "alignmentStart"};
    CLAPP_CHECK(clapp::did_you_mean("alignmentScorr", long_prefix) ==
                std::vector<std::string_view>{"alignmentStart", "alignmentScore"});
}

CLAPP_TEST("did_you_mean matches clap on the pairs the old transposition rule lost") {
    // Bind the arguments to named objects rather than passing literals: GCC 16.1.0
    // can fold a constexpr call on string literals differently at run time than at
    // compile time. This test exists precisely to make the runtime answer trustworthy.
    const std::string_view merge{"merge"};
    const std::string_view verbose{"verbose"};

    // Byte-identical to what clap prints for a Command carrying these
    // subcommands, captured from clap itself at /Users/quincy/Desktop/clap:
    //   git merge   -> "tip: a similar subcommand exists: 'remote'"
    //   git reslove -> "tip: some similar subcommands exist: 'remote', 'restore',
    //                   'reserve', 'revolve', 'resolve'"
    const std::vector<std::string_view> git{
            "add",   "branch", "checkout", "clone",  "commit",  "config",  "diff",
            "fetch", "init",   "log",      "remote", "reset",   "restore", "revert",
            "stash", "status", "switch",   "tag",    "resolve", "revolve", "reserve"};

    CLAPP_CHECK(clapp::did_you_mean(merge, git) == std::vector<std::string_view>{"remote"});

    const std::string_view reslove{"reslove"};
    const std::vector<std::string_view> want{"remote", "restore", "reserve", "revolve", "resolve"};
    CLAPP_CHECK(clapp::did_you_mean(reslove, git) == want);

    // "verbose" against a set holding "revolve" now clears the threshold, as it
    // does in clap. The old rule scored it 0.676190 and offered nothing.
    const std::vector<std::string_view> opts{"revolve", "quiet"};
    const auto hit = clapp::best_match(verbose, opts);
    CLAPP_CHECK(hit.has_value());
    CLAPP_CHECK(hit.value() == "revolve");
}

CLAPP_TEST("similarity is measured in code points, so non-ASCII names rank as clap ranks them") {
    const std::string_view typed{"文中"};
    const std::vector<std::string_view> candidates{"测中", "文中"};

    // Byte comparison scored "文中" against "测中" at 0.777778 and would have
    // offered it; strsim says 0.666667, below the threshold.
    CLAPP_CHECK(clapp::did_you_mean(typed, candidates) == std::vector<std::string_view>{"文中"});

    const std::string_view cafe_acute{"café"};
    const std::string_view cafe_plain{"cafe"};
    CLAPP_CHECK(clapp::jaro(cafe_acute, cafe_plain) == clapp::jaro(cafe_plain, cafe_acute));

    // Total on malformed input: no crash, no dropped byte, and self-similarity
    // still 1.0.
    const std::string_view lone_byte{"a\xFF"
                                     "b"};
    CLAPP_CHECK(clapp::jaro(lone_byte, lone_byte) == 1.0);
    CLAPP_CHECK(clapp::detail::similarity_units(lone_byte).size() == 3);
}

CLAPP_TEST("did_you_mean accepts owning strings, not just views") {
    // The concept is on the reference type, so a range of std::string works and the
    // results alias its elements.
    const std::vector<std::string> owned{"verbose", "version", "quiet"};
    const auto hit = clapp::best_match("verbos", owned);
    CLAPP_CHECK(hit.has_value());
    CLAPP_CHECK(hit.value() == "verbose");
}

CLAPP_TEST("best_match agrees with the last element of did_you_mean") {
    const std::vector<std::string_view> vals{"test", "temp", "possible", "values"};
    const auto ranked = clapp::did_you_mean("te", vals);
    const auto best   = clapp::best_match("te", vals);
    CLAPP_CHECK(!ranked.empty());
    CLAPP_CHECK(best.has_value());
    CLAPP_CHECK(best.value() == ranked.back());
}

CLAPP_TEST("Damerau-Levenshtein is the unrestricted variant") {
    CLAPP_CHECK(clapp::damerau_levenshtein("ca", "abc") == 2);
    CLAPP_CHECK(clapp::damerau_levenshtein("kitten", "sitting") == 3);
    CLAPP_CHECK(clapp::damerau_levenshtein("verbose", "verbsoe") == 1);
}
