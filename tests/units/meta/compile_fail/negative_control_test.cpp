#include <clapp/meta/parse.hpp>

#include <expected>
#include <string>
#include <variant>

// --------------------------------------------------------------------------
// The near-miss of no_value_parser_test.cpp: a user type that CAN be parsed.
// --------------------------------------------------------------------------
namespace {

struct pattern {
    std::string text;
};

}  // namespace

template<>
struct clapp::value_parser<pattern> {
    [[nodiscard]] static constexpr std::expected<pattern, parse_error> parse(os_str value) {
        const std::expected<std::string_view, invalid_encoding> text = value.to_string_view();
        if (!text.has_value()) {
            return std::unexpected(parse_error{.kind      = parse_error_kind::invalid_value,
                                               .input     = value,
                                               .type_name = "pattern",
                                               .reason    = "invalid UTF-8"});
        }
        return pattern{std::string(*text)};
    }
};

// --------------------------------------------------------------------------
// The near-miss of flatten_on_non_aggregate_test.cpp: a real aggregate.
// --------------------------------------------------------------------------
namespace {

struct verbosity {
    [[= clapp::arg{.short_ = 'q'}]] bool quiet;
};

// --------------------------------------------------------------------------
// The near-miss of subcommand_on_non_variant_test.cpp: a real std::variant.
// --------------------------------------------------------------------------
struct add_command {
    [[= clapp::arg{.index = 1}]] std::string path;
};

struct remove_command {
    [[= clapp::arg{.index = 1}]] std::string path;
};

struct cli {
    /** Derives `--dry-run`; `mode` below derives `--mode`. No collision. */
    bool dry_run;
    [[= clapp::arg{.short_ = 'm'}]] std::string mode;

    /** A parsable user type, so check_fields_parsable() has nothing to say. */
    pattern filter;

    /** Two arguments in one group, both of which exist. */
    [[= clapp::arg{.short_ = 'j', .group = "output"}]] std::string json;
    [[= clapp::arg{.short_ = 'p', .group = "output"}]] std::string plain;

    /** Positional indices 1 and 2: contiguous, distinct. */
    [[= clapp::arg{.index = 1}]] std::string source;
    [[= clapp::arg{.index = 2}]] std::string destination;

    [[= clapp::flatten{}]] verbosity noise;

    [[= clapp::subcommand{}]] std::variant<add_command, remove_command> command;
};

}  // namespace

// The forward direction, frozen at compile time. If any guard started rejecting a valid
// definition, this initializer is where it would show.
static constexpr clapp::command_spec spec = clapp::command_of<cli>();
static_assert(clapp::parsable_command<cli>);
static_assert(spec.has_arg("source"));
static_assert(spec.has_arg("quiet"));
// By name, not by count: freeze() also injects a `help` subcommand, and a count would
// pin that injection rather than the two alternatives this file declares.
static_assert(spec.has_subcommand("add-command"));
static_assert(spec.has_subcommand("remove-command"));

int main(int argc, char** argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);
    bool has_value = parsed.dry_run || !parsed.mode.empty() || !parsed.filter.text.empty()
                     || !parsed.json.empty() || !parsed.plain.empty()
                     || !parsed.source.empty() || !parsed.destination.empty()
                     || parsed.noise.quiet;
    if (const add_command* add = std::get_if<add_command>(&parsed.command)) {
        has_value = has_value || !add->path.empty();
    } else if (const remove_command* remove = std::get_if<remove_command>(&parsed.command)) {
        has_value = has_value || !remove->path.empty();
    }
    return has_value ? 0 : 1;
}
