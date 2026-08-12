#include <clapp/clapp.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>

namespace {
    /** \brief How fast to run. A plain enum: the accepted spellings come from reflection. */
    enum class run_mode : unsigned char {
        fast[[= clapp::value{.help = "Run swiftly"}]],
        slow[[= clapp::value{.help = "Crawl slowly but steadily"}]],
    };

    /**
     * \brief Spell a #run_mode the way the command line does.
     * \param mode The parsed value.
     * \return A view into a string literal.
     */
    [[nodiscard]] constexpr std::string_view name_of(run_mode mode) noexcept {
        switch (mode) {
            case run_mode::fast:
                return "fast";
            case run_mode::slow:
                return "slow";
        }
        return "?";
    }

    /** \brief A TCP port, which is a `std::uint16_t` that may not be zero. */
    struct port_number {
        std::uint16_t value = 0;
    };

    /** \brief The root command. */
    struct[[= clapp::cmd{
                .name = "05_validation",
                .version = "1.0.0",
                .about = "Value sets, range checks and argument relations"
            }]] cli {
        /** Required, and restricted to the enumerators of #run_mode. */
        [[= clapp::arg{
            .index = 1,
            .help = "What mode to run the program in",
            .value_name = "MODE"
        }]] run_mode mode;

        /** Optional, and validated by the specialization above. */
        [[= clapp::arg{
            .long_ = "port",
            .help = "Network port to use",
            .value_name = "PORT"
        }]] std::optional<port_number>
        port;

        /** Either this or `--major`, never both and never neither. */
        [[= clapp::arg{
            .long_ = "set-ver",
            .help = "Set the version manually",
            .value_name = "VER"
        }]][[= clapp::conflicts_with{"major"}]][
            [= clapp::required_unless_any{"major"}]] std::optional<std::string>
        set_ver;

        [[= clapp::arg{
            .long_ = "major",
            .help = "Auto-increment the major version instead"
        }]] bool major;
    };
}


/**
 * \brief Parse a #port_number, rejecting anything outside 1-65535.
 *
 * \note The whole of clap's `04_02_validate.rs` fits in one static function, because a
 *       clapp::value_parser specialization is where the type's own rules belong. Two
 *       details are worth copying into your own:
 *
 *       * Delegate the numeric half to `clapp::value_parser<unsigned long>` rather than
 *         reimplementing it, so `--port eight` reports the same sentence a bare integer
 *         argument would.
 *       * Fill in `domain`. It is what turns "out of range" into a message that says
 *         *which* range, and it costs one string literal.
 */
template<>
struct clapp::value_parser<port_number> {
    /**
     * \brief Convert \p value into a #port_number.
     * \param value The raw bytes from the command line.
     * \return The port, or the error explaining why those bytes are not one.
     */
    [[nodiscard]] static constexpr std::expected<port_number, clapp::parse_error>
    parse(clapp::os_str value) {
        const std::expected<unsigned long, clapp::parse_error> number =
                clapp::value_parser<unsigned long>::parse(value);
        if (!number.has_value()) return std::unexpected(number.error());

        if (*number < 1 || *number > 65535)
            return std::unexpected(clapp::parse_error{
                .kind = clapp::parse_error_kind::out_of_range,
                .input = value,
                .type_name = "port_number",
                .reason = "port not in range 1-65535",
                .domain = "1..=65535"
            });

        return port_number{.value = static_cast<std::uint16_t>(*number)};
    }
};


int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);

    std::println("mode: {}", name_of(args.mode));
    if (args.port.has_value())
        std::println("port: Some({})", args.port->value);
    else
        std::println("port: None");

    // The relations above are what make this total: exactly one of the two was given.
    const std::string version = args.set_ver.has_value() ? *args.set_ver : std::string{"2.0.0"};
    std::println("version: {}", version);
}
