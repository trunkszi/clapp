#include <clapp/derive.hpp>
#include "support/check.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>
#include <cstdio>
#include <cstdlib>
#if !defined(_WIN32)
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
    consteval clapp::command_spec make_core_command() {
        clapp::command_builder app{"core"};
        app.version("1.0").styles(clapp::styles::styled()).color(clapp::color_choice::always);
        clapp::arg_builder text{"text"};
        text.long_("text").value_parser<std::string>();
        app.arg(text).arg(clapp::arg_builder{"count"}.short_('v').action(clapp::arg_action::count));
        app.arg(clapp::arg_builder{"optional"}.long_("optional"));
        return app.freeze();
    }
    inline constexpr auto core = make_core_command();
    inline constexpr auto chain = clapp::command_builder{"chain"}.version("1.0")
        .arg(clapp::arg_builder{"flag"}.long_("flag").action(clapp::arg_action::set_true)).freeze();
    inline constexpr auto response = clapp::command_builder{"response"}.expand_args_from_files()
        .arg(clapp::arg_builder{"value"}.long_("value")).freeze();
    inline constexpr auto nested = clapp::command_builder{"outer"}
        .subcommand(clapp::command_builder{"inner"}.styles(clapp::styles::styled())
            .color(clapp::color_choice::always)).freeze();
    inline constexpr auto plain_override = clapp::command_builder{"plain"}
        .styles(clapp::styles::plain()).color(clapp::color_choice::never).freeze();
    inline constexpr auto no_binary_response = clapp::command_builder{"response"}.no_binary_name()
        .expand_args_from_files().arg(clapp::arg_builder{"value"}.long_("value")).freeze();
    static_assert(std::same_as<decltype(std::declval<clapp::command_builder&>().version("x")),
                               clapp::command_builder&>);
    static_assert(std::same_as<decltype(std::declval<clapp::arg_builder&>().long_("x")),
                               clapp::arg_builder&>);
    static_assert(std::same_as<decltype(std::declval<clapp::group_builder&>().arg("x")),
                               clapp::group_builder&>);

    // All test files live under this worktree's build directory and are removed on scope exit.
    struct response_fixture {
        std::filesystem::path directory;
        response_fixture() {
            directory = std::filesystem::current_path() / "clapp-response-core-fixture";
            std::filesystem::create_directories(directory);
        }
        ~response_fixture() { std::filesystem::remove_all(directory); }
        void write(std::string_view name, std::string_view text) const {
            std::ofstream out{directory / name, std::ios::binary};
            out << text;
            CLAPP_CHECK(out.good());
        }
        std::string arg(std::string_view name) const { return "@" + (directory / name).string(); }
    };

    CLAPP_TEST("builder lvalue chaining and frozen rvalue chains") {
        CLAPP_CHECK(core.get_version() == "1.0");
        const auto matches = chain.try_get_matches({"chain", "--flag"});
        CLAPP_CHECK(matches && matches->get_flag("flag"));
        const char* argv[]{"core", "--text", "hello"};
        const auto parsed = core.try_get_matches(3, argv);
        CLAPP_CHECK(parsed);
        CLAPP_CHECK(parsed->get_or<std::string>("text", "fallback") == "hello");
        CLAPP_CHECK(parsed->get_expect<std::string>("text") == "hello");
        CLAPP_CHECK(parsed->get_copied<std::string>("text") == "hello");
        CLAPP_CHECK(parsed->get_or<std::string>("optional", "fallback") == "fallback");
        CLAPP_CHECK(!parsed->get_copied<std::string>("optional"));
        auto owning = core.get_matches({"core", "--text", "owned"});
        CLAPP_CHECK(owning.get_expect<std::string>("text") == "owned");
    }

    CLAPP_TEST("parse errors retain command ANSI styling and policy") {
        auto result = core.try_get_matches({"core", "--wat"});
        CLAPP_CHECK(!result);
        CLAPP_CHECK(result.error().render_ansi().contains("\x1b["));
        auto help = core.try_get_matches({"core", "--help"});
        CLAPP_CHECK(!help && help.error().exit_code() == 0);
        CLAPP_CHECK(help.error().render_ansi().contains("\x1b["));
        auto plain = clapp::error::raw(clapp::error_kind::unknown_argument,
                                      clapp::cow_str::borrowed("test"));
        CLAPP_CHECK(!plain.render_ansi().contains("\x1b["));
        plain.set_output(clapp::styles::styled(), clapp::color_choice::never);
        CLAPP_CHECK(!plain.render_ansi().contains("\x1b["));
    }

