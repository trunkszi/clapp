#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/parsed_arg.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matcher.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/value_source.hpp>
#include <clapp/util/id.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef _WIN32
#    include <stdlib.h>  // setenv / unsetenv are POSIX, not in <cstdlib>
#endif

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_condition;
    using clapp::arg_flags;
    using clapp::arg_id;
    using clapp::arg_matches;
    using clapp::arg_setting;
    using clapp::arg_spec;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::count_type;
    using clapp::error;
    using clapp::error_kind;
    using clapp::group_builder;
    using clapp::matched_arg;
    using clapp::os_str;
    using clapp::os_string;
    using clapp::parse_error;
    using clapp::parsed_arg;
    using clapp::raw_args;
    using clapp::value_range;
    using clapp::value_source;

    using clapp::detail::arg_display;
    using clapp::detail::check_terminator;
    using clapp::detail::file_stem;
    using clapp::detail::has_last_positional;
    using clapp::detail::is_new_arg;
    using clapp::detail::last_positional;
    using clapp::detail::long_arg_at;
    using clapp::detail::positional_at;
    using clapp::detail::positional_count;
    using clapp::detail::prefers_long_form;
    using clapp::detail::short_arg_at;
    using clapp::detail::value_reason;

    // ---------------------------------------------------------------------------
    // Compile-time: what the loop delegates
    // ---------------------------------------------------------------------------
    //
    // clapp::parse() cannot be a constant expression — the assertion below is the reason the
    // runtime weighting in this file is a measured consequence rather than a preference.
    static_assert(!std::is_trivially_destructible_v<arg_matches>);
    static_assert(!std::is_trivially_destructible_v<clapp::any_value>);
    static_assert(std::is_trivially_destructible_v<command_spec>);
    // This asymmetry is the `command_of<T>()` boundary: the command tree is a `.rodata`
    // object, while matches are not and never can be.

    // Every spec here is built from string *literals*: under `-fsanitize=null` GCC 16.1.0 will
    // not fold libstdc++'s `basic_string(const CharT*, size_type)` for a variable source
    // pointer, and clapp::os_string reaches it. CLAUDE.md trap 10.

    consteval bool value_words_are_never_new_arguments() {
        const arg_spec plain{.id = arg_id{"file"}, .index = 1};
        const parsed_arg word{os_str{"notes.txt"}};
        const parsed_arg empty{os_str{""}};
        const parsed_arg stdio{os_str{"-"}};
        // A bare `-` is the Unix stdin placeholder and is a VALUE. Reading it as a flag is the
        // classic regression, and clapp::parsed_arg::is_short() already answers `false` for it.
        return !is_new_arg(word, plain) && !is_new_arg(empty, plain) && !is_new_arg(stdio, plain);
    }
    static_assert(value_words_are_never_new_arguments());

    consteval bool flags_are_new_arguments() {
        const arg_spec plain{.id = arg_id{"file"}, .index = 1};
        const parsed_arg long_flag{os_str{"--verbose"}};
        const parsed_arg short_flag{os_str{"-v"}};
        const parsed_arg cluster{os_str{"-abc"}};
        const parsed_arg number{os_str{"-3"}};
        // Without `allow_negative_numbers`, `-3` really is a flag spelling.
        return is_new_arg(long_flag, plain) && is_new_arg(short_flag, plain) &&
               is_new_arg(cluster, plain) && is_new_arg(number, plain);
    }
    static_assert(flags_are_new_arguments());

    consteval bool hyphen_values_beat_negative_numbers() {
        const arg_spec hyphens{.id       = arg_id{"rest"},
                               .index    = 1,
                               .settings = arg_flags{}.set(arg_setting::allow_hyphen_values)};
        const arg_spec numbers{.id       = arg_id{"delta"},
                               .index    = 1,
                               .settings = arg_flags{}.set(arg_setting::allow_negative_numbers)};
        const parsed_arg flag{os_str{"-v"}};
        const parsed_arg number{os_str{"-3.5"}};
        const parsed_arg long_flag{os_str{"--weird"}};
        // The distinction that matters: `allow_hyphen_values` takes `-v`, and
        // `allow_negative_numbers` refuses it. Making the second behave like the first is a
        // one-word edit that no runtime case for numbers would catch.
        return !is_new_arg(flag, hyphens) && !is_new_arg(number, hyphens) &&
               !is_new_arg(long_flag, hyphens) && is_new_arg(flag, numbers) &&
               !is_new_arg(number, numbers) && is_new_arg(long_flag, numbers);
    }
    static_assert(hyphen_values_beat_negative_numbers());

    consteval bool a_terminator_is_matched_exactly() {
        const arg_spec listed{.id = arg_id{"args"}, .index = 1, .terminator = arg_id{";"}};
        const arg_spec plain{.id = arg_id{"args"}, .index = 1};
        return check_terminator(listed, os_str{";"}) && !check_terminator(listed, os_str{";;"}) &&
               !check_terminator(listed, os_str{""}) && !check_terminator(plain, os_str{";"});
    }
    static_assert(a_terminator_is_matched_exactly());

    // The positional slot table the loop walks. `flag` has no index and must never answer.
    static constexpr arg_spec slot_table[] = {
            arg_spec{.id = arg_id{"flag"}, .short_ = 'f'},
            arg_spec{.id = arg_id{"src"}, .index = 1},
            arg_spec{.id       = arg_id{"dst"},
                     .index    = 2,
                     .settings = arg_flags{}.set(arg_setting::last)},
    };
    constexpr command_spec slot_cmd{.name = arg_id{"demo"}, .arg_data = slot_table, .arg_count = 3};

    consteval bool slots_are_dense_and_one_based() {
        return positional_at(slot_cmd, 0) == nullptr && positional_at(slot_cmd, 3) == nullptr &&
               positional_at(slot_cmd, 1)->get_id() == "src" &&
               positional_at(slot_cmd, 2)->get_id() == "dst" && positional_count(slot_cmd) == 2 &&
               last_positional(slot_cmd)->get_id() == "dst" && has_last_positional(slot_cmd);
    }
    static_assert(slots_are_dense_and_one_based());

    // Aliases are part of the lookup, because clap's keymap indexes them. An implementation
    // comparing get_long() alone rejects every alias with no diagnostic at all.
    static constexpr clapp::alias_spec alias_table[] = {clapp::alias_spec{.name = arg_id{"loud"}}};
    static constexpr clapp::short_alias_spec short_alias_table[] = {
            clapp::short_alias_spec{.name = 'V'}};
    static constexpr arg_spec alias_args[] = {
            arg_spec{.id                = arg_id{"verbose"},
                     .short_            = 'v',
                     .long_             = arg_id{"verbose"},
                     .alias_data        = alias_table,
                     .alias_count       = 1,
                     .short_alias_data  = short_alias_table,
                     .short_alias_count = 1},
    };
    constexpr command_spec alias_cmd{
            .name = arg_id{"demo"}, .arg_data = alias_args, .arg_count = 1};

    consteval bool lookups_are_alias_aware() {
        return long_arg_at(alias_cmd, "verbose")->get_id() == "verbose" &&
               long_arg_at(alias_cmd, "loud")->get_id() == "verbose" &&
               long_arg_at(alias_cmd, "verb") == nullptr &&
               short_arg_at(alias_cmd, 'v')->get_id() == "verbose" &&
               short_arg_at(alias_cmd, 'V')->get_id() == "verbose" &&
               short_arg_at(alias_cmd, '\0') == nullptr;
    }
    static_assert(lookups_are_alias_aware());

    // clapp::detail::arg_display() is the text every diagnostic quotes. Asserting it here means
    // a change to the rendering fails to compile rather than silently rewording twelve errors.
    consteval bool arguments_name_themselves_as_clap_does() {
        const arg_spec flag{.id       = arg_id{"verbose"},
                            .short_   = 'v',
                            .long_    = arg_id{"verbose"},
                            .num_args = value_range::empty()};
        const arg_spec counter{.id       = arg_id{"verbose"},
                               .short_   = 'v',
                               .act      = arg_action::count,
                               .num_args = value_range::empty()};
        const arg_spec option{.id = arg_id{"port"}, .long_ = arg_id{"port"}};
        // The id and the long spelling routinely DIFFER — `no_color` is spelled
        // `--no-color` — so a rendering that reached for the id looks right on every
        // argument whose two names happen to coincide.
        const arg_spec renamed{.id = arg_id{"no_color"}, .long_ = arg_id{"no-color"}};
        const arg_spec short_only{.id = arg_id{"jobs"}, .short_ = 'j'};
        const arg_spec pair{.id       = arg_id{"point"},
                            .long_    = arg_id{"point"},
                            .num_args = value_range::exactly(2)};
        const arg_spec many{.id       = arg_id{"path"},
                            .long_    = arg_id{"path"},
                            .num_args = value_range::at_least(1)};
        return arg_display(flag) == "--verbose" && arg_display(counter) == "-v..." &&
               arg_display(option) == "--port <port>" &&
               arg_display(renamed) == "--no-color <no_color>" &&
               arg_display(short_only) == "-j <jobs>" &&
               arg_display(pair) == "--point <point> <point>" &&
               arg_display(many) == "--path <path>...";
    }
    static_assert(arguments_name_themselves_as_clap_does());

    consteval bool positionals_name_themselves_by_requiredness() {
        const arg_spec needed{.id       = arg_id{"file"},
                              .index    = 1,
                              .settings = arg_flags{}.set(arg_setting::required)};
        const arg_spec spare{.id = arg_id{"file"}, .index = 1};
        return arg_display(needed) == "<file>" && arg_display(spare) == "[file]";
    }
    static_assert(positionals_name_themselves_by_requiredness());

    // clapp::detail::value_reason() is the sentence after the colon in
    // `invalid value 'X' for '--opt <OPT>': …`, and the ONLY reader of
    // clapp::parse_error::domain. Before it existed the field was computed for every integer
    // failure and read by nothing, so `--port 99999` said `number too large to fit in target
    // type` where clap says `99999 is not in 0..=65535` — the type's own limits, which the
    // user cannot otherwise discover. Each expectation was measured against clap 4.6 with
    // `value_parser!(u16)` and the same argv.
    // Every argument below is bound to a named object before the call: GCC 16.1.0 miscompiles
    // a `constexpr` function invoked directly on string literals whose result is then stored,
    // and this file's own header already warns about it. CLAUDE.md, known workarounds.
    consteval bool a_range_failure_names_the_range() {
        const std::string_view big_text{"99999"};
        const std::string_view small_text{"-9999999999"};
        const parse_error too_big{.kind   = clapp::parse_error_kind::out_of_range,
                                  .domain = "0..=65535"};
        const parse_error too_small{.kind   = clapp::parse_error_kind::out_of_range,
                                    .domain = "-2147483648..=2147483647"};
        return value_reason(big_text, too_big) == "99999 is not in 0..=65535" &&
               value_reason(small_text, too_small) ==
                       "-9999999999 is not in -2147483648..=2147483647";
    }
    static_assert(a_range_failure_names_the_range());

    // Rung 1 outranks rung 2: a parser that wrote its own sentence keeps it, even when it
    // ALSO named an interval. `examples/tutorial/*/05_validation.cpp` does exactly that, and
    // its whole point is the sentence — synthesizing over it silently rewords every custom
    // parser in the wild. (Found by running the suite: putting the domain first broke both
    // tutorial e2e tests, and nothing else.)
    consteval bool a_parser_that_wrote_a_sentence_keeps_it() {
        const std::string_view text{"65536"};
        const parse_error tutorial{.kind   = clapp::parse_error_kind::out_of_range,
                                   .reason = "port not in range 1-65535",
                                   .domain = "1..=65535"};
        return value_reason(text, tutorial) == "port not in range 1-65535";
    }
    static_assert(a_parser_that_wrote_a_sentence_keeps_it());

    // Everything else keeps the parser's own static sentence, and so does a range failure
    // with no interval to name — which is value_parser<F>, whose overflow is a recorded
    // divergence (clap saturates `1e400` to `inf`) with no bounds to print.
    consteval bool every_other_failure_keeps_its_own_sentence() {
        const parse_error empty{.kind   = clapp::parse_error_kind::invalid_digit,
                                .reason = "cannot parse integer from empty string",
                                .domain = "0..=65535"};
        const parse_error digit{.kind   = clapp::parse_error_kind::invalid_digit,
                                .reason = "invalid digit found in string",
                                .domain = "0..=65535"};
        const parse_error unbounded{.kind   = clapp::parse_error_kind::out_of_range,
                                    .reason = "number is outside the representable range"};
        const parse_error wordless{.kind = clapp::parse_error_kind::out_of_range};
        const std::string_view nothing{""};
        const std::string_view hex{"0x2A"};
        const std::string_view huge{"1e400"};
        return value_reason(nothing, empty) == "cannot parse integer from empty string" &&
               value_reason(hex, digit) == "invalid digit found in string" &&
               value_reason(huge, unbounded) == "number is outside the representable range" &&
               value_reason(huge, wordless) == "value is out of range for its type";
    }
    static_assert(every_other_failure_keeps_its_own_sentence());

    // Only an explicit `-h` asks for short help. clap maps Index and None to the long form,
    // which is easy to invert and produces `-h` output for `--help`.
    static_assert(!prefers_long_form(clapp::detail::arg_identifier::short_));
    static_assert(prefers_long_form(clapp::detail::arg_identifier::long_));
    static_assert(prefers_long_form(clapp::detail::arg_identifier::index));
    static_assert(prefers_long_form(std::nullopt));

    // The multicall applet name is Rust's `Path::file_stem`: directory AND extension go.
    static_assert(file_stem("/usr/bin/busybox") == "busybox");
    static_assert(file_stem("./bin/my_prog.exe") == "my_prog");
    static_assert(file_stem("C:\\tools\\hush.exe") == "hush");
    static_assert(file_stem(".bashrc") == ".bashrc");
    static_assert(file_stem("") == "");

    // The arithmetic behind every arity case below, spelled on clapp::value_range so each
    // expected value downstream is traceable to one line here.
    static_assert(!value_range::empty().accepts_more(0));
    static_assert(value_range::exactly(2).accepts_more(1));
    static_assert(!value_range::exactly(2).accepts_more(2));
    static_assert(value_range::at_least(1).accepts_more(99));
    static_assert(value_range::optional().min_values() == 0);
    static_assert(value_range::exactly(2).num_values() == std::optional<std::size_t>{2});
    static_assert(!value_range::at_least(1).num_values().has_value());

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    // The arity fixture: a flag, a counter, a single-valued option, a fixed pair, an unbounded
    // list, an appender, and one positional. Everything the pending machinery must tell apart.
    consteval command_spec make_basic() {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("verbose").short_('v').long_("verbose").action(arg_action::count))
                .arg(arg_builder("quiet").short_('q').long_("quiet").action(arg_action::set_true))
                .arg(arg_builder("port").short_('p').long_("port").value_parser<int>())
                .arg(arg_builder("point").long_("point").num_args(value_range::exactly(2)))
                .arg(arg_builder("files").long_("files").num_args(value_range::at_least(1)))
                .arg(arg_builder("define").short_('D').action(arg_action::append))
                .arg(arg_builder("input").index(1));
        return app.freeze();
    }
    constexpr command_spec basic = make_basic();

    // The fixture really does carry those arities; a builder-side change that resolved them
    // differently must fail to compile rather than make the cases below vacuous.
    static_assert(basic.find_arg("point")->get_num_args() == value_range::exactly(2));
    static_assert(basic.find_arg("files")->get_num_args() == value_range::at_least(1));
    static_assert(!basic.find_arg("verbose")->get_num_args().takes_values());
    static_assert(basic.find_arg("define")->get_action() == arg_action::append);
    static_assert(basic.find_arg("input")->get_index() == std::optional<std::size_t>{1});

    // Subcommands: an alias, inference, a `--long`/`-S` flag subcommand, and a global.
    consteval command_spec make_tree() {
        command_builder app("git");
        std::move(app)
                .infer_subcommands()
                .arg(arg_builder("global").short_('g').long_("global").global().action(
                        arg_action::set_true))
                .subcommand(command_builder("commit")
                                    .short_flag('C')
                                    .long_flag("commit")
                                    .arg(arg_builder("message").short_('m').long_("message"))
                                    .subcommand(command_builder("amend")))
                .subcommand(command_builder("checkout").alias("co"))
                .subcommand(command_builder("stash"));
        return app.freeze();
    }
    constexpr command_spec tree = make_tree();

    static_assert(tree.is_infer_subcommands_set());
    static_assert(tree.find_arg("global")->is_global_set());
    static_assert(tree.find_subcommand("co")->get_name() == "checkout");

    // Sources: a default, a conditional default, an environment variable, a
    // `default_missing_value`, and a delimiter.
    consteval command_spec make_sources() {
        command_builder app("src");
        std::move(app)
                .arg(arg_builder("level").long_("level").default_value("info"))
                .arg(arg_builder("mode").long_("mode"))
                .arg(arg_builder("boost").long_("boost").default_value_if(
                        "mode", arg_condition::equal_to("fast"), "9"))
                .arg(arg_builder("seen").long_("seen").default_value_if(
                        "mode", arg_condition::present(), "yes"))
                .arg(arg_builder("home").long_("home").env("CLAPP_PARSE_TEST_HOME"))
                .arg(arg_builder("color")
                             .long_("color")
                             .num_args(value_range::optional())
                             .default_missing_value("always"))
                .arg(arg_builder("paths").long_("paths").value_delimiter(',').num_args(
                        value_range::at_least(1)));
        return app.freeze();
    }
    constexpr command_spec sources = make_sources();

    // Hyphen handling, terminators, `last(true)` and overrides.
    consteval command_spec make_edges() {
        command_builder app("edge");
        std::move(app)
                .arg(arg_builder("color")
                             .long_("color")
                             .action(arg_action::set_true)
                             .overrides_with("no_color"))
                .arg(arg_builder("no_color").long_("no-color").action(arg_action::set_true))
                .arg(arg_builder("delta").long_("delta").allow_negative_numbers())
                .arg(arg_builder("cmd")
                             .long_("cmd")
                             .num_args(value_range::at_least(1))
                             .value_terminator(";"))
                .arg(arg_builder("rest").index(1).num_args(value_range::at_least(0)).last());
        return app.freeze();
    }
    constexpr command_spec edges = make_edges();

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    using outcome = std::expected<arg_matches, error>;

    // find_arg(), never contains_id(): a subcommand's arg_matches validates ids against its
    // OWN command and aborts on anything else, so a generic walk must not ask that question.
    bool present(const arg_matches& matches, std::string_view id) {
        return matches.find_arg(id) != nullptr;
    }

    std::vector<std::string> raw_of(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    std::string one_of(const arg_matches& matches, std::string_view id) {
        const std::vector<std::string> all = raw_of(matches, id);
        return all.size() == 1 ? all.front() : std::string{};
    }

    std::size_t occurrences_of(const arg_matches& matches, std::string_view id) {
        const matched_arg* found = matches.find_arg(id);
        return found == nullptr ? 0 : found->occurrence_count();
    }

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::string message_of(const outcome& got) {
        return got.has_value() ? std::string{} : got.error().render().to_string();
    }

}  // namespace

