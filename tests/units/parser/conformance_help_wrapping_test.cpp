#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::help_style;
    using clapp::value_range;

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        Identical to conformance_help_test.cpp's helper plus the `detected_width`
     *        parameter that `issue_626_variable_panic` sweeps.
     */
    std::string page(const command_spec& cmd,
                     bool long_form,
                     std::string_view usage_name               = {},
                     std::optional<std::size_t> detected_width = {}) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd),
                                             .usage_name     = usage_name,
                                             .detected_width = detected_width})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    /**
     * \brief Whether \p text is well-formed UTF-8.
     *
     * `issue_626_panic` and `issue_626_variable_panic` are about a wrapper that indexes
     * bytes. In Rust that is a panic; in C++ it is a silent half-codepoint, so the property
     * clap gets for free has to be asserted here. Deliberately written out rather than
     * reusing a clapp helper: a bug in the thing under test must not be able to hide behind
     * the same bug in the checker. Rejects overlong forms, surrogates and >U+10FFFF, since a
     * truncating wrapper produces exactly those.
     */
    bool valid_utf8(std::string_view text) {
        std::size_t i = 0;
        while (i < text.size()) {
            const auto b0     = static_cast<unsigned char>(text[i]);
            std::size_t extra = 0;
            unsigned lo       = 0x80;
            unsigned hi       = 0xBF;
            if (b0 < 0x80) {
                extra = 0;
            } else if (b0 >= 0xC2 && b0 <= 0xDF) {
                extra = 1;
            } else if (b0 == 0xE0) {
                extra = 2;
                lo    = 0xA0;  // no overlong 3-byte forms
            } else if (b0 >= 0xE1 && b0 <= 0xEC) {
                extra = 2;
            } else if (b0 == 0xED) {
                extra = 2;
                hi    = 0x9F;  // no UTF-16 surrogates
            } else if (b0 >= 0xEE && b0 <= 0xEF) {
                extra = 2;
            } else if (b0 == 0xF0) {
                extra = 3;
                lo    = 0x90;  // no overlong 4-byte forms
            } else if (b0 >= 0xF1 && b0 <= 0xF3) {
                extra = 3;
            } else if (b0 == 0xF4) {
                extra = 3;
                hi    = 0x8F;  // no code point above U+10FFFF
            } else {
                return false;  // 0x80–0xC1 and 0xF5–0xFF start nothing
            }
            if (i + extra >= text.size()) return false;
            for (std::size_t k = 1; k <= extra; ++k) {
                const auto b = static_cast<unsigned char>(text[i + k]);
                const auto l = (k == 1) ? lo : 0x80U;
                const auto h = (k == 1) ? hi : 0xBFU;
                if (b < l || b > h) return false;
            }
            i += extra + 1;
        }
        return true;
    }

    /**
     * \brief Whether \p text contains a byte outside ASCII.
     *
     * Exists for one reason. Three cases in conformance_help_test.cpp shipped with clap's
     * `café` quietly respelled `cafe` in both the fixture AND the expectation, which passes
     * for ever and deletes the property the case carries. Nothing catches that by comparing
     * pages — the two sides move together — so the fixture's own bytes have to be asserted.
     * Any case in this file whose name says "unicode" states this about its input.
     */
    consteval bool has_non_ascii(std::string_view text) {
        for (const char c : text)
            if (static_cast<unsigned char>(c) >= 0x80U) return true;
        return false;
    }

    // ---------------------------------------------------------------------------
    // Value domains for `possible_value_wrapped_help`
    //
    // clap writes `value_parser([PossibleValue::new("short_name").help(..), ..])`; clapp
    // enumerates a domain as a type and puts the name and help on the enumerator. The three
    // domains differ only in how long their names and help texts are, which is the whole point
    // of the case: name length decides whether the `name:` column is padded, and help length
    // decides whether the help wraps.
    // ---------------------------------------------------------------------------

    /**
     * Names short, one help long enough to wrap at 67 columns: the column IS padded, and the
     * wrapped line hangs past the `- ` marker.
     */
    enum class pv_wrapping {
        short_name[[= clapp::value{.name = "short_name",
                                   .help = "Long enough help message, barely warrant wrapping"}]],
        second[[= clapp::value{.help = "Short help gets handled the same"}]],
    };

    /**
     * One name long enough that padding every row to it would leave no room for the help, so
     * the padding is dropped for the whole block. `second` carries no help and so gets no
     * colon either.
     */
    enum class pv_new_line {
        long_name[[= clapp::value{.name = "long enough name to trigger new line",
                                  .help = "Really long enough help message to clearly warrant "
                                          "wrapping believe me"}]],
        second,
    };

    /** Short names, short help: the padded column survives and nothing wraps. */
    enum class pv_no_new_line {
        name[[= clapp::value{.help = "Short enough help message with no wrapping"}]],
        second[[= clapp::value{.help = "short help"}]],
    };

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_pv_wrapped() {
        command_builder app("test");
        std::move(app)
                .term_width(67)
                .arg(arg_builder("possible_values")
                             .long_("possible-values")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .value_parser<pv_wrapping>())
                .arg(arg_builder("possible_values_with_new_line")
                             .long_("possible-values-with-new-line")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .value_parser<pv_new_line>())
                .arg(arg_builder("possible_values_without_new_line")
                             .long_("possible-values-without-new-line")
                             .action(arg_action::set)
                             .num_args(value_range::exactly(1))
                             .value_parser<pv_no_new_line>());
        return app.freeze();
    }
    constexpr command_spec pv_wrapped = make_pv_wrapped();

    /**
     * clap's issue 626 text, verbatim. Rust's `\` line continuation eats the newline and the
     * following indentation, so the three source fragments concatenate with a single space
     * between them exactly as spelled here.
     */
    constexpr std::string_view cafe_help =
            "La culture du café est très développée dans de nombreux pays à climat chaud "
            "d'Amérique, d'Afrique et d'Asie, dans des plantations qui sont cultivées pour les "
            "marchés d'exportation. Le café est souvent une contribution majeure aux "
            "exportations des régions productrices.";

    consteval command_spec make_626(std::size_t width) {
        command_builder app("ctest");
        arg_builder cafe("cafe");
        std::move(cafe)
                .short_('c')
                .long_("cafe")
                .value_name("FILE")
                .help(cafe_help)
                .action(arg_action::set)
                .num_args(value_range::exactly(1));
        std::move(app).version("0.1").arg(std::move(cafe));
        if (width != 0) std::move(app).term_width(width);
        return app.freeze();
    }
    constexpr command_spec cmd_626 = make_626(52);

    /**
     * The same command with NO `term_width`, so that `help_style::detected_width` reaches
     * clapp::resolve_wrap_width() unchanged and the sweep can vary the width at run time.
     * See the comment on `issue_626_variable_panic` for why this is the faithful analogue.
     */
    constexpr command_spec cmd_626_auto = make_626(0);

    /**
     * Three commands built the way clap's loop builds them — with a real `term_width(i)` —
     * at the bottom, middle and top of clap's range. The sweep below renders `cmd_626_auto`
     * with `detected_width`; these exist so the substitution is *proved* equivalent at the
     * renderer rather than argued from clapp::resolve_wrap_width's signature.
     */
    constexpr command_spec cmd_626_w10  = make_626(10);
    constexpr command_spec cmd_626_w319 = make_626(319);

    /**
     * clap's issue 626 *cutoff* text, verbatim, with Rust's `\` continuations collapsed the
     * same way. A different string from `cafe_help` above and a different command: this one
     * renders in the COMPACT layout at term_width 70, so the wrap width is what is left of 70
     * after a 21-column description column rather than the fixed 10 of NEXT_LINE_INDENT.
     *
     * Spelled with real `é` bytes, which is the entire point — see the case.
     */
    constexpr std::string_view cafe_cutoff_help =
            "A coffeehouse, coffee shop, or café is an establishment which primarily serves hot "
            "coffee, related coffee beverages (e.g., café latte, cappuccino, espresso), tea, and "
            "other hot beverages. Some coffeehouses also serve cold beverages such as iced coffee "
            "and iced tea. Many cafés also serve some type of food, such as light snacks, muffins, "
            "or pastries.";

    consteval command_spec make_unicode_cutoff() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(70).arg(
                arg_builder("cafe")
                        .short_('c')
                        .long_("cafe")
                        .value_name("FILE")
                        .help(cafe_cutoff_help)
                        .action(arg_action::set)
                        .num_args(value_range::exactly(1)));
        return app.freeze();
    }
    constexpr command_spec unicode_cutoff_cmd = make_unicode_cutoff();

    consteval command_spec make_newline_chars() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(60).arg(arg_builder("mode").help(
                "x, max, maximum   20 characters, contains symbols.\n"
                "l, long           Copy-friendly, 14 characters, contains symbols.\n"
                "m, med, medium    Copy-friendly, 8 characters, contains symbols.\n"));
        return app.freeze();
    }
    constexpr command_spec newline_chars = make_newline_chars();

    /** The same three lines with clap's `{n}` escape in place of `\n`. */
    consteval command_spec make_newline_variables() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(60).arg(arg_builder("mode").help(
                "x, max, maximum   20 characters, contains symbols.{n}"
                "l, long           Copy-friendly, 14 characters, contains symbols.{n}"
                "m, med, medium    Copy-friendly, 8 characters, contains symbols.{n}"));
        return app.freeze();
    }
    constexpr command_spec newline_variables = make_newline_variables();

    consteval command_spec make_wrapped_indentation() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(60).arg(arg_builder("mode").help(
                "Some values:\n"
                "  - l, long           Copy-friendly, 14 characters, contains symbols.\n"
                "  - m, med, medium    Copy-friendly, 8 characters, contains symbols."));
        return app.freeze();
    }
    constexpr command_spec wrapped_indentation = make_wrapped_indentation();

    consteval command_spec make_old_newline(bool variable) {
        command_builder app("ctest");
        std::move(app).version("0.1").arg(
                arg_builder("mode")
                        .short_('m')
                        .action(arg_action::set_true)
                        .help(variable ? "Some help with some wrapping{n}(Defaults to something)"
                                       : "Some help with some wrapping\n(Defaults to something)"));
        return app.freeze();
    }
    constexpr command_spec old_newline_chars_cmd     = make_old_newline(false);
    constexpr command_spec old_newline_variables_cmd = make_old_newline(true);

    consteval command_spec make_wrapped_help() {
        command_builder app("test");
        std::move(app)
                .term_width(67)
                .arg(arg_builder("all")
                             .short_('a')
                             .long_("all")
                             .action(arg_action::set_true)
                             .help("Also do versioning for private crates (will not be "
                                   "published)"))
                .arg(arg_builder("exact")
                             .long_("exact")
                             .action(arg_action::set_true)
                             .help("Specify inter dependency version numbers exactly with `=`"))
                .arg(arg_builder("no_git_commit")
                             .long_("no-git-commit")
                             .action(arg_action::set_true)
                             .help("Do not commit version changes"))
                .arg(arg_builder("no_git_push")
                             .long_("no-git-push")
                             .action(arg_action::set_true)
                             .help("Do not push generated commit and tags to git remote"))
                .subcommand(command_builder("sub1").about(
                        "One two three four five six seven eight nine ten eleven twelve "
                        "thirteen fourteen fifteen"));
        return app.freeze();
    }
    constexpr command_spec wrapped_help_cmd = make_wrapped_help();

    /** No arguments of its own: the screen is `-h` and `-V` and nothing else. */
    consteval command_spec make_final_word_wrapping() {
        command_builder app("ctest");
        std::move(app).version("0.1").term_width(24);
        return app.freeze();
    }
    constexpr command_spec final_word_wrapping_cmd = make_final_word_wrapping();

    /**
     * The URL clap's fixture uses, spelled once so the case can assert its length as well as
     * assert the page. Requirement of the case, not decoration: if the URL ever fitted in the
     * available column the expected output would still match and the case would be proving
     * nothing.
     */
    constexpr std::string_view rustup_url =
            "https://github.com/rust-lang/rustup/wiki/Non-host-toolchains";

    consteval command_spec make_dont_wrap_urls() {
        command_builder app("Example");
        std::move(app).term_width(30).subcommand(command_builder("update").arg(
                arg_builder("force-non-host")
                        .help("Install toolchains that require an emulator. See "
                              "https://github.com/rust-lang/rustup/wiki/Non-host-toolchains")
                        .long_("force-non-host")
                        .action(arg_action::set_true)));
        return app.freeze();
    }
    constexpr command_spec dont_wrap_urls_cmd = make_dont_wrap_urls();

    /**
     * clap's `utils::FULL_TEMPLATE` (clap/tests/builder/utils.rs:9), transcribed. Rust's `\`
     * after the opening quote eats the first newline, so the template begins at
     * `{before-help}` with no leading blank line.
     */
    constexpr std::string_view full_template = "{before-help}{name} {version}\n"
                                               "{author-with-newline}{about-with-newline}\n"
                                               "{usage-heading} {usage}\n"
                                               "\n"
                                               "{all-args}{after-help}";

    consteval command_spec make_issue_777() {
        command_builder app("A cmd with a crazy very long long long name hahaha");
        std::move(app)
                .version("1.0")
                .author("Some Very Long Name and crazy long email <email@server.com>")
                .about("Show how the about text is not wrapped")
                .help_template(full_template)
                .term_width(35);
        return app.freeze();
    }
    constexpr command_spec issue_777_cmd = make_issue_777();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // Every expectation below is a function of the wrap width, so pin the width each fixture
    // was built with. Without these, a builder change that silently dropped `term_width()`
    // would re-flow every screen in this file and the failures would all look like wrapper
    // bugs.
    // ---------------------------------------------------------------------------

    static_assert(pv_wrapped.get_term_width() == std::optional<std::size_t>{67});
    static_assert(cmd_626.get_term_width() == std::optional<std::size_t>{52});
    static_assert(unicode_cutoff_cmd.get_term_width() == std::optional<std::size_t>{70});
    static_assert(newline_chars.get_term_width() == std::optional<std::size_t>{60});
    static_assert(newline_variables.get_term_width() == std::optional<std::size_t>{60});
    static_assert(wrapped_indentation.get_term_width() == std::optional<std::size_t>{60});

    // ...and that the two `old_newline_*` cases have none, which is what makes them wrap at
    // clapp::default_terminal_width and therefore not wrap at all.
    static_assert(old_newline_chars_cmd.get_term_width() == std::nullopt);
    static_assert(old_newline_variables_cmd.get_term_width() == std::nullopt);

    // `cmd_626_auto` must have no `term_width` and no `max_term_width`, or the sweep below
    // would silently render one width 310 times and prove nothing. Trap 13's lesson in
    // miniature: a loop that runs is not a loop that varies. Both ends of clap's range are
    // pinned, because a cap would only show at the top of it.
    static_assert(cmd_626_auto.get_term_width() == std::nullopt);
    static_assert(cmd_626_auto.get_max_term_width() == std::nullopt);
    static_assert(cmd_626_w10.get_term_width() == std::optional<std::size_t>{10});
    static_assert(cmd_626_w319.get_term_width() == std::optional<std::size_t>{319});
    static_assert(clapp::resolve_wrap_width(cmd_626_auto.get_term_width(),
                                            cmd_626_auto.get_max_term_width(),
                                            10U) == 10U);
    static_assert(clapp::resolve_wrap_width(cmd_626_auto.get_term_width(),
                                            cmd_626_auto.get_max_term_width(),
                                            319U) == 319U);

    // A value's help is what upgrades `--help` to a screen of its own, and the possible-value
    // case needs the long screen to reach the bullet list at all.
    static_assert(clapp::long_help_exists(pv_wrapped));

    // Both 626 fixtures must really be multibyte, and the checker must really be able to say
    // no — otherwise the assertion above it is decoration. `has_non_ascii` is the only guard
    // against the failure mode that actually occurred in this project: respelling `café` as
    // `cafe` in fixture and expectation at once, which no page comparison can see.
    static_assert(has_non_ascii(cafe_cutoff_help));
    static_assert(has_non_ascii(cafe_help));
    static_assert(!has_non_ascii("A coffeehouse, coffee shop, or cafe."));

    // `unicode_cutoff_cmd` renders the SHORT screen — clap's expected page says plain
    // `Print help` and puts every description beside its flag rather than under it. If this
    // ever became long help the case would move to NEXT_LINE_INDENT, the wrap width would
    // stop being `70 - 21`, and the boundary row the case exists for would vanish.
    static_assert(!clapp::long_help_exists(unicode_cutoff_cmd));

    // The five `\n` / `{n}` fixtures must NOT reach the long screen — clap's expected output
    // for all of them says plain `Print help`, with no `(see a summary with '-h')`. A newline
    // in a `help()` string is not long help, and if it became one every expectation in the
    // second half of this file would change layout rather than merely wrap differently.
    static_assert(!clapp::long_help_exists(newline_chars));
    static_assert(!clapp::long_help_exists(newline_variables));
    static_assert(!clapp::long_help_exists(wrapped_indentation));
    static_assert(!clapp::long_help_exists(old_newline_chars_cmd));
    static_assert(!clapp::long_help_exists(old_newline_variables_cmd));

    // The four cases added last, same rule: every screen below is a function of its width.
    static_assert(wrapped_help_cmd.get_term_width() == std::optional<std::size_t>{67});
    static_assert(final_word_wrapping_cmd.get_term_width() == std::optional<std::size_t>{24});
    static_assert(dont_wrap_urls_cmd.get_term_width() == std::optional<std::size_t>{30});
    static_assert(issue_777_cmd.get_term_width() == std::optional<std::size_t>{35});

    // `dont_wrap_urls` renders the CHILD's page, and clap's `term_width` is documented as
    // applying globally rather than per command. clapp propagates it at freeze time
    // (command.hpp:2901); pin that here, because if it stopped propagating the child would
    // silently render at clapp::default_terminal_width and the URL would no longer overflow
    // anything — the case would pass its own name while testing nothing.
    static_assert(dont_wrap_urls_cmd.has_subcommand("update"));
    static_assert(dont_wrap_urls_cmd.find_subcommand("update")->get_term_width() ==
                  std::optional<std::size_t>{30});

    // ...and that the URL really is too long for the hole it is rendered into. At
    // term_width 30 with the next-line layout the description column starts at
    // NEXT_LINE_INDENT (10), leaving 20; the URL is 60. Without this the expected string
    // below would be satisfied by a fixture whose URL happened to fit, which is exactly the
    // vacuous-case shape traps 15–17 are all instances of.
    static_assert(rustup_url.size() == 60);
    static_assert(rustup_url.size() > 30 - 10);
    static_assert(rustup_url.find(' ') == std::string_view::npos);

    // The template must have reached the spec. `help_template("")` is stored as *no*
    // template (the length sentinel — see conformance_template_help_test.cpp's divergence
    // note), so a typo that produced an empty string here would silently fall back to the
    // automatic layout and `issue_777`'s header lines would come from the wrong code path.
    static_assert(issue_777_cmd.get_help_template() ==
                  std::optional<std::string_view>{full_template});

    // None of the four has long help, so all four render the SHORT screen. clap's expected
    // strings say plain `Print help` with no `(see a summary with '-h')`, and the layout
    // difference between the two screens is larger than anything wrapping does — pin the
    // input rather than discover it in the diff.
    static_assert(!clapp::long_help_exists(wrapped_help_cmd));
    static_assert(!clapp::long_help_exists(final_word_wrapping_cmd));
    static_assert(!clapp::long_help_exists(*dont_wrap_urls_cmd.find_subcommand("update")));
    static_assert(!clapp::long_help_exists(issue_777_cmd));

}  // namespace

