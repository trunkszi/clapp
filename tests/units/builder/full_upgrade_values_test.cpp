#include <clapp/builder/value_parser.hpp>
#include <clapp/output/complete.hpp>
#include <clapp/util/match.hpp>

#include "support/check.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {
    enum class annotated_mode {
        fast[[= clapp::value{.name = "quick", .alias = "rapid", .help = "Run quickly"}]],
        slow,
        secret[[= clapp::value{.alias = "hidden", .hide = true}]],
    };

    using bool_range = clapp::ranged<bool, false, true>;
    static_assert(clapp::value_parser<bool_range>::parse(clapp::os_str{"true"})->value);
    using port = clapp::ranged<unsigned, 1, 65535>;
    static_assert(clapp::parsable<port>);
    static_assert(clapp::value_parser<port>::parse(clapp::os_str{"1"})->value == 1);
    static_assert(clapp::value_parser<port>::parse(clapp::os_str{"65535"})->value == 65535);
    static_assert(!clapp::value_parser<port>::parse(clapp::os_str{"0"}));
    static_assert(!clapp::value_parser<port>::parse(clapp::os_str{"65536"}));
    static_assert(clapp::value_parser<port>::parse(clapp::os_str{"0"}).error().domain == "1..=65535");
    static_assert(clapp::value_parser<annotated_mode>::parse(clapp::os_str{"rapid"}) == annotated_mode::fast);
    static_assert(clapp::parse_value<annotated_mode>(clapp::os_str{"RAPID"}, true) == annotated_mode::fast);
    static_assert(!clapp::parse_value<annotated_mode>(clapp::os_str{"RAPID"}));
    static_assert(clapp::value_parser<annotated_mode>::possible_values()[0].get_name() == "quick");
    static_assert(clapp::value_parser<annotated_mode>::possible_values()[0].get_help() == "Run quickly");
    static_assert(clapp::value_parser<annotated_mode>::possible_values()[2].is_hide_set());
    static_assert(clapp::value_parser<annotated_mode>::parse(clapp::os_str{"hidden"}) == annotated_mode::secret);
    static_assert(clapp::value_parser<clapp::shell>::parse(clapp::os_str{"powershell"}) == clapp::shell::powershell);

    template<class T>
    auto parse_duration(std::string_view text) {
        return clapp::value_parser<T>::parse(clapp::os_str{text});
    }
}

CLAPP_TEST("ranged parses boundaries and preserves numeric errors") {
    CLAPP_CHECK(clapp::value_parser<port>::parse(clapp::os_str{"12"})->value == 12);
    CLAPP_CHECK(clapp::value_parser<port>::parse(clapp::os_str{"junk"}).error().kind == clapp::parse_error_kind::invalid_digit);
    using signed_range = clapp::ranged<int, -10, 10>;
    CLAPP_CHECK(clapp::value_parser<signed_range>::parse(clapp::os_str{"-10"})->value == -10);
    CLAPP_CHECK(clapp::value_parser<signed_range>::parse(clapp::os_str{"-11"}).error().domain == "-10..=10");
}

CLAPP_TEST("duration units scale to exact target ticks") {
    using namespace std::chrono;
    CLAPP_CHECK(parse_duration<nanoseconds>("1ns")->count() == 1);
    CLAPP_CHECK(parse_duration<nanoseconds>("2us")->count() == 2000);
    CLAPP_CHECK(parse_duration<nanoseconds>("2µs")->count() == 2000);
    CLAPP_CHECK(parse_duration<milliseconds>("1.5s")->count() == 1500);
    CLAPP_CHECK(parse_duration<nanoseconds>("0.000000001s")->count() == 1);
    CLAPP_CHECK(parse_duration<milliseconds>("0.1s")->count() == 100);
    CLAPP_CHECK(parse_duration<milliseconds>("1e-3s")->count() == 1);
    CLAPP_CHECK(parse_duration<milliseconds>("+2s")->count() == 2000);
    CLAPP_CHECK(parse_duration<seconds>("2m")->count() == 120);
    CLAPP_CHECK(parse_duration<seconds>("2h")->count() == 7200);
    CLAPP_CHECK(parse_duration<milliseconds>("7")->count() == 7);
    CLAPP_CHECK(parse_duration<seconds>("-2s")->count() == -2);
    CLAPP_CHECK(parse_duration<std::chrono::duration<double>>("500ms")->count() == 0.5);
}

CLAPP_TEST("duration rejects malformed units fractional ticks nonfinite and overflow") {
    using namespace std::chrono;
    for (std::string_view bad : {"", "1day", "3 s", "nan", "inf", "1xs", "--1s", "+-1s", "++1s", "1.2.3s"}) {
        CLAPP_CHECK(!parse_duration<seconds>(bad));
    }
    CLAPP_CHECK(!parse_duration<seconds>("1ms"));
    CLAPP_CHECK(!parse_duration<seconds>("1.00000000000000000001s"));
    CLAPP_CHECK(parse_duration<std::chrono::duration<std::uint64_t>>("18446744073709551615s")->count() == std::numeric_limits<std::uint64_t>::max());
    CLAPP_CHECK(parse_duration<std::chrono::duration<std::int64_t>>("-9223372036854775808s")->count() == std::numeric_limits<std::int64_t>::lowest());
    CLAPP_CHECK(!parse_duration<std::chrono::duration<unsigned>>("-1s"));
    CLAPP_CHECK(!parse_duration<std::chrono::duration<std::int8_t>>("128s"));
    CLAPP_CHECK(parse_duration<std::chrono::duration<std::int8_t>>("127s")->count() == 127);
    CLAPP_CHECK(parse_duration<std::chrono::duration<std::int8_t>>("-128s")->count() == -128);
    CLAPP_CHECK(!parse_duration<std::chrono::duration<std::int8_t>>("-129s"));
    CLAPP_CHECK(!parse_duration<std::chrono::duration<std::uint64_t>>("18446744073709551616s"));
    CLAPP_CHECK(!parse_duration<seconds>("1e1000h"));
}

CLAPP_TEST("match forwards variant and optional values including move only values") {
    std::variant<int, std::string> value = std::string("hello");
    CLAPP_CHECK(clapp::match(value, [](int number) { return std::to_string(number); },
                                  [](const std::string& text) { return text; }) == "hello");
    clapp::match(value, [](int&) {}, [](std::string& text) { text += "!"; });
    CLAPP_CHECK(std::get<std::string>(value) == "hello!");
    std::optional<std::variant<int, std::string>> maybe;
    CLAPP_CHECK(!clapp::match(maybe, [](int) { return 1; }, [](const std::string&) { return 2; }));
    maybe.emplace(7);
    CLAPP_CHECK(clapp::match(maybe, [](int n) { return n; }, [](const std::string&) { return 0; }) == 7);
    bool called = false;
    CLAPP_CHECK(clapp::match(maybe, [&](int) { called = true; }, [](const std::string&) {}));
    CLAPP_CHECK(called);
    maybe.reset();
    CLAPP_CHECK(!clapp::match(maybe, [](int) {}, [](const std::string&) {}));
    std::variant<std::unique_ptr<int>> movable{std::make_unique<int>(42)};
    auto moved = clapp::match(std::move(movable), [](std::unique_ptr<int> p) { return p; });
    CLAPP_CHECK(*moved == 42);
    CLAPP_CHECK(!std::get<0>(movable));
}