// ---------------------------------------------------------------------------
// Flags and counters
// ---------------------------------------------------------------------------

CLAPP_TEST("a long flag sets its argument") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--quiet"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("quiet"));
    CLAPP_CHECK(one_of(*got, "quiet") == "true");
}

CLAPP_TEST("an absent set_true flag still reads false, from its injected default") {
    const outcome got = clapp::parse(basic, raw_args{"app"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->get_flag("quiet"));
    // clap gives every SetTrue argument `default_value("false")`, so the entry EXISTS but
    // its source is the default rather than the command line.
    CLAPP_CHECK(present(*got, "quiet"));
    CLAPP_CHECK(got->value_source("quiet") == value_source::default_value);
}

CLAPP_TEST("a short cluster fires every flag in it") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-qv"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("quiet"));
    CLAPP_CHECK(got->get_count("verbose") == count_type{1});
}

CLAPP_TEST("a counter counts occurrences, not values") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-vvv"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_count("verbose") == count_type{3});
    // Each occurrence REPLACES the stored count; three sightings leave one value.
    CLAPP_CHECK(raw_of(*got, "verbose").size() == 1);
}

CLAPP_TEST("a counter reads zero when absent") {
    const outcome got = clapp::parse(basic, raw_args{"app"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_count("verbose") == count_type{0});
    CLAPP_CHECK(got->value_source("verbose") == value_source::default_value);
}

CLAPP_TEST("mixed long and short spellings of one counter accumulate") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-v", "--verbose", "-vv"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_count("verbose") == count_type{4});
}