// ---------------------------------------------------------------------------
// Possible values, wrapped
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::possible_value_wrapped_help") {
    // Three blocks, three different outcomes, one screen:
    //   1. padded column (`short_name:` / `second:    `) + a wrap that hangs at column 12,
    //      two past the block indent, so the continuation lines up under the item text and
    //      not under the `- `;
    //   2. one name too long, so the whole block loses its padding — and `- second`, with
    //      no help, loses its colon too;
    //   3. short names, short help: padded, nothing wraps.
    // A renderer with one code path for the list satisfies whichever of these is tested
    // alone; it cannot satisfy all three.
    CLAPP_CHECK(same(page(pv_wrapped, true),
                     "Usage: test [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --possible-values <possible_values>\n"
                     "          Possible values:\n"
                     "          - short_name: Long enough help message, barely warrant\n"
                     "            wrapping\n"
                     "          - second:     Short help gets handled the same\n"
                     "\n"
                     "      --possible-values-with-new-line <possible_values_with_new_line>\n"
                     "          Possible values:\n"
                     "          - long enough name to trigger new line: Really long\n"
                     "            enough help message to clearly warrant wrapping believe\n"
                     "            me\n"
                     "          - second\n"
                     "\n"
                     "      --possible-values-without-new-line "
                     "<possible_values_without_new_line>\n"
                     "          Possible values:\n"
                     "          - name:   Short enough help message with no wrapping\n"
                     "          - second: short help\n"
                     "\n"
                     "  -h, --help\n"
                     "          Print help (see a summary with '-h')\n"));
}

