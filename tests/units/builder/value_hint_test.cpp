#include <clapp/builder/value_hint.hpp>
#include <clapp/meta/annotations.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

    using clapp::value_hint;
    using namespace std::string_view_literals;

    // ---------------------------------------------------------------------------
    // One value_hint type, not two
    // ---------------------------------------------------------------------------

    // The annotation payload and the builder value are the same type; this header adds
    // behaviour only. Unlike clapp::arg_action there is no sentinel to reconcile, because
    // clap's own `Unknown` already means "no hint given".
    static_assert(std::is_same_v<std::underlying_type_t<value_hint>, unsigned char>);
    static_assert(value_hint{} == value_hint::unknown);
    static_assert(clapp::all_value_hints.size() == 13);
    static_assert(std::meta::enumerators_of(^^clapp::value_hint).size() ==
                  clapp::all_value_hints.size());

    // all_value_hints must be clap's order, so a value-enum expansion lists them the way
    // clap's documentation does.
    static_assert(clapp::all_value_hints.front() == value_hint::unknown);
    static_assert(clapp::all_value_hints[1] == value_hint::other);
    static_assert(clapp::all_value_hints[2] == value_hint::any_path);
    static_assert(clapp::all_value_hints.back() == value_hint::email_address);

    // ---------------------------------------------------------------------------
    // name_of — kebab-cased, one line per hint
    // ---------------------------------------------------------------------------

    static_assert(clapp::name_of(value_hint::unknown) == "unknown"sv);
    static_assert(clapp::name_of(value_hint::other) == "other"sv);
    static_assert(clapp::name_of(value_hint::any_path) == "any-path"sv);
    static_assert(clapp::name_of(value_hint::file_path) == "file-path"sv);
    static_assert(clapp::name_of(value_hint::dir_path) == "dir-path"sv);
    static_assert(clapp::name_of(value_hint::executable_path) == "executable-path"sv);
    static_assert(clapp::name_of(value_hint::command_name) == "command-name"sv);
    static_assert(clapp::name_of(value_hint::command_string) == "command-string"sv);
    static_assert(clapp::name_of(value_hint::command_with_arguments) == "command-with-arguments"sv);
    static_assert(clapp::name_of(value_hint::username) == "username"sv);
    static_assert(clapp::name_of(value_hint::hostname) == "hostname"sv);
    static_assert(clapp::name_of(value_hint::url) == "url"sv);
    static_assert(clapp::name_of(value_hint::email_address) == "email-address"sv);

    consteval bool hint_names_are_distinct_and_nonempty() {
        for (std::size_t i = 0; i < clapp::all_value_hints.size(); ++i) {
            if (clapp::name_of(clapp::all_value_hints[i]).empty()) return false;
            for (std::size_t j = i + 1; j < clapp::all_value_hints.size(); ++j) {
                if (clapp::name_of(clapp::all_value_hints[i]) ==
                    clapp::name_of(clapp::all_value_hints[j])) {
                    return false;
                }
            }
        }
        return true;
    }
    static_assert(hint_names_are_distinct_and_nonempty());

    // ---------------------------------------------------------------------------
    // parse_value_hint
    // ---------------------------------------------------------------------------

    // Every canonical name round-trips.
    consteval bool every_hint_round_trips() {
        for (const value_hint hint : clapp::all_value_hints) {
            if (clapp::parse_value_hint(clapp::name_of(hint)) != hint) return false;
        }
        return true;
    }
    static_assert(every_hint_round_trips());

    // Separators and ASCII case are both ignored, so kebab, snake, camel, Pascal and
    // clap's own squashed spelling all name the same hint.
    static_assert(clapp::parse_value_hint("any-path") == value_hint::any_path);
    static_assert(clapp::parse_value_hint("any_path") == value_hint::any_path);
    static_assert(clapp::parse_value_hint("anyPath") == value_hint::any_path);
    static_assert(clapp::parse_value_hint("AnyPath") == value_hint::any_path);
    static_assert(clapp::parse_value_hint("anypath") == value_hint::any_path);
    static_assert(clapp::parse_value_hint("ANYPATH") == value_hint::any_path);
    static_assert(clapp::parse_value_hint("-any--path-") == value_hint::any_path);

    // The spellings clap's own FromStr accepts, all thirteen of them, still work.
    static_assert(clapp::parse_value_hint("unknown") == value_hint::unknown);
    static_assert(clapp::parse_value_hint("other") == value_hint::other);
    static_assert(clapp::parse_value_hint("filepath") == value_hint::file_path);
    static_assert(clapp::parse_value_hint("dirpath") == value_hint::dir_path);
    static_assert(clapp::parse_value_hint("executablepath") == value_hint::executable_path);
    static_assert(clapp::parse_value_hint("commandname") == value_hint::command_name);
    static_assert(clapp::parse_value_hint("commandstring") == value_hint::command_string);
    static_assert(clapp::parse_value_hint("commandwitharguments") ==
                  value_hint::command_with_arguments);
    static_assert(clapp::parse_value_hint("username") == value_hint::username);
    static_assert(clapp::parse_value_hint("hostname") == value_hint::hostname);
    static_assert(clapp::parse_value_hint("url") == value_hint::url);
    static_assert(clapp::parse_value_hint("emailaddress") == value_hint::email_address);

    // Whitespace is not a separator, and neither is anything else.
    static_assert(clapp::parse_value_hint("any path") == std::nullopt);
    static_assert(clapp::parse_value_hint("any.path") == std::nullopt);
    static_assert(clapp::parse_value_hint("") == std::nullopt);
    static_assert(clapp::parse_value_hint("-") == std::nullopt);
    static_assert(clapp::parse_value_hint("path") == std::nullopt);
    static_assert(clapp::parse_value_hint("any-path-extra") == std::nullopt);

    // Ignoring separators must not let two different hints collide: `command-name` and
    // `commandname` are the same hint, but no two *distinct* hints may become equal.
    consteval bool loose_matching_keeps_hints_distinct() {
        for (std::size_t i = 0; i < clapp::all_value_hints.size(); ++i) {
            for (std::size_t j = i + 1; j < clapp::all_value_hints.size(); ++j) {
                if (clapp::detail::equals_ignore_separators(
                            clapp::name_of(clapp::all_value_hints[i]),
                            clapp::name_of(clapp::all_value_hints[j]))) {
                    return false;
                }
            }
        }
        return true;
    }
    static_assert(loose_matching_keeps_hints_distinct());

    // ---------------------------------------------------------------------------
    // equals_ignore_separators, on its own
    // ---------------------------------------------------------------------------

    static_assert(clapp::detail::equals_ignore_separators("", ""));
    static_assert(clapp::detail::equals_ignore_separators("-_-", ""));
    static_assert(clapp::detail::equals_ignore_separators("a-b", "AB"));
    static_assert(clapp::detail::equals_ignore_separators("a", "a-"));
    static_assert(!clapp::detail::equals_ignore_separators("ab", "abc"));
    static_assert(!clapp::detail::equals_ignore_separators("abc", "ab"));
    static_assert(!clapp::detail::equals_ignore_separators("a", ""));

    // ---------------------------------------------------------------------------
    // Predicates
    // ---------------------------------------------------------------------------

    static_assert(clapp::is_path(value_hint::any_path));
    static_assert(clapp::is_path(value_hint::file_path));
    static_assert(clapp::is_path(value_hint::dir_path));
    static_assert(clapp::is_path(value_hint::executable_path));
    static_assert(!clapp::is_path(value_hint::unknown));
    static_assert(!clapp::is_path(value_hint::other));
    static_assert(!clapp::is_path(value_hint::command_name));
    static_assert(!clapp::is_path(value_hint::command_string));
    static_assert(!clapp::is_path(value_hint::command_with_arguments));
    static_assert(!clapp::is_path(value_hint::username));
    static_assert(!clapp::is_path(value_hint::hostname));
    static_assert(!clapp::is_path(value_hint::url));
    static_assert(!clapp::is_path(value_hint::email_address));

    // `command_name` is a path to an executable in practice, but it is not a path *hint*:
    // zsh completes it from PATH rather than from the filesystem, so grouping it with the
    // four would send the completion generator down the wrong branch.
    static_assert(!clapp::is_path(value_hint::command_name));

    // Exactly one hint constrains how its argument must be configured.
    consteval bool only_one_hint_constrains_its_argument() {
        std::size_t constraining = 0;
        for (const value_hint hint : clapp::all_value_hints) {
            if (clapp::requires_trailing_var_arg(hint)) ++constraining;
        }
        return constraining == 1;
    }
    static_assert(only_one_hint_constrains_its_argument());
    static_assert(clapp::requires_trailing_var_arg(value_hint::command_with_arguments));
    static_assert(!clapp::requires_trailing_var_arg(value_hint::command_string));

}  // namespace