// ---------------------------------------------------------------------------
// Options and the four spellings of a value
// ---------------------------------------------------------------------------

CLAPP_TEST("a long option takes the next token") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--port", "8080"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_of(*got, "port") == "8080");
    CLAPP_CHECK(*got->get_one<int>("port").value() == 8080);
}

CLAPP_TEST("all four value spellings agree") {
    // `--port 8080`, `--port=8080`, `-p 8080`, `-p8080` are the same argument, and the
    // attached forms travel a completely different code path from the detached ones.
    const outcome detached_long  = clapp::parse(basic, raw_args{"app", "--port", "8080"});
    const outcome attached_long  = clapp::parse(basic, raw_args{"app", "--port=8080"});
    const outcome detached_short = clapp::parse(basic, raw_args{"app", "-p", "8080"});
    const outcome attached_short = clapp::parse(basic, raw_args{"app", "-p8080"});
    const outcome equals_short   = clapp::parse(basic, raw_args{"app", "-p=8080"});
    CLAPP_CHECK(detached_long.has_value() && attached_long.has_value());
    CLAPP_CHECK(detached_short.has_value() && attached_short.has_value());
    CLAPP_CHECK(equals_short.has_value());
    CLAPP_CHECK(one_of(*detached_long, "port") == "8080");
    CLAPP_CHECK(one_of(*attached_long, "port") == "8080");
    CLAPP_CHECK(one_of(*detached_short, "port") == "8080");
    CLAPP_CHECK(one_of(*attached_short, "port") == "8080");
    CLAPP_CHECK(one_of(*equals_short, "port") == "8080");
}

CLAPP_TEST("a value-taking short ends its cluster") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-vp", "80"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_count("verbose") == count_type{1});
    CLAPP_CHECK(one_of(*got, "port") == "80");
}

CLAPP_TEST("repeating a `set` option without overrides_with is a conflict") {
    // clap's ArgumentConflict, and the reason `args_override_self()` exists.
    const outcome got = clapp::parse(basic, raw_args{"app", "--port", "1", "--port", "2"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
    CLAPP_CHECK(message_of(got).find("--port <port>") != std::string::npos);
}

CLAPP_TEST("append accumulates occurrences instead of conflicting") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-D", "a=1", "-D", "b=2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "define") == std::vector<std::string>{"a=1", "b=2"});
    CLAPP_CHECK(occurrences_of(*got, "define") == 2);
}

CLAPP_TEST("a bad value names the argument and the parser's own reason") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--port", "eighty"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(message_of(got).find("eighty") != std::string::npos);
    CLAPP_CHECK(message_of(got).find("--port <port>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Arity — the decision this whole module exists for
// ---------------------------------------------------------------------------

CLAPP_TEST("a fixed pair takes exactly two tokens") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--point", "3", "4"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "point") == std::vector<std::string>{"3", "4"});
}

CLAPP_TEST("a fixed pair with one value is wrong_number_of_values, not too_few") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--point", "3"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::wrong_number_of_values);
    CLAPP_CHECK(message_of(got).find("2 values required") != std::string::npos);
}

CLAPP_TEST("a fixed pair stops after two, and the third token is a positional") {
    // NOT `too_many_values`: the option closed itself at two, so `5` reaches the
    // positional slot. This is the single most load-bearing consequence of reading the
    // PENDING count rather than the committed one.
    const outcome got = clapp::parse(basic, raw_args{"app", "--point", "3", "4", "5"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "point") == std::vector<std::string>{"3", "4"});
    CLAPP_CHECK(one_of(*got, "input") == "5");
}

CLAPP_TEST("an unbounded option swallows every following value") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--files", "a", "b", "c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == std::vector<std::string>{"a", "b", "c"});
    // ... and therefore leaves the positional empty.
    CLAPP_CHECK(!present(*got, "input"));
}

CLAPP_TEST("an unbounded option stops at the next flag") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--files", "a", "b", "--quiet"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "files") == std::vector<std::string>{"a", "b"});
    CLAPP_CHECK(got->get_flag("quiet"));
}

CLAPP_TEST("an unbounded option with zero values is empty_value, not too_few_values") {
    // clap issues 665 and 1105: `min_values > 0` with nothing supplied is its own error.
    const outcome got = clapp::parse(basic, raw_args{"app", "--files", "--quiet"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
    CLAPP_CHECK(message_of(got).find("--files") != std::string::npos);
}

CLAPP_TEST("an unbounded minimum of two reports too_few_values, not wrong_number") {
    // `2..` has no exact arity, so it takes the `actual < min_values()` branch — the one
    // arity path that `exactly(N)` and `1..` between them never reach.
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).arg(arg_builder("pairs").long_("pairs").num_args(value_range::at_least(2)));
        return app.freeze();
    }();
    const outcome enough = clapp::parse(spec, raw_args{"app", "--pairs", "a", "b", "c"});
    CLAPP_CHECK(enough.has_value());
    CLAPP_CHECK(raw_of(*enough, "pairs") == std::vector<std::string>{"a", "b", "c"});

    const outcome short_one = clapp::parse(spec, raw_args{"app", "--pairs", "a"});
    CLAPP_CHECK(!short_one.has_value());
    CLAPP_CHECK(kind_of(short_one) == error_kind::too_few_values);
    CLAPP_CHECK(message_of(short_one).find("2 values required") != std::string::npos);
}

CLAPP_TEST("a value attached to a flag that takes none is too_many_values") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--quiet=loud"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::too_many_values);
    CLAPP_CHECK(message_of(got).find("loud") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Positionals
// ---------------------------------------------------------------------------

CLAPP_TEST("a bare word lands on the positional slot") {
    const outcome got = clapp::parse(basic, raw_args{"app", "notes.txt"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_of(*got, "input") == "notes.txt");
}

CLAPP_TEST("a second bare word has nowhere to go") {
    const outcome got = clapp::parse(basic, raw_args{"app", "one", "two"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).find("two") != std::string::npos);
}

CLAPP_TEST("a bare `-` is a value, not a flag") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_of(*got, "input") == "-");
}

CLAPP_TEST("indices count values, so flags and positionals interleave") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--port", "1", "file"});
    CLAPP_CHECK(got.has_value());
    // `--port` claims index 1 for the flag itself and index 2 for its value; the
    // positional then takes index 3.
    CLAPP_CHECK(got->index_of("port") == std::optional<std::size_t>{2});
    CLAPP_CHECK(got->index_of("input") == std::optional<std::size_t>{3});
}

CLAPP_TEST("a low-index multiple looks ahead to leave the last slot filled") {
    // `<first>... <second>`: the parser cannot know where `first` ends without peeking.
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("first").index(1).num_args(value_range::at_least(1)).required())
                .arg(arg_builder("second").index(2).required());
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "a", "b", "c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "first") == std::vector<std::string>{"a", "b"});
    CLAPP_CHECK(one_of(*got, "second") == "c");
}

