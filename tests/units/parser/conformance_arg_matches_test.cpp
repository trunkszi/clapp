#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <csignal>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if __has_include(<unistd.h>) && __has_include(<sys/wait.h>)
#    define CLAPP_TEST_HAS_FORK 1
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::matches_error_kind;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;
    using id_list = std::vector<std::string_view>;

#ifdef CLAPP_TEST_HAS_FORK
    struct abort_result {
        int signal = -1;
        std::string err;
    };

    std::string drain(int fd) {
        std::string text;
        char buffer[256];
        for (;;) {
            const ::ssize_t got = ::read(fd, buffer, sizeof buffer);
            if (got <= 0) break;
            text.append(buffer, static_cast<std::size_t>(got));
        }
        ::close(fd);
        return text;
    }

    template<class F>
    abort_result run_to_abort(F&& query) {
        int err_pipe[2] = {-1, -1};
        if (::pipe(err_pipe) != 0) return {};

        static_cast<void>(std::fflush(nullptr));
        const ::pid_t child = ::fork();
        if (child == 0) {
            static_cast<void>(::dup2(err_pipe[1], 2));
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            std::forward<F>(query)();
            ::_exit(70);
        }
        ::close(err_pipe[1]);

        abort_result result;
        result.err = drain(err_pipe[0]);
        int raw    = 0;
        static_cast<void>(::waitpid(child, &raw, 0));
        result.signal = raw & 0x7f;
        return result;
    }