#if !defined(_WIN32)
    struct captured_output { std::string bytes; int status; };
    template<class F>
    captured_output capture(std::FILE* stream, F&& action) {
        int channel[2]{};
        CLAPP_CHECK(::pipe(channel) == 0);
        std::fflush(nullptr);
        const auto child = ::fork();
        CLAPP_CHECK(child >= 0);
        if (child == 0) {
            ::close(channel[0]);
            if (::dup2(channel[1], ::fileno(stream)) < 0) std::_Exit(99);
            ::close(channel[1]);
            action();
            std::exit(0);
        }
        ::close(channel[1]);
        std::string bytes;
        char buffer[1024];
        for (;;) {
            const auto count = ::read(channel[0], buffer, sizeof buffer);
            if (count > 0) bytes.append(buffer, static_cast<std::size_t>(count));
            else if (count < 0 && errno == EINTR) continue;
            else { CLAPP_CHECK(count == 0); break; }
        }
        ::close(channel[0]);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) CLAPP_CHECK(errno == EINTR);
        CLAPP_CHECK(WIFEXITED(status));
        return {std::move(bytes), WEXITSTATUS(status)};
    }

    CLAPP_TEST("error print exit and derive report preserve ANSI and exit status") {
        const auto parsed = core.try_get_matches({"core", "--wat"});
        CLAPP_CHECK(!parsed);
        const auto retained = capture(stderr, [&] { parsed.error().print(); });
        CLAPP_CHECK(retained.status == 0 && retained.bytes.contains("\x1b["));
        const auto override = capture(stderr, [&] { parsed.error().print(chain); });
        CLAPP_CHECK(override.status == 0 && !override.bytes.contains("\x1b["));
        const auto exited = capture(stderr, [&] { parsed.error().exit(); });
        CLAPP_CHECK(exited.status == 2 && exited.bytes.contains("\x1b["));
        const auto derived = capture(stderr, [&] { clapp::detail::report_and_exit(parsed.error(), core); });
        CLAPP_CHECK(derived.status == 2 && derived.bytes.contains("\x1b["));
        const auto help = core.try_get_matches({"core", "--help"});
        const auto printed_help = capture(stdout, [&] { help.error().exit(core); });
        CLAPP_CHECK(printed_help.status == 0 && printed_help.bytes.contains("\x1b["));
    }