CLAPP_TEST("allow_missing_positional skips the earlier slot") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .allow_missing_positional()
                .arg(arg_builder("src").index(1))
                .arg(arg_builder("dst").index(2).required());
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "only"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!present(*got, "src"));
    CLAPP_CHECK(one_of(*got, "dst") == "only");
}

// ---------------------------------------------------------------------------
// `--`, trailing values, and `last(true)`
// ---------------------------------------------------------------------------

CLAPP_TEST("after `--` a leading dash is data") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--", "--quiet"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_of(*got, "input") == "--quiet");
    CLAPP_CHECK(!got->get_flag("quiet"));
}

CLAPP_TEST("a second `--` is itself a value") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).arg(arg_builder("rest").index(1).num_args(value_range::at_least(0)));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--", "a", "--", "b"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "rest") == std::vector<std::string>{"a", "--", "b"});
}

CLAPP_TEST("a last(true) positional is unreachable without `--`") {
    const outcome without = clapp::parse(edges, raw_args{"edge", "x"});
    CLAPP_CHECK(!without.has_value());
    CLAPP_CHECK(kind_of(without) == error_kind::unknown_argument);

    const outcome with = clapp::parse(edges, raw_args{"edge", "--", "x", "y"});
    CLAPP_CHECK(with.has_value());
    CLAPP_CHECK(raw_of(*with, "rest") == std::vector<std::string>{"x", "y"});
}

CLAPP_TEST("trailing_var_arg turns the positional into a command line of its own") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("flag").short_('f').action(arg_action::set_true))
                .arg(arg_builder("cmd")
                             .index(1)
                             .num_args(value_range::at_least(1))
                             .trailing_var_arg());
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "-f", "prog", "-x", "--long"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(raw_of(*got, "cmd") == std::vector<std::string>{"prog", "-x", "--long"});
}

CLAPP_TEST("a value terminator closes an unbounded option") {
    const outcome got =
            clapp::parse(edges, raw_args{"edge", "--cmd", "a", "b", ";", "--delta", "-3"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "cmd") == std::vector<std::string>{"a", "b"});
    CLAPP_CHECK(one_of(*got, "delta") == "-3");
}

// ---------------------------------------------------------------------------
// Hyphen values and negative numbers
// ---------------------------------------------------------------------------

CLAPP_TEST("allow_negative_numbers takes a number but still refuses a flag") {
    const outcome number = clapp::parse(edges, raw_args{"edge", "--delta", "-3.5"});
    CLAPP_CHECK(number.has_value());
    CLAPP_CHECK(one_of(*number, "delta") == "-3.5");

    const outcome flag = clapp::parse(edges, raw_args{"edge", "--delta", "--color"});
    CLAPP_CHECK(!flag.has_value());
    // `--color` was read as a flag, so `--delta` got nothing.
    CLAPP_CHECK(kind_of(flag) == error_kind::invalid_value);
}

CLAPP_TEST("allow_hyphen_values on a positional swallows flag-shaped tokens") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("verbose").short_('v').action(arg_action::set_true))
                .arg(arg_builder("args")
                             .index(1)
                             .num_args(value_range::at_least(0))
                             .allow_hyphen_values());
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "-v", "--weird", "-x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("verbose"));
    CLAPP_CHECK(raw_of(*got, "args") == std::vector<std::string>{"--weird", "-x"});
}

// ---------------------------------------------------------------------------
// require_equals
// ---------------------------------------------------------------------------

CLAPP_TEST("require_equals rejects the detached spelling") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).arg(arg_builder("eq").long_("eq").require_equals());
        return app.freeze();
    }();
    const outcome attached = clapp::parse(spec, raw_args{"app", "--eq=v"});
    CLAPP_CHECK(attached.has_value());
    CLAPP_CHECK(one_of(*attached, "eq") == "v");

    const outcome detached = clapp::parse(spec, raw_args{"app", "--eq", "v"});
    CLAPP_CHECK(!detached.has_value());
    CLAPP_CHECK(kind_of(detached) == error_kind::no_equals);
    // The rendering shows the `=`, which is the whole point of the message.
    CLAPP_CHECK(message_of(detached).find("--eq=<eq>") != std::string::npos);
}

CLAPP_TEST("require_equals with an optional value fires bare") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).arg(
                arg_builder("opt").long_("opt").require_equals().num_args(value_range::optional()));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--opt"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(present(*got, "opt"));
    CLAPP_CHECK(raw_of(*got, "opt").empty());
}

// ---------------------------------------------------------------------------
// Value sources, in precedence order
// ---------------------------------------------------------------------------

CLAPP_TEST("a default_value applies only when nothing else did") {
    const outcome bare = clapp::parse(sources, raw_args{"src"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(one_of(*bare, "level") == "info");
    CLAPP_CHECK(bare->value_source("level") == value_source::default_value);

    const outcome typed = clapp::parse(sources, raw_args{"src", "--level", "debug"});
    CLAPP_CHECK(typed.has_value());
    CLAPP_CHECK(one_of(*typed, "level") == "debug");
    CLAPP_CHECK(typed->value_source("level") == value_source::command_line);
}

CLAPP_TEST("default_value_if fires on a value match and on mere presence") {
    const outcome fast = clapp::parse(sources, raw_args{"src", "--mode", "fast"});
    CLAPP_CHECK(fast.has_value());
    CLAPP_CHECK(one_of(*fast, "boost") == "9");
    CLAPP_CHECK(one_of(*fast, "seen") == "yes");

    const outcome slow = clapp::parse(sources, raw_args{"src", "--mode", "slow"});
    CLAPP_CHECK(slow.has_value());
    CLAPP_CHECK(!present(*slow, "boost"));        // the value predicate did not match
    CLAPP_CHECK(one_of(*slow, "seen") == "yes");  // presence alone did

    const outcome none = clapp::parse(sources, raw_args{"src"});
    CLAPP_CHECK(none.has_value());
    CLAPP_CHECK(!present(*none, "boost"));
    CLAPP_CHECK(!present(*none, "seen"));
}

CLAPP_TEST("a conditional default with no values REMOVES the plain default") {
    // clap's `Some(_)` versus `None`, and the reason clapp::default_value_spec carries an
    // explicit `values_present` flag. A rule that fires and supplies nothing must STOP the
    // scan — falling through to the unconditional default would defeat the whole point.
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("mode").long_("mode"))
                .arg(arg_builder("level").long_("level").default_value("plain").default_value_if(
                        "mode", arg_condition::present(), std::optional<std::string_view>{}));
        return app.freeze();
    }();
    const outcome suppressed = clapp::parse(spec, raw_args{"app", "--mode", "x"});
    CLAPP_CHECK(suppressed.has_value());
    CLAPP_CHECK(!present(*suppressed, "level"));

    const outcome plain = clapp::parse(spec, raw_args{"app"});
    CLAPP_CHECK(plain.has_value());
    CLAPP_CHECK(one_of(*plain, "level") == "plain");
}

CLAPP_TEST("default_missing_value fills an option that was present but empty") {
    const outcome bare = clapp::parse(sources, raw_args{"src", "--color"});
    CLAPP_CHECK(bare.has_value());
    CLAPP_CHECK(one_of(*bare, "color") == "always");
    CLAPP_CHECK(bare->value_source("color") == value_source::command_line);

    const outcome given = clapp::parse(sources, raw_args{"src", "--color", "never"});
    CLAPP_CHECK(given.has_value());
    CLAPP_CHECK(one_of(*given, "color") == "never");

    const outcome absent = clapp::parse(sources, raw_args{"src"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!present(*absent, "color"));
}

#ifndef _WIN32
CLAPP_TEST("the environment beats a default and loses to the command line") {
    ::setenv("CLAPP_PARSE_TEST_HOME", "/from/env", 1);

    const outcome from_env = clapp::parse(sources, raw_args{"src"});
    CLAPP_CHECK(from_env.has_value());
    CLAPP_CHECK(one_of(*from_env, "home") == "/from/env");
    CLAPP_CHECK(from_env->value_source("home") == value_source::env_variable);
    // "Explicit" means "not a default", so an environment value satisfies a predicate.
    CLAPP_CHECK(clapp::is_explicit(value_source::env_variable));

    const outcome from_cli = clapp::parse(sources, raw_args{"src", "--home", "/from/cli"});
    CLAPP_CHECK(from_cli.has_value());
    CLAPP_CHECK(one_of(*from_cli, "home") == "/from/cli");
    CLAPP_CHECK(from_cli->value_source("home") == value_source::command_line);

    ::unsetenv("CLAPP_PARSE_TEST_HOME");
    const outcome unset = clapp::parse(sources, raw_args{"src"});
    CLAPP_CHECK(unset.has_value());
    CLAPP_CHECK(!present(*unset, "home"));
}
#endif

CLAPP_TEST("a value delimiter splits attached and detached values alike") {
    const outcome attached = clapp::parse(sources, raw_args{"src", "--paths=a,b,c"});
    CLAPP_CHECK(attached.has_value());
    CLAPP_CHECK(raw_of(*attached, "paths") == std::vector<std::string>{"a", "b", "c"});

    const outcome detached = clapp::parse(sources, raw_args{"src", "--paths", "a,b"});
    CLAPP_CHECK(detached.has_value());
    CLAPP_CHECK(raw_of(*detached, "paths") == std::vector<std::string>{"a", "b"});

    const outcome plain = clapp::parse(sources, raw_args{"src", "--paths", "solo"});
    CLAPP_CHECK(plain.has_value());
    CLAPP_CHECK(raw_of(*plain, "paths") == std::vector<std::string>{"solo"});
}

CLAPP_TEST("dont_delimit_trailing_values exempts what followed a `--`") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).dont_delimit_trailing_values().arg(
                arg_builder("paths")
                        .index(1)
                        .num_args(value_range::at_least(1))
                        .value_delimiter(','));
        return app.freeze();
    }();
    const outcome leading = clapp::parse(spec, raw_args{"app", "a,b"});
    CLAPP_CHECK(leading.has_value());
    CLAPP_CHECK(raw_of(*leading, "paths") == std::vector<std::string>{"a", "b"});

    // After `--` the comma is data, which is the whole point of the setting.
    const outcome trailing = clapp::parse(spec, raw_args{"app", "--", "a,b"});
    CLAPP_CHECK(trailing.has_value());
    CLAPP_CHECK(raw_of(*trailing, "paths") == std::vector<std::string>{"a,b"});
}