#endif

    /**
     * The ids a parse recorded, in the order clapp reports them (name order, see the
     * divergence note above). Deliberately NOT sorted or normalised here: the ordering is a
     * documented difference from clap and belongs in the assertion, not in a helper that
     * would quietly make both orderings pass.
     */
    id_list ids_of(const arg_matches& matches) {
        id_list out;
        for (const clapp::arg_id& one : matches.ids()) out.push_back(one.name());
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // clap's file uses `value_parser!(std::path::PathBuf)` on `--config` and a
    // possible-value list on `--color`. Neither participates in what the tests assert —
    // they are there to make the two arguments visibly different types — so both are plain
    // string options here. What matters is that both are declared and only some are given.
    // ---------------------------------------------------------------------------

    consteval command_spec make_two_options() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("color").long_("color").action(arg_action::set))
                .arg(arg_builder("config").long_("config").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec two_options = make_two_options();

    consteval command_spec make_overriding() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("color").long_("color").action(arg_action::set))
                .arg(arg_builder("config")
                             .long_("config")
                             .action(arg_action::set)
                             .overrides_with("color"));
        return app.freeze();
    }
    constexpr command_spec overriding = make_overriding();

    consteval command_spec make_positional() {
        command_builder app("test");
        std::move(app).arg(arg_builder("positional").index(1));
        return app.freeze();
    }
    constexpr command_spec positional_only = make_positional();

    consteval command_spec make_flag() {
        command_builder app("test");
        std::move(app).arg(arg_builder("flag").long_("flag").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec flag_only = make_flag();

    /**
     * clap's `arg_matches_if_present_wrong_arg` / `arg_matches_value_of_wrong_arg` shape:
     * the id and the flag spelling deliberately differ, so that asking by spelling is a
     * detectable mistake rather than an accidental synonym.
     */
    consteval command_spec make_short_named() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("flag").short_('f').action(arg_action::set_true))
                .arg(arg_builder("opt").short_('o').action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec short_named = make_short_named();

    consteval command_spec make_with_subcommand() {
        command_builder app("test");
        std::move(app)
                .subcommand(command_builder("speed").alias("fast"))
                .subcommand(command_builder("check"));
        return app.freeze();
    }
    constexpr command_spec with_subcommand = make_with_subcommand();

    consteval command_spec make_with_external_subcommand() {
        command_builder app("test");
        std::move(app).allow_external_subcommands();
        return app.freeze();
    }
    constexpr command_spec with_external_subcommand = make_with_external_subcommand();

    // The definitions themselves, so a failure below is unambiguously about the ACCESSORS.
    static_assert(two_options.has_arg("color"));
    static_assert(two_options.has_arg("config"));
    static_assert(short_named.has_arg("flag"));
    static_assert(!short_named.has_arg("f"));  // the short spelling is not an id
    static_assert(short_named.has_arg("opt"));
    static_assert(!short_named.has_arg("o"));
    static_assert(with_subcommand.has_subcommand("speed"));
    static_assert(with_subcommand.has_subcommand("fast"));
    static_assert(with_subcommand.has_subcommand("check"));
    static_assert(!with_subcommand.has_subcommand("seed"));

}  // namespace

// ===========================================================================
// ids(): what the parse recorded
// ===========================================================================

CLAPP_TEST("arg_matches.rs::ids") {
    const outcome got =
            clapp::parse(two_options, raw_args{"test", "--config=config.toml", "--color=auto"});
    CLAPP_CHECK(got.has_value());
    // clap: ["config", "color"] — insertion order. clapp sorts; see the divergence note.
    CLAPP_CHECK(ids_of(*got) == id_list{"color", "config"});
    CLAPP_CHECK(got->arg_count() == 2);
}

CLAPP_TEST("arg_matches.rs::ids_ignore_unused") {
    const outcome got = clapp::parse(two_options, raw_args{"test", "--config=config.toml"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(ids_of(*got) == id_list{"config"});
    CLAPP_CHECK(got->arg_count() == 1);
}

CLAPP_TEST("arg_matches.rs::ids_ignore_overridden") {
    // `--config` declares overrides_with("color"), and BOTH are supplied. The later
    // token wins, so it is `config` that disappears — the direction is easy to get
    // backwards, and getting it backwards still yields a one-element list.
    const outcome got =
            clapp::parse(overriding, raw_args{"test", "--config=config.toml", "--color=auto"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(ids_of(*got) == id_list{"color"});
    CLAPP_CHECK(got->arg_count() == 1);
}

// ===========================================================================
// args_present(): defaults do not count, subcommands do not count
// ===========================================================================

CLAPP_TEST("arg_matches.rs::args_present_positional") {
    const outcome empty = clapp::parse(positional_only, raw_args{"test"});
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(!empty->args_present());

    const outcome given = clapp::parse(positional_only, raw_args{"test", "value"});
    CLAPP_CHECK(given.has_value());
    CLAPP_CHECK(given->args_present());
}

CLAPP_TEST("arg_matches.rs::args_present_flag") {
    const outcome empty = clapp::parse(flag_only, raw_args{"test"});
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(!empty->args_present());
    // The flag still HAS an id — clapp records the `set_true` default — which is exactly
    // why args_present() cannot be implemented as `!empty()`.
    CLAPP_CHECK(empty->arg_count() == 1);
    CLAPP_CHECK(empty->contains_id("flag"));

    const outcome given = clapp::parse(flag_only, raw_args{"test", "--flag"});
    CLAPP_CHECK(given.has_value());
    CLAPP_CHECK(given->args_present());
}

CLAPP_TEST("arg_matches.rs::args_present_subcommand") {
    const outcome empty = clapp::parse(with_subcommand, raw_args{"test"});
    CLAPP_CHECK(empty.has_value());
    CLAPP_CHECK(!empty->args_present());

    // Running the subcommand leaves the PARENT with nothing present. clap asserts the
    // same, and it is the assertion that keeps `args_present()` from creeping into
    // meaning "did anything happen".
    const outcome given = clapp::parse(with_subcommand, raw_args{"test", "speed"});
    CLAPP_CHECK(given.has_value());
    CLAPP_CHECK(!given->args_present());
    CLAPP_CHECK(given->has_subcommand());
}

// ===========================================================================
// The three #[should_panic] cases, through the non-aborting twin
// ===========================================================================

CLAPP_TEST("arg_matches.rs::arg_matches_if_present_wrong_arg") {
    // clap: panics with "Unknown argument or group id.  Make sure you are using the
    // argument id and not the short or long flags".
    const outcome got = clapp::parse(short_named, raw_args{"test", "-f"});
    CLAPP_CHECK(got.has_value());

    // The correct question first, so the fixture is known good.
    const std::optional<const bool*> value = got->get_one<bool>("flag");
    CLAPP_CHECK(value.has_value());
    CLAPP_CHECK(**value);
    CLAPP_CHECK(got->contains_id("flag"));

    // The mistake: `-f` is a spelling, `flag` is the id. Not `false` — an ERROR.
    const std::expected<bool, clapp::matches_error> wrong = got->try_contains_id("f");
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(wrong.error().kind() == matches_error_kind::unknown_argument);
    // The sentence a developer sees, which must say what to do about it. Compared as a
    // whole expression: clapp::matches_error::to_string() returns by value.
    CLAPP_CHECK(wrong.error().to_string() ==
                "unknown argument or group id -- make sure you are using the argument id and "
                "not the short or long flag");

    // And the detection only exists because the parser told the matches what is legal.
    CLAPP_CHECK(got->has_id_validation());
}

CLAPP_TEST("arg_matches.rs::arg_matches_value_of_wrong_arg") {
    // clap: panics with "Mismatch between definition and access of `o`. Unknown argument
    // or group id. ...".
    const outcome got = clapp::parse(short_named, raw_args{"test", "-o", "val"});
    CLAPP_CHECK(got.has_value());

    const std::optional<const std::string*> value = got->get_one<std::string>("opt");
    CLAPP_CHECK(value.has_value());
    CLAPP_CHECK(**value == "val");

    const std::expected<std::optional<const std::string*>, clapp::matches_error> wrong =
            got->try_get_one<std::string>("o");
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(wrong.error().kind() == matches_error_kind::unknown_argument);
}

CLAPP_TEST("arg_matches.rs::wrong type on a declared id is a downcast error") {
    // The other half of clap's "Mismatch between definition and access" panic: the id is
    // right and the TYPE is wrong. clap's own witness for this is in utf8.rs (ported in
    // conformance_utf8_test.cpp, on the external-subcommand id); this pins the ordinary
    // case, on a flag, where the stored type is `bool`.
    const outcome got = clapp::parse(short_named, raw_args{"test", "-f"});
    CLAPP_CHECK(got.has_value());

    const std::expected<std::optional<const std::string*>, clapp::matches_error> wrong =
            got->try_get_one<std::string>("flag");
    CLAPP_CHECK(!wrong.has_value());
    CLAPP_CHECK(wrong.error().kind() == matches_error_kind::downcast);
    // Both type names, so the message says which way round the mistake was. The exact
    // spelling of a type name is implementation-defined (CLAUDE.md trap 11) — assert the
    // shape and the distinctness, never the bytes.
    CLAPP_CHECK(wrong.error().expected() != wrong.error().actual());
    CLAPP_CHECK(wrong.error().actual() == clapp::any_id::of<bool>());
    CLAPP_CHECK(wrong.error().expected() == clapp::any_id::of<std::string>());
    CLAPP_CHECK(wrong.error().to_string().starts_with("could not downcast to "));
}

CLAPP_TEST("arg_matches.rs::arg_matches_subcommand_matches_wrong_sub") {
    // clap: panics with "`seed` is not a name of a subcommand."
    const outcome got = clapp::parse(with_subcommand, raw_args{"test", "speed"});
    CLAPP_CHECK(got.has_value());

    CLAPP_CHECK(got->subcommand_matches("speed") != nullptr);
    CLAPP_CHECK(got->subcommand_matches("check") == nullptr);
    CLAPP_CHECK(got->has_subcommand_validation());
    CLAPP_CHECK(got->is_valid_subcommand("speed"));
    CLAPP_CHECK(!got->is_valid_subcommand("seed"));

    // A declared subcommand that was not run is ordinary absence.
    const outcome unused = clapp::parse(with_subcommand, raw_args{"test"});
    CLAPP_CHECK(unused.has_value());
    CLAPP_CHECK(unused->subcommand_matches("speed") == nullptr);

#ifdef CLAPP_TEST_HAS_FORK
    // Validation precedes the "did one run?" check: the same programmer error aborts
    // whether a different subcommand ran or none did.
    for (const arg_matches* matches : {std::addressof(*got), std::addressof(*unused)}) {
        const abort_result bad =
                run_to_abort([matches] { static_cast<void>(matches->subcommand_matches("seed")); });
        CLAPP_CHECK(bad.signal == SIGABRT);
        CLAPP_CHECK(bad.err == "clapp: `seed` is not a name of a subcommand; this is a bug in the "
                               "calling program, not in its input.\n");
    }
#endif
}

CLAPP_TEST("subcommand_matches accepts canonical names, not aliases or captured external names") {
    const outcome aliased = clapp::parse(with_subcommand, raw_args{"test", "fast"});
    CLAPP_CHECK(aliased.has_value());
    CLAPP_CHECK(aliased->subcommand_name() == std::optional<std::string_view>{"speed"});
    CLAPP_CHECK(aliased->subcommand_matches("speed") != nullptr);
    CLAPP_CHECK(!aliased->is_valid_subcommand("fast"));

    const outcome external =
            clapp::parse(with_external_subcommand, raw_args{"test", "third-party", "arg"});
    CLAPP_CHECK(external.has_value());
    CLAPP_CHECK(external->subcommand_name() == std::optional<std::string_view>{"third-party"});
    CLAPP_CHECK(external->subcommand_matches("") == nullptr);
    CLAPP_CHECK(external->is_valid_subcommand(""));
    CLAPP_CHECK(!external->is_valid_subcommand("third-party"));

#ifdef CLAPP_TEST_HAS_FORK
    const abort_result alias_query =
            run_to_abort([&aliased] { static_cast<void>(aliased->subcommand_matches("fast")); });
    CLAPP_CHECK(alias_query.signal == SIGABRT);
    CLAPP_CHECK(alias_query.err.find("`fast` is not a name of a subcommand") != std::string::npos);

    const abort_result external_query = run_to_abort(
            [&external] { static_cast<void>(external->subcommand_matches("third-party")); });
    CLAPP_CHECK(external_query.signal == SIGABRT);
    CLAPP_CHECK(external_query.err.find("`third-party` is not a name of a subcommand") !=
                std::string::npos);
#endif
}
