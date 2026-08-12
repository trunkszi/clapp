#include <clapp/clapp.hpp>

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// ===========================================================================
// Values that need nothing but their own declaration
// ===========================================================================

namespace {
    /** \brief How far to move a version number. A plain enum; no `ValueEnum` in sight. */
    enum class bump_level : unsigned char {
        major, /**< Increase the major version (x.0.0) */
        minor, /**< Increase the minor version (x.y.0) */
        patch, /**< Increase the patch version (x.y.z) */
    };

    /** \brief Log verbosity, showing the three things `[[= clapp::value{...}]]` is for. */
    enum class log_level : unsigned char {
        trace,
        debug,
        info,
        warn[[= clapp::value{.help = "Only warnings and errors"}]],
        error,
        /**
     * Renamed *and* hidden: `--log-level off` still works and is still matched, it is
     * merely kept out of help. That is clap's behaviour for a hidden possible value.
     */
        quiet[[= clapp::value{.name = "off", .hide = true}]],
    };
}

/**
 * \brief Spell a #bump_level the way the command line does.
 * \param level The parsed value.
 * \return A view into a string literal.
 */
[[nodiscard]] static constexpr std::string_view name_of(const bump_level level) noexcept {
    switch (level) {
        case bump_level::major:
            return "major";
        case bump_level::minor:
            return "minor";
        case bump_level::patch:
            return "patch";
    }
    return "?";
}

/**
 * \brief Spell a #log_level the way the command line does.
 * \param level The parsed value.
 * \return A view into a string literal; `quiet` reports its renamed spelling.
 */
[[nodiscard]] static constexpr std::string_view name_of(const log_level level) noexcept {
    switch (level) {
        case log_level::trace:
            return "trace";
        case log_level::debug:
            return "debug";
        case log_level::info:
            return "info";
        case log_level::warn:
            return "warn";
        case log_level::error:
            return "error";
        case log_level::quiet:
            return "off";
    }
    return "?";
}

// ===========================================================================
// Custom types, and the clapp::value_parser specializations that teach clapp about them
// ===========================================================================

namespace {
    /** \brief One `KEY=VALUE` pair, clap's `parse_key_val::<String, i32>`. */
    struct key_value {
        std::string key;
        int value = 0;
    };

    /** \brief Either a #bump_level or a literal version string, clap's `TargetVersion`. */
    struct target_version {
        /** Empty when #absolute holds a literal version instead. */
        std::optional<bump_level> relative{};
        /** Meaningful only when #relative is empty. */
        std::string absolute{};
    };

    /** \brief A TCP port restricted to a listed set, clap's `PossibleValuesParser`. */
    struct port_number {
        unsigned value = 0;
    };
}

/**
 * \brief Parse `KEY=VALUE` into a #key_value.
 *
 * \note The simplest shape a specialization can have: one static `parse`, no
 *       `possible_values()`, because the domain is not enumerable. Delegating the
 *       right-hand side to `clapp::value_parser<int>` is what keeps `-D k=x` reporting
 *       "invalid digit found in string" — the same sentence a bare `int` argument
 *       produces — instead of a second, differently worded message.
 */
template<>
struct clapp::value_parser<key_value> {
    /**
     * \brief Split at the first `=` and parse the tail as an `int`.
     * \param value The raw bytes from the command line.
     * \return The pair, or the error that explains which half was wrong.
     */
    [[nodiscard]] static constexpr std::expected<key_value, clapp::parse_error>
    parse(const clapp::os_str value) {
        const std::optional<std::pair<clapp::os_str, clapp::os_str> > halves = value.split_once('=');
        if (!halves.has_value())
            return std::unexpected(
                clapp::parse_error{
                    .kind = clapp::parse_error_kind::invalid_value,
                    .input = value,
                    .type_name = "key_value",
                    .reason = "no `=` found in the value"
                });

        const std::expected<std::string_view, clapp::invalid_encoding> key =
                halves->first.to_string_view();
        if (!key.has_value())
            return std::unexpected(clapp::parse_error{
                .kind = clapp::parse_error_kind::invalid_utf8,
                .input = value,
                .type_name = "key_value",
                .encoding = key.error()
            });

        const std::expected<int, clapp::parse_error> number =
                clapp::value_parser<int>::parse(halves->second);
        if (!number.has_value()) return std::unexpected(number.error());

        return key_value{.key = std::string{*key}, .value = *number};
    }
};