// ---------------------------------------------------------------------------
// overrides_with
// ---------------------------------------------------------------------------

CLAPP_TEST("overrides_with is symmetric in effect and asymmetric in declaration") {
    // Only `--color` declares the override, yet either order must leave exactly one of
    // them set on the command line. The reverse direction is the transitive sweep.
    const outcome forward = clapp::parse(edges, raw_args{"edge", "--no-color", "--color"});
    CLAPP_CHECK(forward.has_value());
    CLAPP_CHECK(forward->get_flag("color"));
    CLAPP_CHECK(!forward->get_flag("no_color"));
    CLAPP_CHECK(forward->value_source("no_color") == value_source::default_value);

    const outcome backward = clapp::parse(edges, raw_args{"edge", "--color", "--no-color"});
    CLAPP_CHECK(backward.has_value());
    CLAPP_CHECK(!backward->get_flag("color"));
    CLAPP_CHECK(backward->get_flag("no_color"));
    CLAPP_CHECK(backward->value_source("color") == value_source::default_value);
}

CLAPP_TEST("args_override_self turns a repeat into last-one-wins") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).args_override_self().arg(arg_builder("port").long_("port"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--port", "1", "--port", "2"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_of(*got, "port") == "2");
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

CLAPP_TEST("a group records which member matched, and only for explicit sources") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("alpha").long_("alpha"))
                .arg(arg_builder("beta").long_("beta").default_value("d"))
                .group(group_builder("pick").arg("alpha").arg("beta"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--alpha", "1"});
    CLAPP_CHECK(got.has_value());
    const std::optional<clapp::values_ref<arg_id>> members = got->get_many<arg_id>("pick");
    CLAPP_CHECK(members.has_value());
    CLAPP_CHECK(std::ranges::distance(*members) == 1);
    CLAPP_CHECK((*std::ranges::begin(*members)).name() == "alpha");
    // `beta` only got its default, so it did not occupy the group.
    CLAPP_CHECK(one_of(*got, "beta") == "d");
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

CLAPP_TEST("an exact subcommand name and an alias reach the same child") {
    const outcome named = clapp::parse(tree, raw_args{"git", "checkout"});
    CLAPP_CHECK(named.has_value());
    CLAPP_CHECK(named->subcommand_name() == std::optional<std::string_view>{"checkout"});

    const outcome aliased = clapp::parse(tree, raw_args{"git", "co"});
    CLAPP_CHECK(aliased.has_value());
    // The CANONICAL name is recorded, never the alias the user typed.
    CLAPP_CHECK(aliased->subcommand_name() == std::optional<std::string_view>{"checkout"});
}

CLAPP_TEST("infer_subcommands accepts a unique prefix and refuses an ambiguous one") {
    const outcome unique = clapp::parse(tree, raw_args{"git", "sta"});
    CLAPP_CHECK(unique.has_value());
    CLAPP_CHECK(unique->subcommand_name() == std::optional<std::string_view>{"stash"});

    // `c` prefixes both `commit` and `checkout`; returning the first is the tempting bug.
    const outcome ambiguous = clapp::parse(tree, raw_args{"git", "c"});
    CLAPP_CHECK(!ambiguous.has_value());
    CLAPP_CHECK(kind_of(ambiguous) == error_kind::invalid_subcommand);
}

CLAPP_TEST("a subcommand's arguments are parsed by the subcommand") {
    const outcome got = clapp::parse(tree, raw_args{"git", "commit", "-m", "hello"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* child = got->subcommand_matches("commit");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(one_of(*child, "message") == "hello");
    // The parent knows nothing about `message`.
    CLAPP_CHECK(!present(*got, "message"));
}

CLAPP_TEST("subcommands nest") {
    const outcome got = clapp::parse(tree, raw_args{"git", "commit", "amend"});
    CLAPP_CHECK(got.has_value());
    const arg_matches* child = got->subcommand_matches("commit");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(child->subcommand_name() == std::optional<std::string_view>{"amend"});
}

CLAPP_TEST("a long-flag subcommand and a short-flag subcommand select the same child") {
    const outcome long_form = clapp::parse(tree, raw_args{"git", "--commit", "-m", "x"});
    CLAPP_CHECK(long_form.has_value());
    CLAPP_CHECK(long_form->subcommand_name() == std::optional<std::string_view>{"commit"});

    const outcome short_form = clapp::parse(tree, raw_args{"git", "-C", "-m", "x"});
    CLAPP_CHECK(short_form.has_value());
    CLAPP_CHECK(short_form->subcommand_name() == std::optional<std::string_view>{"commit"});
}

CLAPP_TEST("a flag subcommand in the middle of a cluster keeps the earlier flags") {
    // `-gC` is `-g` plus the flag subcommand `-C`. The cursor steps back and the child
    // re-reads the cluster with the already-consumed letters skipped; getting the skip
    // count wrong re-fires `-g` or drops it.
    const outcome got = clapp::parse(tree, raw_args{"git", "-gC", "-m", "x"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("global"));
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"commit"});
    const arg_matches* child = got->subcommand_matches("commit");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(one_of(*child, "message") == "x");
}

CLAPP_TEST("a global argument reads back at every level, whichever level set it") {
    const outcome deep = clapp::parse(tree, raw_args{"git", "commit", "-g"});
    CLAPP_CHECK(deep.has_value());
    CLAPP_CHECK(deep->get_flag("global"));
    const arg_matches* child = deep->subcommand_matches("commit");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(child->get_flag("global"));

    const outcome shallow = clapp::parse(tree, raw_args{"git", "-g", "commit"});
    CLAPP_CHECK(shallow.has_value());
    CLAPP_CHECK(shallow->get_flag("global"));
    const arg_matches* shallow_child = shallow->subcommand_matches("commit");
    CLAPP_CHECK(shallow_child != nullptr);
    CLAPP_CHECK(shallow_child->get_flag("global"));
}

CLAPP_TEST("a subcommand is not recognized while an option is collecting") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).arg(arg_builder("opt").long_("opt")).subcommand(command_builder("go"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--opt", "go"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_of(*got, "opt") == "go");
    CLAPP_CHECK(!got->has_subcommand());
}

CLAPP_TEST("subcommand_precedence_over_arg inverts that, leaving the option empty") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .subcommand_precedence_over_arg()
                .arg(arg_builder("opt").long_("opt"))
                .subcommand(command_builder("go"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--opt", "go"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
}

CLAPP_TEST("args_conflicts_with_subcommands refuses a subcommand after an argument") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .args_conflicts_with_subcommands()
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .subcommand(command_builder("go"));
        return app.freeze();
    }();
    const outcome clash = clapp::parse(spec, raw_args{"app", "--flag", "go"});
    CLAPP_CHECK(!clash.has_value());
    CLAPP_CHECK(kind_of(clash) == error_kind::argument_conflict);

    const outcome alone = clapp::parse(spec, raw_args{"app", "go"});
    CLAPP_CHECK(alone.has_value());
    CLAPP_CHECK(alone->subcommand_name() == std::optional<std::string_view>{"go"});
}

CLAPP_TEST("an external subcommand captures its name and every remaining token") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .allow_external_subcommands();
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "other", "--any", "-x", "1"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"other"});
    const auto selected = got->subcommand();
    CLAPP_CHECK(selected.has_value());
    if (!selected.has_value()) return;
    const arg_matches& child = selected->second;
    // Stored under clapp::external_id, which is the EMPTY name — clap's `Id::EXTERNAL`.
    CLAPP_CHECK(raw_of(child, "") == std::vector<std::string>{"--any", "-x", "1"});
    const std::optional<std::span<const os_string>> readable = child.get_raw("");
    CLAPP_CHECK(readable.has_value());
    CLAPP_CHECK(readable->size() == 3);
}

