#include <clapp/derive.hpp>
#include <clapp/meta/cstr.hpp>

#include "support/check.hpp"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <variant>
#include <vector>

static_assert(clapp::value_range::exact(3) == clapp::value_range::exactly(3));

namespace store_upgrade {
enum class mode { red, blue };
struct pairs {
    [[= clapp::arg{.ignore_case = true}]] std::pair<mode, int> pair;
};
struct tuples {
    [[= clapp::arg{.ignore_case = true}]] std::tuple<mode, int, std::string> tuple;
};
struct sets {
    std::set<int> ordered;
    std::unordered_set<std::string> hashed;
};
struct narrow_count {
    [[= clapp::arg{.short_ = 'v', .act = clapp::action::count}]] std::int8_t verbose{};
};
struct wide_count {
    [[= clapp::arg{.short_ = 'v', .act = clapp::action::count}]] std::uint32_t verbose{};
};
struct known { bool fast{}; };
struct [[= clapp::external_subcommand{}]] external_one { std::vector<std::string> values; };
struct [[= clapp::external_subcommand{}]] external_two {
    clapp::os_string name;
    std::vector<clapp::os_string> values;
};
struct external_cli_one { std::variant<external_one, known> command; };
struct external_cli_two { std::variant<known, external_two> command; };
}

CLAPP_TEST("pair parser honors ignore case and returns enum choices with suggestions") {
    auto good = clapp::try_parse_from<store_upgrade::pairs>({"app", "--pair", "BLUE", "42"});
    CLAPP_CHECK(good);
    CLAPP_CHECK(good->pair.first == store_upgrade::mode::blue);
    CLAPP_CHECK(good->pair.second == 42);
    auto bad = clapp::try_parse_from<store_upgrade::pairs>({"app", "--pair", "blu", "42"});
    CLAPP_CHECK(!bad);
    CLAPP_CHECK(bad.error().kind() == clapp::error_kind::invalid_value);
    const auto text = bad.error().render().to_string();
    CLAPP_CHECK(text.find("possible values") != std::string::npos);
    CLAPP_CHECK(text.find("blue") != std::string::npos);
    CLAPP_CHECK(text.find("similar") != std::string::npos);
}

CLAPP_TEST("heterogeneous tuples parse each element with field case policy") {
    auto good = clapp::try_parse_from<store_upgrade::tuples>({"app", "--tuple", "RED", "17", "payload"});
    CLAPP_CHECK(good);
    CLAPP_CHECK(std::get<0>(good->tuple) == store_upgrade::mode::red);
    CLAPP_CHECK(std::get<1>(good->tuple) == 17);
    CLAPP_CHECK(std::get<2>(good->tuple) == "payload");
    auto bad = clapp::try_parse_from<store_upgrade::tuples>({"app", "--tuple", "bleu", "17", "x"});
    CLAPP_CHECK(!bad);
    CLAPP_CHECK(bad.error().kind() == clapp::error_kind::invalid_value);
}

CLAPP_TEST("ordered and unordered set fields insert and deduplicate") {
    auto got = clapp::try_parse_from<store_upgrade::sets>({"app", "--ordered", "3", "1", "3", "--hashed", "x", "y", "x"});
    CLAPP_CHECK(got);
    CLAPP_CHECK(got->ordered == std::set<int>({1, 3}));
    CLAPP_CHECK(got->hashed.size() == 2);
    CLAPP_CHECK(got->hashed.contains("y"));
}

CLAPP_TEST("narrow signed counts saturate while wide derive counts exceed builder byte count") {
    const std::string flags = "-" + std::string(300, 'v');
    const clapp::raw_args raw({clapp::os_str{"app"}, clapp::os_str{flags}});
    auto small = clapp::try_parse_from<store_upgrade::narrow_count>(raw);
    auto large = clapp::try_parse_from<store_upgrade::wide_count>(raw);
    CLAPP_CHECK(small);
    CLAPP_CHECK(large);
    CLAPP_CHECK(small->verbose == std::numeric_limits<std::int8_t>::max());
    CLAPP_CHECK(large->verbose == 300);
}

CLAPP_TEST("external vector alternative captures name and raw tail without eclipsing named commands") {
    auto got = clapp::try_parse_from<store_upgrade::external_cli_one>({"app", "plugin", "--flag", "payload"});
    CLAPP_CHECK(got);
    CLAPP_CHECK(std::get<store_upgrade::external_one>(got->command).values ==
                std::vector<std::string>({"plugin", "--flag", "payload"}));
    auto named = clapp::try_parse_from<store_upgrade::external_cli_one>({"app", "known", "--fast"});
    CLAPP_CHECK(named);
    CLAPP_CHECK(std::get<store_upgrade::known>(named->command).fast);
}

CLAPP_TEST("external name plus OS vector alternative separates command and arguments") {
    auto got = clapp::try_parse_from<store_upgrade::external_cli_two>({"app", "plugin", "--flag", "payload"});
    CLAPP_CHECK(got);
    const auto& external = std::get<store_upgrade::external_two>(got->command);
    CLAPP_CHECK(external.name.view().to_string_lossy() == "plugin");
    CLAPP_CHECK(external.values.size() == 2);
    CLAPP_CHECK(external.values[0].view().to_string_lossy() == "--flag");
}

namespace store_upgrade {
struct no_default {
    no_default() = delete;
    explicit no_default(int v) : value(v) {}
    int value;
};
}
namespace clapp {
template<> struct value_parser<store_upgrade::no_default> {
    static std::expected<store_upgrade::no_default, parse_error> parse(os_str raw) {
        return value_parser<int>::parse(raw).transform([](int value) { return store_upgrade::no_default{value}; });
    }
};
}
namespace store_upgrade {
struct initialized_products {
    std::pair<no_default, int> pair{no_default{1}, 2};
    std::tuple<no_default, std::string> tuple{no_default{3}, "default"};
};
}
CLAPP_TEST("pair and tuple parsers preserve support for non-default-constructible elements") {
    auto got = clapp::try_parse_from<store_upgrade::initialized_products>({"app", "--pair", "9", "10", "--tuple", "11", "text"});
    CLAPP_CHECK(got);
    CLAPP_CHECK(got->pair.first.value == 9 && got->pair.second == 10);
    CLAPP_CHECK(std::get<0>(got->tuple).value == 11 && std::get<1>(got->tuple) == "text");
}

namespace store_upgrade {
struct [[= clapp::external_subcommand{}]] cached_external {
    [[= clapp::skip{}]] int cache = 10;
    std::vector<std::string> args;
};
struct cached_external_cli { std::variant<cached_external> command; };
}
CLAPP_TEST("external updates replace argument payload while preserving skipped state") {
    auto initial = clapp::try_parse_from<store_upgrade::cached_external_cli>({"app", "old", "first"});
    CLAPP_CHECK(initial);
    std::get<store_upgrade::cached_external>(initial->command).cache = 77;
    auto updated = clapp::try_update_from(*initial, {"app", "new", "second"});
    CLAPP_CHECK(updated);
    const auto& external = std::get<store_upgrade::cached_external>(initial->command);
    CLAPP_CHECK(external.cache == 77);
    CLAPP_CHECK(external.args == std::vector<std::string>({"new", "second"}));
}