/**
 * \brief Parse a #target_version: one of the #bump_level spellings, or anything else.
 *
 * \note The second shape: a specialization that also publishes `possible_values()`. That
 *       member is what puts `[possible values: major, minor, patch]` into an error and
 *       into help — but note that this parser *accepts* values outside the list, exactly
 *       as clap's `TargetVersionParser` does, so the list is a hint rather than a
 *       contract. clapp never re-validates against it; the parser is the only authority.
 */
template<>
struct clapp::value_parser<target_version> {
    /** The three relative spellings, for help and for error messages. */
    static constexpr std::array<clapp::possible_value, 3> levels{
        clapp::possible_value{.name = clapp::arg_id{"major"}},
        clapp::possible_value{.name = clapp::arg_id{"minor"}},
        clapp::possible_value{.name = clapp::arg_id{"patch"}},
    };

    /**
     * \brief Convert \p value into a #target_version.
     * \param value The raw bytes from the command line.
     * \return A relative bump when the value names one, an absolute version otherwise.
     */
    [[nodiscard]] static constexpr std::expected<target_version, clapp::parse_error>
    parse(clapp::os_str value) {
        const std::expected<std::string_view, clapp::invalid_encoding> text =
                value.to_string_view();
        if (!text.has_value())
            return std::unexpected(clapp::parse_error{
                .kind = clapp::parse_error_kind::invalid_utf8,
                .input = value,
                .type_name = "target_version",
                .encoding = text.error()
            });

        if (const std::expected<bump_level, clapp::parse_error> level =
                    clapp::value_parser<bump_level>::parse(value);
            level.has_value())
            return target_version{.relative = *level};

        if (text->empty())
            return std::unexpected(clapp::parse_error{
                .kind = clapp::parse_error_kind::empty_value,
                .input = value,
                .type_name = "target_version",
                .possible = levels
            });

        return target_version{.absolute = std::string{*text}};
    }

    /**
     * \brief The spellings help is allowed to list.
     * \return A span over static storage.
     */
    [[nodiscard]] static constexpr std::span<const clapp::possible_value>
    possible_values() noexcept {
        return levels;
    }
};

/**
 * \brief Parse a #port_number, accepting only the listed ports.
 *
 * \note clap writes this as `PossibleValuesParser::new(["22", "80"]).map(...)`. The point
 *       of porting it is the *rejection* path: `--port 443` must name the accepted set,
 *       which is what parse_error::possible carries.
 */
template<>
struct clapp::value_parser<port_number> {
    /** The accepted ports. */
    static constexpr std::array<clapp::possible_value, 2> ports{
        clapp::possible_value{.name = clapp::arg_id{"22"}},
        clapp::possible_value{.name = clapp::arg_id{"80"}},
    };

    /**
     * \brief Convert \p value into a #port_number.
     * \param value The raw bytes from the command line.
     * \return The port, or an clapp::parse_error_kind::invalid_value carrying #ports.
     */
    [[nodiscard]] static constexpr std::expected<port_number, clapp::parse_error>
    parse(clapp::os_str value) {
        for (const clapp::possible_value &candidate: ports) {
            if (!candidate.matches(value.chars(), false)) continue;
            const std::expected<unsigned, clapp::parse_error> number =
                    clapp::value_parser<unsigned>::parse(value);
            if (!number.has_value()) return std::unexpected(number.error());
            return port_number{.value = *number};
        }
        return std::unexpected(clapp::parse_error{
            .kind = clapp::parse_error_kind::invalid_value,
            .input = value,
            .type_name = "port_number",
            .possible = ports
        });
    }

    /**
     * \brief The accepted ports, for help and for "did you mean".
     * \return A span over static storage.
     */
    [[nodiscard]] static constexpr std::span<const clapp::possible_value>
    possible_values() noexcept {
        return ports;
    }
};

// ===========================================================================
// The command tree
// ===========================================================================

namespace {
    /** \brief `typed implicit` — types that need nothing beyond being named. */
    struct[[= clapp::cmd{.name = "implicit", .about = "Types clapp already knows how to parse"}]]
            implicit_parsers {
        [[= clapp::arg{
            .short_ = 'O',
            .no_long = true,
            .help = "Optimization level"
        }]] std::optional<unsigned long>
        optimization;

        [[= clapp::arg{
            .short_ = 'I',
            .no_long = true,
            .hint = clapp::value_hint::dir_path,
            .help = "Include directory",
            .value_name = "DIR"
        }]] std::optional<std::filesystem::path>
        include;

        [[= clapp::arg{
            .long_ = "sleep",
            .help = "Seconds to sleep",
            .value_name = "SECONDS"
        }]] std::optional<double>
        sleep;

        [[= clapp::arg{
            .long_ = "bump-level",
            .help = "How far to move the version"
        }]] std::optional<bump_level>
        bump;
    };
}

