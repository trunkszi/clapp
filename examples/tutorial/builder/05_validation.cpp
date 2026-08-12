#include <clapp/clapp.hpp>

#include <cstdio>
#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {
    /** \brief How fast to run. A plain enum: the accepted spellings come from reflection. */
    enum class run_mode : unsigned char {
        fast[[= clapp::value{.help = "Run swiftly"}]],
        slow[[= clapp::value{.help = "Crawl slowly but steadily"}]],
    };

    /** \brief A TCP port, which is a `std::uint16_t` that may not be zero. */
    struct port_number {
        std::uint16_t value = 0;
    };
} // namespace

/**
 * \brief Parse a #port_number, rejecting anything outside 1-65535.
 * \note Identical to the derive half's specialization; see the file header.
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

namespace {
    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::raw_args;

    /**
     * \brief Build the command tree.
     * \return The frozen tree.
     */
    [[nodiscard]] consteval command_spec build() {
        command_builder app("05_validation");
        std::move(app)
                .version("1.0.0")
                .about("Value sets, range checks and argument relations")
                .arg(arg_builder("mode")
                    .index(1)
                    .value_name("MODE")
                    .help("What mode to run the program in")
                    .required()
                    .value_parser<run_mode>())
                .arg(arg_builder("port")
                    .long_("port")
                    .value_name("PORT")
                    .help("Network port to use")
                    .value_parser<port_number>())
                .arg(arg_builder("set_ver")
                    .long_("set-ver")
                    .value_name("VER")
                    .help("Set the version manually")
                    .conflicts_with("major")
                    .required_unless_present("major")
                    .value_parser<std::string>())
                .arg(arg_builder("major")
                    .long_("major")
                    .help("Auto-increment the major version instead")
                    .action(arg_action::set_true));
        return app.freeze();
    }

    /** The frozen tree. Built once, at compile time. */
    constexpr command_spec spec = build();

    /**
     * \brief Print \p err the way clapp::parse<T>() would, and yield its exit status.
     * \param err What clapp::parse() rejected the command line with.
     * \return The status `main` should return.
     */
    [[nodiscard]] int report(const error &err) {
        const std::string text = err.render().to_string();
        std::FILE *const out = err.use_stderr() ? stderr : stdout;
        static_cast<void>(std::fwrite(text.data(), 1, text.size(), out));
        if (!text.empty() && text.back() != '\n') static_cast<void>(std::fputc('\n', out));
        return err.exit_code();
    }
} // namespace

/**
 * \brief Spell a #run_mode the way the command line does.
 * \param mode The parsed value.
 * \return A view into a string literal.
 */
[[nodiscard]] static constexpr std::string_view name_of(run_mode mode) noexcept {
    switch (mode) {
        case run_mode::fast:
            return "fast";
        case run_mode::slow:
            return "slow";
    }
    return "?";
}


int main(int argc, char **argv) {
    const std::expected<arg_matches, error> got = clapp::parse(spec, raw_args(argc, argv));
    if (!got.has_value()) return report(got.error());

    std::println("mode: {}", name_of(**got->get_one<run_mode>("mode")));

    if (const std::optional<const port_number *> port = got->get_one<port_number>("port");
        port.has_value())
        std::println("port: Some({})", (*port)->value);
    else
        std::println("port: None");

    // The relations above are what make this total: exactly one of the two was given.
    const std::optional<const std::string *> set_ver = got->get_one<std::string>("set_ver");
    std::println("version: {}", set_ver.has_value() ? **set_ver : std::string{"2.0.0"});
}