CLAPP_TEST("an unrecognized token that resembles a subcommand says so") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("prog");
        std::move(app).subcommand(command_builder("install"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"prog", "instal"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_subcommand);
    CLAPP_CHECK(message_of(got) == "error: unrecognized subcommand 'instal'\n"
                                   "\n"
                                   "  tip: a similar subcommand exists: 'install'\n"
                                   "\n"
                                   "Usage: prog [COMMAND]\n"
                                   "\n"
                                   "For more information, try '--help'.\n");
    CLAPP_CHECK(!message_of(got).contains("to pass"));
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

CLAPP_TEST("an unknown long flag suggests the nearest one") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--quie"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(got.error().has_context(clapp::context_kind::suggested_arg));
    CLAPP_CHECK(message_of(got).find("--quiet") != std::string::npos);
}

CLAPP_TEST("a flag-shaped unknown stays an unknown argument, not an invalid subcommand") {
    static constexpr command_spec spec = [] consteval {
        return command_builder{"prog"}.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"prog", "--unknown"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got) == "error: unexpected argument '--unknown' found\n"
                                   "\n"
                                   "Usage: prog\n"
                                   "\n"
                                   "For more information, try '--help'.\n");
}

// clap calls this *smart usage*: once the parser knows which argument the user meant,
// the usage line names THAT argument instead of hiding it behind `[OPTIONS]`. The
// machinery is shared with the validator's conflict messages; these three cases are the
// token loop's half of it, and each was measured against clap_builder 4.6.5.
CLAPP_TEST("a suggested flag is named in the usage line, not hidden behind [OPTIONS]") {
    static constexpr command_spec one_flag = [] consteval {
        command_builder app("test");
        std::move(app).arg(arg_builder("verbose").long_("verbose").action(arg_action::set_true));
        return app.freeze();
    }();
    const outcome got = clapp::parse(one_flag, raw_args{"test", "--verbos"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).contains("Usage: test --verbose"));
}

CLAPP_TEST("an unneeded attached value names the flag that got it") {
    static constexpr command_spec zero_args = [] consteval {
        command_builder app("test");
        std::move(app).arg(arg_builder("o").long_("o").num_args(value_range::exactly(0)));
        return app.freeze();
    }();
    const outcome got = clapp::parse(zero_args, raw_args{"test", "--o=x"});
    CLAPP_CHECK(kind_of(got) == error_kind::too_many_values);
    CLAPP_CHECK(message_of(got).contains("Usage: test --o"));
}

CLAPP_TEST("smart usage renders an already-supplied positional as required") {
    // `<cmd>` rather than `[cmd]`: the user did supply it, so the line describes the
    // command they are building rather than the command in the abstract.
    static constexpr command_spec rest = [] consteval {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("cmd").index(1))
                .arg(arg_builder("rest").index(2).num_args(value_range::at_least(0)));
        return app.freeze();
    }();
    const outcome got = clapp::parse(rest, raw_args{"test", "prog", "--flag"});
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).contains("Usage: test <cmd> [rest]..."));
}

CLAPP_TEST("an unknown long flag with no neighbour offers the `--` escape instead") {
    const outcome got = clapp::parse(basic, raw_args{"app", "--zzzzzz"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(!got.error().has_context(clapp::context_kind::suggested_arg));
    // `basic` has a positional, so the "pass it as a value" tip is offered.
    CLAPP_CHECK(message_of(got).find("-- --zzzzzz") != std::string::npos);
}

CLAPP_TEST("an unknown short flag is quoted with its dash and gets no fuzzy match") {
    const outcome got = clapp::parse(basic, raw_args{"app", "-z"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).find("'-z'") != std::string::npos);
}

CLAPP_TEST("a subcommand name after `--` is reported as an unnecessary double dash") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).subcommand(command_builder("go"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--", "go"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(got).find("--") != std::string::npos);
}

CLAPP_TEST("an enumerated parser lists what it would have accepted") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).arg(arg_builder("mode").long_("mode").value_parser<bool>());
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--mode", "maybe"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
    CLAPP_CHECK(message_of(got).find("true") != std::string::npos);
    CLAPP_CHECK(message_of(got).find("false") != std::string::npos);
}

// ---------------------------------------------------------------------------
// "No value was supplied" and "an empty value was supplied" are different facts
//
// The whole page is compared, because both halves of this were wrong at once and each
// half looks right on its own: the KIND said clapp::error_kind::invalid_value where clap
// says ValueValidation, and the SENTENCE said "a value is required … but none was
// supplied" — of an ordinary `--port "$MAYBE_UNSET"` from a shell script, where a value
// most certainly was. Checking only the kind, or only a substring of the sentence, is
// what let it stand. Every expected string below was measured against clap 4.6 with
// `value_parser!(u16)` / `char` / `f64` / `bool` / `PathBuf` and the same argv.
// ---------------------------------------------------------------------------

namespace {

    consteval command_spec make_values() {
        command_builder app("app");
        std::move(app)
                .arg(arg_builder("port").long_("port").value_parser<std::uint16_t>())
                .arg(arg_builder("ratio").long_("ratio").value_parser<double>())
                .arg(arg_builder("sep").long_("sep").value_parser<char>())
                .arg(arg_builder("flag").long_("flag").value_parser<bool>())
                .arg(arg_builder("path").long_("path").value_parser<std::filesystem::path>());
        return app.freeze();
    }
    constexpr command_spec values = make_values();

}  // namespace

CLAPP_TEST("an empty value handed to a numeric parser is a conversion failure, not absence") {
    const outcome got = clapp::parse(values, raw_args{"app", "--port", ""});
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(message_of(got) ==
                "error: invalid value '' for '--port <port>': cannot parse integer from "
                "empty string\n\nFor more information, try '--help'.\n");

    // Same for a float and for a char, with each type's own Rust wording.
    const outcome ratio = clapp::parse(values, raw_args{"app", "--ratio", ""});
    CLAPP_CHECK(kind_of(ratio) == error_kind::value_validation);
    CLAPP_CHECK(message_of(ratio).contains(
            "invalid value '' for '--ratio <ratio>': cannot parse float from empty string"));

    const outcome sep = clapp::parse(values, raw_args{"app", "--sep", ""});
    CLAPP_CHECK(kind_of(sep) == error_kind::value_validation);
    CLAPP_CHECK(message_of(sep).contains(
            "invalid value '' for '--sep <sep>': cannot parse char from empty string"));

    // `--port=` and `-p ""` reach the parser the same way, so they say the same thing.
    CLAPP_CHECK(message_of(clapp::parse(values, raw_args{"app", "--port="})) == message_of(got));
}

CLAPP_TEST("no value at all still says none was supplied") {
    // The other side of the same distinction, and the reason it cannot simply be
    // deleted: clapp::detail::parse_engine::verify_num_args() raises this one, and it is
    // clap's `ErrorKind::InvalidValue` with clap's sentence.
    const outcome got = clapp::parse(values, raw_args{"app", "--port"});
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
    CLAPP_CHECK(message_of(got) ==
                "error: a value is required for '--port <port>' but none was supplied"
                "\n\nFor more information, try '--help'.\n");
}

CLAPP_TEST("a parser that has no empty value keeps clap's empty_value sentence") {
    // clapp::parse_error_kind::empty_value survives, for the one builtin that means it:
    // clap's PathBufValueParser. Asserted on the side that HAS the behaviour, not only
    // on the side that lacks it.
    const outcome got = clapp::parse(values, raw_args{"app", "--path", ""});
    CLAPP_CHECK(kind_of(got) == error_kind::invalid_value);
    CLAPP_CHECK(message_of(got) ==
                "error: a value is required for '--path <path>' but none was supplied"
                "\n\nFor more information, try '--help'.\n");

    // And bool keeps its accepted-value list on the same sentence, as clap's
    // BoolValueParser does.
    const outcome flag = clapp::parse(values, raw_args{"app", "--flag", ""});
    CLAPP_CHECK(kind_of(flag) == error_kind::invalid_value);
    CLAPP_CHECK(message_of(flag) ==
                "error: a value is required for '--flag <flag>' but none was supplied\n"
                "  [possible values: true, false]\n\nFor more information, try '--help'.\n");
}

CLAPP_TEST("an out-of-range integer names the range it is not in") {
    // clap: `invalid value '99999' for '--port <port>': 99999 is not in 0..=65535`.
    // clapp used to forward `std::from_chars`'s "number too large to fit in target type",
    // which never tells the user what WOULD fit — and computed the interval anyway.
    const outcome got = clapp::parse(values, raw_args{"app", "--port", "99999"});
    CLAPP_CHECK(kind_of(got) == error_kind::value_validation);
    CLAPP_CHECK(message_of(got) ==
                "error: invalid value '99999' for '--port <port>': 99999 is not in 0..=65535"
                "\n\nFor more information, try '--help'.\n");

    // A range failure with no interval to name keeps the parser's sentence: the float
    // overflow is a recorded divergence from clap, which saturates instead.
    const outcome ratio = clapp::parse(values, raw_args{"app", "--ratio", "1e400"});
    CLAPP_CHECK(kind_of(ratio) == error_kind::value_validation);
    CLAPP_CHECK(message_of(ratio).contains(
            "invalid value '1e400' for '--ratio <ratio>': number is outside the "
            "representable range"));
}

// ---------------------------------------------------------------------------
// help and version are control flow, not failure
// ---------------------------------------------------------------------------