#endif

    CLAPP_TEST("nested command errors and help retain the originating child policy") {
        const auto invalid = nested.try_get_matches({"outer", "inner", "--wat"});
        CLAPP_CHECK(!invalid && invalid.error().render_ansi().contains("\x1b["));
        const auto help = nested.try_get_matches({"outer", "inner", "--help"});
        CLAPP_CHECK(!help && help.error().exit_code() == 0);
        CLAPP_CHECK(help.error().render_ansi().contains("\x1b["));
        auto raw = clapp::error::raw(clapp::error_kind::unknown_argument,
                                    clapp::cow_str::borrowed("test"));
        raw.set_output_if_unset(clapp::styles::styled(), clapp::color_choice::always)
            .set_output_if_unset(clapp::styles::plain(), clapp::color_choice::never);
        CLAPP_CHECK(raw.render_ansi().contains("\x1b["));
#if !defined(_WIN32)
        const auto automatic_error = capture(stderr, [&] {
            static_cast<void>(nested.get_matches({"outer", "inner", "--wat"}));
        });
        CLAPP_CHECK(automatic_error.status == 2 && automatic_error.bytes.contains("\x1b["));
        const auto automatic_help = capture(stdout, [&] {
            static_cast<void>(nested.get_matches({"outer", "inner", "--help"}));
        });
        CLAPP_CHECK(automatic_help.status == 0 && automatic_help.bytes.contains("\x1b["));
        const auto derive_exit = capture(stderr, [&] {
            clapp::detail::report_and_exit(invalid.error(), nested);
        });
        CLAPP_CHECK(derive_exit.status == 2 && derive_exit.bytes.contains("\x1b["));
        const auto unbound = clapp::error::raw(clapp::error_kind::unknown_argument,
                                            clapp::cow_str::borrowed("test"));
        const auto fallback = capture(stderr, [&] { clapp::detail::report_and_exit(unbound, core); });
        CLAPP_CHECK(fallback.status == 2 && fallback.bytes.contains("\x1b["));
        const auto explicit_override = capture(stderr, [&] { invalid.error().print(plain_override); });
        CLAPP_CHECK(explicit_override.status == 0 && !explicit_override.bytes.contains("\x1b["));
#endif
    }

    CLAPP_TEST("logical count exceeds Builder uint8 presentation without wrapping") {
        std::vector<clapp::os_string> args{clapp::os_string{"core"}};
        for (int index = 0; index < 320; ++index) args.emplace_back("-v");
        const auto result = core.try_get_matches(clapp::raw_args{std::from_range, args});
        CLAPP_CHECK(result);
        CLAPP_CHECK(result->get_count("count") == 255);
        CLAPP_CHECK(result->occurrences_of("count") == 320);
    }

    CLAPP_TEST("response files quote escape comments relative recursion and custom prefix") {
        response_fixture fixture;
        fixture.write("outer", "one 'two words' \"three words\" escaped\\ space # comment\n@inner ''");
        fixture.write("inner", "four\n");
        const clapp::raw_args input{clapp::os_str{fixture.arg("outer")}};
        const auto expanded = input.expand_response_files();
        CLAPP_CHECK(expanded && expanded->size() == 6);
        const auto items = expanded->items();
        CLAPP_CHECK(items[0].view().chars() == "one");
        CLAPP_CHECK(items[1].view().chars() == "two words");
        CLAPP_CHECK(items[2].view().chars() == "three words");
        CLAPP_CHECK(items[3].view().chars() == "escaped space");
        CLAPP_CHECK(items[4].view().chars() == "four");
        CLAPP_CHECK(items[5].view().empty());
        std::string custom = fixture.arg("inner"); custom.front() = '%';
        CLAPP_CHECK(clapp::raw_args{clapp::os_str{custom}}.expand_response_files('%')->size() == 1);
    }

    CLAPP_TEST("response files integrate with parsing and no_binary_name") {
        response_fixture fixture;
        fixture.write("args", "--value 'a value'");
        const auto parsed = response.try_get_matches({"response", clapp::os_str{fixture.arg("args")}});
        CLAPP_CHECK(parsed && parsed->get_expect<std::string>("value") == "a value");
        const auto no_binary = no_binary_response.try_get_matches({clapp::os_str{fixture.arg("args")}});
        CLAPP_CHECK(no_binary && no_binary->get_expect<std::string>("value") == "a value");
    }

    CLAPP_TEST("response files reject cycles malformed text missing files and excessive depth") {
        response_fixture fixture;
        fixture.write("cycle", "@cycle");
        fixture.write("quote", "'unterminated");
        fixture.write("escape", "trailing\\");
        for (const auto name : {"cycle", "quote", "escape", "absent"}) {
            const auto result = clapp::raw_args{clapp::os_str{fixture.arg(name)}}.expand_response_files();
            CLAPP_CHECK(!result && result.error().kind() == clapp::error_kind::io);
        }
        for (int index = 0; index < 17; ++index)
            fixture.write(std::to_string(index), index == 16 ? "value" : "@" + std::to_string(index + 1));
        CLAPP_CHECK(!clapp::raw_args{clapp::os_str{fixture.arg("0")}}.expand_response_files());
        fixture.write("15", "value");
        CLAPP_CHECK(clapp::raw_args{clapp::os_str{fixture.arg("0")}}.expand_response_files());
    }
} // namespace