namespace {
    /** \brief `typed builtin` — restricted domains and fixed arities. */
    struct[[= clapp::cmd{.name = "builtin", .about = "Restricted value sets and fixed arities"}]]
            builtin_parsers {
        [[= clapp::arg{
            .long_ = "port", .help = "Port to listen on", .default_value = "22"
        }]] port_number port;

        [[= clapp::arg{
            .long_ = "log-level",
            .help = "Log verbosity",
            .default_value = "info"
        }]] log_level level;

        /**
     * Exactly two values, and they need not have the same type — the arity comes from
     * `std::pair`, not from an annotation.
     *
     * \note The member initializer is load-bearing twice over. It supplies the default,
     *       and it is the *only* way to make this argument optional: the deduction table
     *       has no `std::optional<std::pair<...>>` row, so wrapping it would be a field
     *       with no clapp::value_parser and a compile error naming `bind`. Same for
     *       #origin.
     */
        [[= clapp::arg{
            .long_ = "bind",
            .help = "Host and port to bind",
            .value_name = "HOST PORT"
        }]] std::pair<std::string, unsigned>
        bind{"localhost", 0};

        /** Exactly three, from `std::array`'s extent. */
        [[= clapp::arg{.long_ = "origin", .help = "Three coordinates"}]] std::array<int, 3> origin{
            0, 0, 0
        };
    };

    /** \brief `typed fn-parser` — one hand-written clapp::value_parser, used repeatedly. */
    struct[[= clapp::cmd{.name = "fn-parser", .about = "A hand-written value_parser for KEY=VALUE"}]]
            fn_parser {
        [[= clapp::arg{
            .short_ = 'D',
            .no_long = true,
            .help = "Preprocessor definition",
            .value_name = "KEY=VALUE"
        }]] std::vector<key_value>
        defines;
    };

    /** \brief `typed custom` — a clapp::value_parser that also publishes its domain. */
    struct[[= clapp::cmd{.name = "custom", .about = "A value_parser that publishes possible_values()"}]]
            custom_parser {
        [[= clapp::arg{
            .long_ = "target-version",
            .help = "A bump level or a literal version"
        }]] std::optional<target_version>
        target;
    };

    /** \brief The root. */
    struct[[= clapp::cmd{.name = "typed", .version = "1.0.0", .about = "Typed values, four ways"}]]
            cli {
        [[= clapp::subcommand{}]] std::
        variant<implicit_parsers, builtin_parsers, fn_parser, custom_parser>
        command;
    };
}

// ===========================================================================
// Program
// ===========================================================================

namespace {
    /**
     * \brief Render an optional as `Some(x)` / `None`, so absence is visible in the output.
     * \tparam T The contained type.
     * \param value The optional to render.
     * \param render How to turn a `T` into text.
     * \return The rendered form.
     */
    template<class T, class F>
    [[nodiscard]] std::string some_or_none(const std::optional<T> &value, F render) {
        return value.has_value() ? "Some(" + render(*value) + ")" : std::string{"None"};
    }

    void report(const implicit_parsers &args) {
        std::println("optimization: {}", some_or_none(args.optimization, [](unsigned long v) {
            return std::to_string(v);
        }));
        std::println("include: {}", some_or_none(args.include, [](const std::filesystem::path &p) {
            return p.string();
        }));
        std::println("sleep: {}",
                     some_or_none(args.sleep, [](double v) { return std::format("{}", v); }));
        std::println("bump-level: {}",
                     some_or_none(args.bump, [](bump_level v) { return std::string{name_of(v)}; }));
    }

    void report(const builtin_parsers &args) {
        std::println("port: {}", args.port.value);
        std::println("log-level: {}", name_of(args.level));
        std::println("bind: {}:{}", args.bind.first, args.bind.second);
        std::println("origin: {},{},{}", args.origin[0], args.origin[1], args.origin[2]);
    }

    void report(const fn_parser &args) {
        std::println("defines: {}", args.defines.size());
        for (const key_value &define: args.defines)
            std::println("  {} = {}", define.key, define.value);
    }

    void report(const custom_parser &args) {
        if (!args.target.has_value()) {
            std::println("target-version: None");
            return;
        }
        if (args.target->relative.has_value())
            std::println("target-version: Relative({})", name_of(*args.target->relative));
        else
            std::println("target-version: Absolute({})", args.target->absolute);
    }
} // namespace

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);
    std::visit([](const auto &chosen) { report(chosen); }, args.command);
}