CLAPP_TEST("--help returns a display_help error that exits zero on stdout") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).about("does things").version("1.2.3");
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--help"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::display_help);
    CLAPP_CHECK(!got.error().use_stderr());
    CLAPP_CHECK(got.error().exit_code() == 0);
    CLAPP_CHECK(clapp::is_display(error_kind::display_help));
}

CLAPP_TEST("--version renders the name and the version") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).version("1.2.3");
        return app.freeze();
    }();
    const outcome long_form = clapp::parse(spec, raw_args{"app", "--version"});
    CLAPP_CHECK(!long_form.has_value());
    CLAPP_CHECK(kind_of(long_form) == error_kind::display_version);
    CLAPP_CHECK(long_form.error().exit_code() == 0);
    CLAPP_CHECK(message_of(long_form).find("app 1.2.3") != std::string::npos);

    const outcome short_form = clapp::parse(spec, raw_args{"app", "-V"});
    CLAPP_CHECK(!short_form.has_value());
    CLAPP_CHECK(kind_of(short_form) == error_kind::display_version);
}

CLAPP_TEST("the help subcommand reports the level it names") {
    const outcome root = clapp::parse(tree, raw_args{"git", "help"});
    CLAPP_CHECK(!root.has_value());
    CLAPP_CHECK(kind_of(root) == error_kind::display_help);

    const outcome deep = clapp::parse(tree, raw_args{"git", "help", "commit"});
    CLAPP_CHECK(!deep.has_value());
    CLAPP_CHECK(kind_of(deep) == error_kind::display_help);

    const outcome missing = clapp::parse(tree, raw_args{"git", "help", "nonsuch"});
    CLAPP_CHECK(!missing.has_value());
    CLAPP_CHECK(kind_of(missing) == error_kind::invalid_subcommand);
}

CLAPP_TEST("the `help` subcommand's own error advertises the help ITS level has") {
    // `git help help <bad>` reports on the generated `help` command, which carries
    // clapp::command_setting::disable_help_flag — so it has no `--help` to try, and clap
    // prints no footer at all. Taking the footer from the root instead advertises a flag
    // the named command does not accept, and every other `help` path agrees with the
    // root, so nothing else in the suite could see it.
    const outcome nested = clapp::parse(tree, raw_args{"git", "help", "help", "nonsuch"});
    CLAPP_CHECK(kind_of(nested) == error_kind::invalid_subcommand);
    CLAPP_CHECK(message_of(nested) ==
                "error: unrecognized subcommand 'nonsuch'\n\nUsage: git help [COMMAND]...\n");
    CLAPP_CHECK(!nested.error().has_help_flag());

    // The levels that DO have help still get it, and each gets its own — the footer and
    // the usage line must name the SAME command, which is the property the nested case
    // above broke. All three pages below are byte-identical to clap 4.6's.
    const outcome at_root = clapp::parse(tree, raw_args{"git", "help", "nonsuch"});
    CLAPP_CHECK(at_root.error().help_flag() == std::optional<std::string_view>{"--help"});
    CLAPP_CHECK(message_of(at_root) ==
                "error: unrecognized subcommand 'nonsuch'\n\nUsage: git [OPTIONS] [COMMAND]"
                "\n\nFor more information, try '--help'.\n");

    const outcome under_flag_sub = clapp::parse(tree, raw_args{"git", "help", "commit", "nonsuch"});
    CLAPP_CHECK(kind_of(under_flag_sub) == error_kind::invalid_subcommand);
    CLAPP_CHECK(under_flag_sub.error().help_flag() == std::optional<std::string_view>{"--help"});
    CLAPP_CHECK(message_of(under_flag_sub) == "error: unrecognized subcommand 'nonsuch'\n\n"
                                              "Usage: git {commit|--commit|-C} [OPTIONS] [COMMAND]"
                                              "\n\nFor more information, try '--help'.\n");

    const outcome under_sub = clapp::parse(tree, raw_args{"git", "help", "checkout", "nonsuch"});
    CLAPP_CHECK(message_of(under_sub) ==
                "error: unrecognized subcommand 'nonsuch'\n\nUsage: git checkout [OPTIONS]"
                "\n\nFor more information, try '--help'.\n");
}

// ---------------------------------------------------------------------------
// The usage name below the root
//
// A frozen clapp::command_spec knows its own name and nothing about its parent, so a
// subcommand's `Usage:` line has to be told the path it sits at. Getting this wrong is
// invisible to every structural check and to every test that only asks for the error
// KIND: the message is well-formed, the wording is clap's, and the one thing it names —
// the command to type — does not exist. Each expectation below was measured against
// clap_builder 4.6.5 with the same spec and the same argv.
// ---------------------------------------------------------------------------

namespace {

    consteval command_spec make_path_sub() {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub).arg(
                arg_builder("r").long_("r").num_args(value_range::exactly(1)).required());
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }
    constexpr command_spec path_sub = make_path_sub();

}  // namespace

CLAPP_TEST("a subcommand's usage line names the whole path, not just its own name") {
    const outcome missing = clapp::parse(path_sub, raw_args{"test", "sub"});
    CLAPP_CHECK(kind_of(missing) == error_kind::missing_required_argument);
    CLAPP_CHECK(message_of(missing).contains("Usage: test sub --r <r>"));

    // Same path on the token loop's own errors, not only the validator's.
    const outcome unknown = clapp::parse(path_sub, raw_args{"test", "sub", "--nope"});
    CLAPP_CHECK(kind_of(unknown) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(unknown).contains("Usage: test sub --r <r>"));
}

CLAPP_TEST("the path keeps growing at every level, and bin_name replaces the root") {
    static constexpr command_spec deep = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        command_builder inner("deep");
        std::move(inner).arg(
                arg_builder("r").long_("r").num_args(value_range::exactly(1)).required());
        std::move(sub).subcommand(std::move(inner));
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }();
    CLAPP_CHECK(message_of(clapp::parse(deep, raw_args{"test", "sub", "deep"}))
                        .contains("Usage: test sub deep --r <r>"));

    // bin_name() renames the ROOT and everything below inherits the new spelling —
    // `mytool sub`, never `test sub` and never `mytool test sub`.
    static constexpr command_spec renamed = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub).arg(
                arg_builder("r").long_("r").num_args(value_range::exactly(1)).required());
        std::move(app).bin_name("mytool").subcommand(std::move(sub));
        return app.freeze();
    }();
    CLAPP_CHECK(message_of(clapp::parse(renamed, raw_args{"mytool", "sub"}))
                        .contains("Usage: mytool sub --r <r>"));
}

CLAPP_TEST("the path carries the parent's requirements and the child's flag spellings") {
    // clap's `mid_string`: a parent requirement that is still outstanding is part of the
    // line the user has to type, so it sits between the two names.
    static constexpr command_spec mid = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub).arg(
                arg_builder("r").long_("r").num_args(value_range::exactly(1)).required());
        std::move(app)
                .arg(arg_builder("out").long_("out").num_args(value_range::exactly(1)).required())
                .subcommand(std::move(sub));
        return app.freeze();
    }();
    CLAPP_CHECK(message_of(clapp::parse(mid, raw_args{"test", "--out", "f", "sub"}))
                        .contains("Usage: test --out <out> sub --r <r>"));

    // ...unless subcommand_negates_reqs says the requirement stops applying, in which
    // case clap drops it again. Same spec but for that one setting.
    static constexpr command_spec negates = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub).arg(
                arg_builder("r").long_("r").num_args(value_range::exactly(1)).required());
        std::move(app)
                .subcommand_negates_reqs()
                .arg(arg_builder("out").long_("out").num_args(value_range::exactly(1)).required())
                .subcommand(std::move(sub));
        return app.freeze();
    }();
    CLAPP_CHECK(message_of(clapp::parse(negates, raw_args{"test", "sub"}))
                        .contains("Usage: test sub --r <r>"));

    // A flag subcommand can be reached three ways, so clap brace-lists all of them.
    static constexpr command_spec flagged = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub).long_flag("sub").arg(
                arg_builder("r").long_("r").num_args(value_range::exactly(1)).required());
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }();
    CLAPP_CHECK(message_of(clapp::parse(flagged, raw_args{"test", "--sub"}))
                        .contains("Usage: test {sub|--sub} --r <r>"));
}

CLAPP_TEST("missing_subcommand quotes the plain path, with no flag forms") {
    // clap quotes `bin_name` here and `usage_name` in the Usage line, and the two differ.
    static constexpr command_spec nested = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub).subcommand_required().subcommand(command_builder("deep"));
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }();
    const outcome got = clapp::parse(nested, raw_args{"test", "sub"});
    CLAPP_CHECK(kind_of(got) == error_kind::missing_subcommand);
    const std::string text = message_of(got);
    CLAPP_CHECK(text.contains("'test sub' requires a subcommand but one was not provided"));
    CLAPP_CHECK(text.contains("Usage: test sub <COMMAND>"));
}