// ---------------------------------------------------------------------------
// Multibyte text
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::issue_626_panic") {
    // Wrapping measures display cells. Every line below ends on a word boundary and
    // several of those words are accented; a byte-indexed wrapper produces both shorter
    // lines and broken codepoints here. Both are asserted: the byte-for-byte screen, and
    // the property clap's `panic` stood for.
    const std::string got = page(cmd_626, true);
    CLAPP_CHECK(valid_utf8(got));
    CLAPP_CHECK(same(got,
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe <FILE>\n"
                     "          La culture du café est très développée\n"
                     "          dans de nombreux pays à climat chaud\n"
                     "          d'Amérique, d'Afrique et d'Asie, dans des\n"
                     "          plantations qui sont cultivées pour les\n"
                     "          marchés d'exportation. Le café est souvent\n"
                     "          une contribution majeure aux exportations\n"
                     "          des régions productrices.\n"
                     "  -h, --help\n"
                     "          Print help\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

CLAPP_TEST("help.rs::issue_626_unicode_cutoff") {
    // The compact-layout half of issue 626, and the sharper of the two multibyte cases.
    //
    // WHY THIS PAGE IS NOT `issue_626_panic` WITH DIFFERENT WORDS. At term_width 70 the
    // description column starts at 21, so the wrapper is handed 49 columns. Measured on the
    // rendered page (GCC 16.1.0, 2026-08-11), its rows in display columns / bytes:
    //
    //     62 / 63    -c, --cafe <FILE>  A coffeehouse, ... or café is an
    //     69 / 69                       establishment which primarily serves hot coffee,
    //     64 / 65                       related coffee beverages (e.g., café latte,
    //     62 / 62                       cappuccino, espresso), tea, and other hot
    //     65 / 65                       beverages. Some coffeehouses also serve cold
    //     69 / 69                       beverages such as iced coffee and iced tea. Many
    //     70 / 71                       cafés also serve some type of food, such as light
    //
    // The last row is 70 columns wide — flush against term_width, with nothing to spare —
    // and 71 BYTES. A wrapper that measured bytes would have called it 71 > 70 and pushed
    // `light` onto the following row. Every row of `issue_626_panic` has slack, so that
    // wrapper passes it; this is the case that fails.
    //
    // AND WHY THE FIXTURE'S BYTES ARE ASSERTED ABOVE RATHER THAN LEFT IMPLICIT. `é` and `e`
    // are both one display column, so respelling the fixture ASCII and respelling the
    // expectation to match produces a page with IDENTICAL line breaks — measured: 619 bytes
    // against 622, same seven rows, same seven break points. The comparison below cannot
    // see that substitution; `has_non_ascii(cafe_cutoff_help)` is what does.
    const std::string got = page(unicode_cutoff_cmd, true);
    CLAPP_CHECK(valid_utf8(got));
    CLAPP_CHECK(same(got,
                     "Usage: ctest [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "  -c, --cafe <FILE>  A coffeehouse, coffee shop, or café is an\n"
                     "                     establishment which primarily serves hot coffee,\n"
                     "                     related coffee beverages (e.g., café latte,\n"
                     "                     cappuccino, espresso), tea, and other hot\n"
                     "                     beverages. Some coffeehouses also serve cold\n"
                     "                     beverages such as iced coffee and iced tea. Many\n"
                     "                     cafés also serve some type of food, such as light\n"
                     "                     snacks, muffins, or pastries.\n"
                     "  -h, --help         Print help\n"
                     "  -V, --version      Print version\n"));
}

CLAPP_TEST("help.rs::issue_626_variable_panic") {
    // clap loops `term_width(i)` for i in 10..320 and asserts nothing: in Rust the failure
    // mode is a panic on a non-char-boundary slice, so "it returned" IS the assertion. C++
    // has no such backstop — the same wrapper bug here yields a half-codepoint and a
    // perfectly successful return — so this port asserts the properties the panic stood
    // for.
    //
    // `term_width` lives on the frozen `command_spec` and ADR-0005 freezes the tree at
    // compile time, so 310 distinct `term_width(i)` commands would be 310 consteval
    // instantiations of the whole builder. `detected_width` is the same input one layer
    // up: with `term_width` and `max_term_width` both unset, clapp::resolve_wrap_width()
    // returns the detected width unchanged — static_asserted at both ends of clap's range
    // above — so the wrapper sees exactly the widths clap's loop gives it.

    // First, prove the checker can fail. A checker that returns true for everything would
    // make the 310 iterations below meaningless, and nothing else in this file would
    // notice. `café` is `c a f \xC3\xA9`; the three inputs are a truncated lead byte, a
    // bare continuation byte, and a lone continuation after valid text — the three shapes
    // a byte-indexed cut actually produces.
    CLAPP_CHECK(valid_utf8("caf\xC3\xA9"));
    CLAPP_CHECK(!valid_utf8("caf\xC3"));
    CLAPP_CHECK(!valid_utf8("\xA9 est"));
    CLAPP_CHECK(!valid_utf8("caf\xC3\xA9\xA9"));

    for (std::size_t width = 10; width < 320; ++width) {
        const std::string got = page(cmd_626_auto, true, {}, width);
        // 1. No half-codepoint anywhere on the page. This is the defect clap's panic was
        //    reporting, and the only one of the three that is silent in C++.
        CLAPP_CHECK(valid_utf8(got));
        // 2. It terminated and produced a page. A wrapper that cannot fit a word into the
        //    available columns is the classic infinite-loop / empty-output shape, and
        //    width 10 against a 10-column NEXT_LINE_INDENT is exactly that corner.
        CLAPP_CHECK(got.starts_with("Usage: ctest"));
        CLAPP_CHECK(got.ends_with("\n"));
    }

    // 3. The sweep must actually SWEEP. clapp::resolve_wrap_width's pass-through is
    //    static_asserted above, but that pins the seam, not the screen: a renderer that
    //    ignored `detected_width` downstream of it would render one page 310 times and
    //    every assertion above would still pass (trap 13's shape, one layer down).
    CLAPP_CHECK(page(cmd_626_auto, true, {}, 10) != page(cmd_626_auto, true, {}, 319));

    // 4. And the substitution must be FAITHFUL. clap's loop varies `term_width(i)`; this
    //    port varies `detected_width` because ADR-0005 freezes the tree at compile time
    //    and 310 `term_width(i)` commands are 310 consteval builder instantiations. Three
    //    of them — the two ends of clap's range and the width its sibling case pins — are
    //    built the way clap builds them and must render the identical page. Without this,
    //    the two inputs are only argued to be equivalent; with it they are measured, and a
    //    future divergence between the explicit and detected paths fails here rather than
    //    quietly making this case test a code path clap never exercised.
    CLAPP_CHECK(same(page(cmd_626_w10, true), page(cmd_626_auto, true, {}, 10)));
    CLAPP_CHECK(same(page(cmd_626, true), page(cmd_626_auto, true, {}, 52)));
    CLAPP_CHECK(same(page(cmd_626_w319, true), page(cmd_626_auto, true, {}, 319)));
}

// ---------------------------------------------------------------------------
// Newlines: written as `\n` and written as `{n}`
// ---------------------------------------------------------------------------

// One expectation, two fixtures. clap gives these two cases separate `#[test]`s with
// byte-identical expected strings; keeping the string in one place here is what makes the
// equality itself the assertion rather than a coincidence between two copies.
namespace {
    constexpr std::string_view wrapping_newline_expected =
            "Usage: ctest [mode]\n"
            "\n"
            "Arguments:\n"
            "  [mode]  x, max, maximum   20 characters, contains symbols.\n"
            "          l, long           Copy-friendly, 14 characters,\n"
            "          contains symbols.\n"
            "          m, med, medium    Copy-friendly, 8 characters,\n"
            "          contains symbols.\n"
            "\n"
            "Options:\n"
            "  -h, --help     Print help\n"
            "  -V, --version  Print version\n";
}  // namespace

CLAPP_TEST("help.rs::wrapping_newline_chars") {
    // An author's own `\n` starts a new line, and each of those lines then wraps on its
    // own — the first source line fits in 60 columns and stays one row, the second and
    // third do not and become two each. The trailing `\n` on the third produces no
    // trailing blank row.
    CLAPP_CHECK(same(page(newline_chars, true), wrapping_newline_expected));
}

CLAPP_TEST("help.rs::wrapping_newline_variables") {
    // `{n}` is clap's escape for authors who cannot write a literal newline (a derive
    // attribute, a TOML string). Same screen, byte for byte, as the case above.
    CLAPP_CHECK(same(page(newline_variables, true), wrapping_newline_expected));
}

CLAPP_TEST("help.rs::wrapped_indentation") {
    // Read the expected output twice. The two leading spaces of each `  - ` source line
    // survive onto that line's FIRST output row (`- l, long` starts at column 12, two past
    // the `[mode]` description column at 10) and are GONE from its wrapped rows
    // (`characters, contains symbols.` is back at column 10). That asymmetry is clap's,
    // and it is not an accident of clap's: it falls out of treating the leading spaces as
    // ordinary content of the first word, so the wrapper never re-emits them because it
    // never recorded them as an indent.
    //
    // This is the only case in help.rs that pins author indentation reaching the screen at
    // all, and it is the one a renderer written from the documentation rather than from
    // clap gets wrong in either direction — by stripping the indent (flattening the block)
    // or by honouring it (indenting the continuations too). clapp reproduces clap exactly.
    CLAPP_CHECK(same(page(wrapped_indentation, true),
                     "Usage: ctest [mode]\n"
                     "\n"
                     "Arguments:\n"
                     "  [mode]  Some values:\n"
                     "            - l, long           Copy-friendly, 14\n"
                     "            characters, contains symbols.\n"
                     "            - m, med, medium    Copy-friendly, 8 characters,\n"
                     "            contains symbols.\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}

// ---------------------------------------------------------------------------
// Newlines with no term_width — the compact layout's hanging indent
// ---------------------------------------------------------------------------

namespace {
    // clap again gives these two separate tests with identical expectations. The hanging
    // indent is the description column of the COMPACT layout (15 columns, set by
    // `-V, --version`), not NEXT_LINE_INDENT — which is why these two are the only members
    // of the family clap compiles without the `wrap_help` feature: no wrapping happens
    // here at all, only newline handling.
    constexpr std::string_view old_newline_expected =
            "Usage: ctest [OPTIONS]\n"
            "\n"
            "Options:\n"
            "  -m             Some help with some wrapping\n"
            "                 (Defaults to something)\n"
            "  -h, --help     Print help\n"
            "  -V, --version  Print version\n";
}  // namespace

CLAPP_TEST("help.rs::old_newline_chars") {
    CLAPP_CHECK(same(page(old_newline_chars_cmd, true), old_newline_expected));
}

CLAPP_TEST("help.rs::old_newline_variables") {
    CLAPP_CHECK(same(page(old_newline_variables_cmd, true), old_newline_expected));
}

// ---------------------------------------------------------------------------
// The compact layout, wrapped
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::wrapped_help") {
    // Two blocks, two independently computed columns, one term_width. `Commands:` is
    // measured from `sub1` (8 columns of gutter) and `Options:` from `--no-git-commit`
    // (23 with its gutter), so the subcommand about wraps at 67-8 = 59 and the flag
    // descriptions at 67-23 = 44. Both numbers show in the expected string below: `eleven` is the last word
    // that fits on the first `sub1` row, `(will` the last that fits on the first `--all`
    // row. A renderer that computed one width for the page would produce a screen that is
    // internally consistent and wrong in both blocks.
    //
    // Its sibling `unwrapped_help` — the same command at term_width 68, one column wider,
    // where nothing wraps at all — is already ported in conformance_help_test.cpp.
    CLAPP_CHECK(same(page(wrapped_help_cmd, true),
                     "Usage: test [OPTIONS] [COMMAND]\n"
                     "\n"
                     "Commands:\n"
                     "  sub1  One two three four five six seven eight nine ten eleven\n"
                     "        twelve thirteen fourteen fifteen\n"
                     "  help  Print this message or the help of the given subcommand(s)\n"
                     "\n"
                     "Options:\n"
                     "  -a, --all            Also do versioning for private crates (will\n"
                     "                       not be published)\n"
                     "      --exact          Specify inter dependency version numbers\n"
                     "                       exactly with `=`\n"
                     "      --no-git-commit  Do not commit version changes\n"
                     "      --no-git-push    Do not push generated commit and tags to git\n"
                     "                       remote\n"
                     "  -h, --help           Print help\n"));
}

CLAPP_TEST("help.rs::final_word_wrapping") {
    // term_width 24, and the compact layout is abandoned: `  -V, --version  Print version`
    // is 30 columns and does not fit, so BOTH flags fall to NEXT_LINE_INDENT — including
    // `-h, --help`, whose compact row would have been exactly 24. That is the case's
    // content: the decision is taken once for the block from its widest member, not per
    // row, so the narrower row is demoted with it.
    //
    // clap's name refers to the original defect — the final word of a description was lost
    // when it ended flush against the boundary — and `Print version` at 10 columns of
    // indent plus 13 of text is 23 against a 24-column terminal, one short of flush. The
    // adjacent flush case (`Print help`, 10 + 10 = 20) is here too; the genuinely flush one
    // is `issue_626_variable_panic`'s sweep, which walks every width across every word.
    CLAPP_CHECK(same(page(final_word_wrapping_cmd, true),
                     "Usage: ctest\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help\n"
                     "          Print help\n"
                     "  -V, --version\n"
                     "          Print version\n"));
}

// ---------------------------------------------------------------------------
// A word wider than the column
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::dont_wrap_urls") {
    // The wrapper breaks BETWEEN words and never inside one, so a 60-column URL rendered
    // into a 20-column hole is emitted whole and overflows the terminal. That overflow is
    // the intended behaviour and the reason the case exists: a URL cut in half is not a
    // URL, whereas a URL that runs off the right edge can still be selected and pasted.
    //
    // Note where the URL sits. `See` ends the third wrapped row and the URL starts the
    // fourth alone, which is the observable difference between "never break a word" and
    // "break a word only when it cannot fit": the latter would have packed `See` and the
    // first 16 characters of the URL onto one row. Both halves are pinned by one string.
    //
    // clap renders the child (`Example update --help`); clapp's parse loop computes the
    // child's usage name rather than storing it (ADR-0005), so it is passed explicitly —
    // the same substitution conformance_help_parent_cmd_req_test.cpp documents.
    const command_spec& update = *dont_wrap_urls_cmd.find_subcommand("update");
    const std::string got      = page(update, true, "Example update");
    CLAPP_CHECK(got.find(rustup_url) != std::string::npos);  // whole, on one row
    CLAPP_CHECK(same(got,
                     "Usage: Example update [OPTIONS]\n"
                     "\n"
                     "Options:\n"
                     "      --force-non-host\n"
                     "          Install toolchains\n"
                     "          that require an\n"
                     "          emulator. See\n"
                     "          https://github.com/rust-lang/rustup/wiki/Non-host-toolchains\n"
                     "  -h, --help\n"
                     "          Print help\n"));
}

// ---------------------------------------------------------------------------
// Wrapping reaches the template's header lines too
// ---------------------------------------------------------------------------

CLAPP_TEST("help.rs::issue_777_wrap_all_things") {
    // `{name}`, `{author-with-newline}` and `{about-with-newline}` all carry text longer
    // than the 35-column terminal, and all three wrap. "All things" is the case's claim and
    // the reason it uses FULL_TEMPLATE rather than the default layout: the default screen
    // has no author line at all, so nothing else in help.rs can show that the wrapper runs
    // over template-substituted content and not only over the argument table.
    //
    // Read the first two rows together. `{name} {version}` is ONE line before wrapping —
    // the version is not a separate field to the wrapper — which is why `1.0` lands on the
    // second row beside `long name hahaha` instead of on a line of its own.
    //
    // The options block below it stays COMPACT (`  -V, --version  Print version` is 30,
    // under 35), so this page carries both layouts at once: the header wrapped at 35 and
    // the table unwrapped. That is the pairing `final_word_wrapping` cannot show, since at
    // 24 columns everything is demoted together.
    //
    // `Usage: ctest` and not the command's own name: clap's `assert_output` passes
    // `"ctest --help"` as argv, and argv[0] becomes the bin name the usage line reports.
    // clapp spells the same input as `usage_name`.
    CLAPP_CHECK(same(page(issue_777_cmd, true, "ctest"),
                     "A cmd with a crazy very long long\n"
                     "long name hahaha 1.0\n"
                     "Some Very Long Name and crazy long\n"
                     "email <email@server.com>\n"
                     "Show how the about text is not\n"
                     "wrapped\n"
                     "\n"
                     "Usage: ctest\n"
                     "\n"
                     "Options:\n"
                     "  -h, --help     Print help\n"
                     "  -V, --version  Print version\n"));
}