CLAPP_TEST("value_hint: clapp's thirteen are clap's thirteen") {
    CLAPP_CHECK(clapp::all_value_hints.size() == 13);
    CLAPP_CHECK(value_hint{} == value_hint::unknown);
}

CLAPP_TEST("value_hint: names round-trip through parse_value_hint") {
    // std::string and std::vector are transient allocations and cannot be constexpr
    // variables, so the round trip gets its runtime witness with owning strings.
    std::vector<std::string> names;
    for (const value_hint hint : clapp::all_value_hints) {
        names.emplace_back(clapp::name_of(hint));
    }
    CLAPP_CHECK(names.size() == 13);
    for (std::size_t i = 0; i < names.size(); ++i) {
        CLAPP_CHECK(clapp::parse_value_hint(names[i]) == clapp::all_value_hints[i]);
    }
}

CLAPP_TEST("value_hint: a spelling clap accepts is still accepted") {
    const std::string squashed = "commandwitharguments";
    CLAPP_CHECK(clapp::parse_value_hint(squashed) == value_hint::command_with_arguments);
    CLAPP_CHECK(clapp::parse_value_hint(std::string{"CommandWithArguments"}) ==
                value_hint::command_with_arguments);
}

CLAPP_TEST("value_hint: unrecognized text yields nullopt rather than a default") {
    CLAPP_CHECK(clapp::parse_value_hint(std::string{"nonesuch"}) == std::nullopt);
    CLAPP_CHECK(clapp::parse_value_hint(std::string{}) == std::nullopt);
}

CLAPP_TEST("value_hint: exactly four hints complete a filesystem path") {
    std::vector<value_hint> paths;
    for (const value_hint hint : clapp::all_value_hints) {
        if (clapp::is_path(hint)) paths.push_back(hint);
    }
    CLAPP_CHECK(paths.size() == 4);
    CLAPP_CHECK(paths.front() == value_hint::any_path);
    CLAPP_CHECK(paths.back() == value_hint::executable_path);
}