CLAPP_TEST("the help text of a subcommand names the path too, however it was reached") {
    static constexpr command_spec helped = [] consteval {
        command_builder app("test");
        command_builder sub("sub");
        std::move(sub)
                .about("do a thing")
                .arg(arg_builder("s").long_("s").num_args(value_range::exactly(1)));
        std::move(app).subcommand(std::move(sub));
        return app.freeze();
    }();
    // `test sub --help` and `test help sub` walk down by different routes and must agree.
    const outcome flag = clapp::parse(helped, raw_args{"test", "sub", "--help"});
    CLAPP_CHECK(kind_of(flag) == error_kind::display_help);
    CLAPP_CHECK(message_of(flag).contains("Usage: test sub"));

    const outcome word = clapp::parse(helped, raw_args{"test", "help", "sub"});
    CLAPP_CHECK(kind_of(word) == error_kind::display_help);
    CLAPP_CHECK(message_of(word).contains("Usage: test sub"));
}

// ---------------------------------------------------------------------------
// Entry-point settings
// ---------------------------------------------------------------------------

CLAPP_TEST("argv[0] is consumed unless no_binary_name says otherwise") {
    const outcome with_binary = clapp::parse(basic, raw_args{"app", "value"});
    CLAPP_CHECK(with_binary.has_value());
    CLAPP_CHECK(one_of(*with_binary, "input") == "value");

    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app).no_binary_name().arg(arg_builder("first").index(1));
        return app.freeze();
    }();
    const outcome without = clapp::parse(spec, raw_args{"value"});
    CLAPP_CHECK(without.has_value());
    CLAPP_CHECK(one_of(*without, "first") == "value");
}

CLAPP_TEST("multicall turns argv[0]'s file stem into the subcommand") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("busybox");
        std::move(app).multicall().subcommand(command_builder("ls").arg(
                arg_builder("all").short_('a').action(arg_action::set_true)));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"/usr/bin/ls", "-a"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->subcommand_name() == std::optional<std::string_view>{"ls"});
    const arg_matches* child = got->subcommand_matches("ls");
    CLAPP_CHECK(child != nullptr);
    CLAPP_CHECK(child->get_flag("all"));
}

CLAPP_TEST("infer_long_args accepts a unique prefix and refuses an ambiguous one") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .infer_long_args()
                .disable_help_flag()
                .arg(arg_builder("verbose").long_("verbose").action(arg_action::set_true))
                .arg(arg_builder("version_check")
                             .long_("version-check")
                             .action(arg_action::set_true));
        return app.freeze();
    }();
    const outcome unique = clapp::parse(spec, raw_args{"app", "--verb"});
    CLAPP_CHECK(unique.has_value());
    CLAPP_CHECK(unique->get_flag("verbose"));

    const outcome ambiguous = clapp::parse(spec, raw_args{"app", "--ver"});
    CLAPP_CHECK(!ambiguous.has_value());
    CLAPP_CHECK(kind_of(ambiguous) == error_kind::unknown_argument);
}

CLAPP_TEST("ignore_errors keeps what parsed and still applies the later sources") {
    static constexpr command_spec spec = [] consteval {
        command_builder app("app");
        std::move(app)
                .ignore_errors()
                .arg(arg_builder("flag").long_("flag").action(arg_action::set_true))
                .arg(arg_builder("level").long_("level").default_value("info"));
        return app.freeze();
    }();
    const outcome got = clapp::parse(spec, raw_args{"app", "--flag", "--nonsuch"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
    CLAPP_CHECK(one_of(*got, "level") == "info");
}

CLAPP_TEST("an empty command line parses to nothing but the defaults") {
    const outcome got = clapp::parse(basic, raw_args{"app"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->has_subcommand());
    // `verbose` and `quiet` carry injected defaults; nothing was supplied explicitly.
    CLAPP_CHECK(!got->args_present());
}

// ---------------------------------------------------------------------------
// The four spots 2026-08's coverage run found at zero execution
//
// These branches were found by line coverage. They are here rather than in a
// `conformance_*` file because clap has no
// case for any of them — clap's own suite leaves the same branches cold, so a conformance
// port could never have closed them. That is the point worth remembering: **a 100%
// faithful port of somebody else's suite inherits its blind spots.**
// ---------------------------------------------------------------------------

CLAPP_TEST("require_equals on a SHORT option reports no_equals") {
    // Roadmap gap (a). `parse.hpp`'s `apply_short_result` has its own
    // `equals_not_provided` arm, separate from the long one, and until now only the long
    // one had a witness — every existing case (`conformance_opts_test.cpp`,
    // `conformance_empty_values_test.cpp`) spells the option `--config`.
    static constexpr command_spec spec = [] consteval {
        command_builder app("prog");
        std::move(app).arg(arg_builder("cfg")
                                   .short_('c')
                                   .long_("config")
                                   .value_name("cfg")
                                   .require_equals()
                                   .action(arg_action::set));
        return app.freeze();
    }();

    const outcome detached = clapp::parse(spec, raw_args{"prog", "-c", "value"});
    CLAPP_CHECK(!detached.has_value());
    CLAPP_CHECK(kind_of(detached) == error_kind::no_equals);
    // The message names the LONG spelling even though the user typed the short one —
    // clap does the same, and it is the form that shows where the `=` goes.
    CLAPP_CHECK(message_of(detached).find("--config=<cfg>") != std::string::npos);

    // The positive control: with the `=`, the same short spelling parses.
    const outcome attached = clapp::parse(spec, raw_args{"prog", "-c=value"});
    CLAPP_CHECK(attached.has_value());
    CLAPP_CHECK(one_of(*attached, "cfg") == "value");
}

CLAPP_TEST("an unknown NON-ASCII short flag is quoted back as UTF-8") {
    // Roadmap gap (b). `detail::append_utf8()` has four arms and only the ASCII one had
    // ever run; the other three were proved by a `consteval` assertion
    // (`an_unknown_short_flag_is_quoted_as_utf8`) that gcov cannot see. These drive the
    // 2-, 3- and 4-byte arms through the real parse loop, where a one-byte-only encoder
    // would emit a mangled diagnostic rather than crash.
    static constexpr command_spec spec = [] consteval {
        command_builder app("prog");
        std::move(app).arg(arg_builder("v").short_('v').action(arg_action::set_true));
        return app.freeze();
    }();

    const outcome two = clapp::parse(spec, raw_args{"prog", "-é"});
    CLAPP_CHECK(!two.has_value());
    CLAPP_CHECK(kind_of(two) == error_kind::unknown_argument);
    CLAPP_CHECK(message_of(two).find("'-é'") != std::string::npos);

    const outcome three = clapp::parse(spec, raw_args{"prog", "-中"});
    CLAPP_CHECK(!three.has_value());
    CLAPP_CHECK(message_of(three).find("'-中'") != std::string::npos);

    const outcome four = clapp::parse(spec, raw_args{"prog", "-\U0001F600"});
    CLAPP_CHECK(!four.has_value());
    CLAPP_CHECK(message_of(four).find("'-\U0001F600'") != std::string::npos);

    // The control: the SAME loop still recognises the ASCII flag it does know.
    const outcome known = clapp::parse(spec, raw_args{"prog", "-v"});
    CLAPP_CHECK(known.has_value());
    CLAPP_CHECK(known->get_flag("v"));
}

CLAPP_TEST("infer_subcommands reaches a long-flag subcommand through an ALIAS prefix") {
    // Roadmap gap (d), first half. `possible_long_flag_subcommand()` tries the canonical
    // long flag first and the aliases second; only the first branch had a witness.
    static constexpr command_spec spec = [] consteval {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("test").long_flag("test").long_flag_alias("something"))
                .subcommand(command_builder("temp").long_flag("temp"));
        return app.freeze();
    }();

    // `--some` is a prefix of the ALIAS `something`, of nothing else, and resolves to the
    // canonical subcommand name.
    const outcome via_alias = clapp::parse(spec, raw_args{"prog", "--some"});
    CLAPP_CHECK(via_alias.has_value());
    CLAPP_CHECK(via_alias->subcommand_name() == std::optional<std::string_view>{"test"});

    // ...and the canonical spelling still works, so the alias branch did not replace it.
    const outcome via_name = clapp::parse(spec, raw_args{"prog", "--test"});
    CLAPP_CHECK(via_name.has_value());
    CLAPP_CHECK(via_name->subcommand_name() == std::optional<std::string_view>{"test"});
}

CLAPP_TEST("an ambiguous long-flag subcommand prefix matches NEITHER") {
    // Roadmap gap (d), second half: the `ambiguous = true` line. `--te` is a prefix of
    // both `--test` and `--temp`, so inference must refuse rather than take the first.
    static constexpr command_spec spec = [] consteval {
        command_builder app("prog");
        std::move(app)
                .infer_subcommands()
                .subcommand(command_builder("test").long_flag("test"))
                .subcommand(command_builder("temp").long_flag("temp"));
        return app.freeze();
    }();

    const outcome ambiguous = clapp::parse(spec, raw_args{"prog", "--te"});
    CLAPP_CHECK(!ambiguous.has_value());
    CLAPP_CHECK(kind_of(ambiguous) == error_kind::unknown_argument);

    // The disambiguating letter is enough.
    const outcome resolved = clapp::parse(spec, raw_args{"prog", "--tes"});
    CLAPP_CHECK(resolved.has_value());
    CLAPP_CHECK(resolved->subcommand_name() == std::optional<std::string_view>{"test"});
}
