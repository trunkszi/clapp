#include <clapp/clapp.hpp>

#include <filesystem>
#include <optional>
#include <print>
#include <variant>

namespace {
    /** \brief `true` — does nothing successfully. */
    struct[[= clapp::cmd{.name = "true", .about = "does nothing successfully"}]] applet_true {
    };

    /** \brief `false` — does nothing unsuccessfully. */
    struct[[= clapp::cmd{.name = "false", .about = "does nothing unsuccessfully"}]] applet_false {
    };

    /** \brief `busybox` — the dispatcher, reachable by name as well as by link. */
    struct[[= clapp::cmd{.name = "busybox", .arg_required_else_help = true}]] cmd_busybox {
        /**
     * `--install` with no value means `/usr/local/bin`; see the file header for why the
     * type is a nested optional.
     */
        [[= clapp::arg{
            .long_ = "install",
            .help = "Install hardlinks for all subcommands in path",
            .value_name = "PATH",
            .default_missing_value = "/usr/local/bin",
            .exclusive = true
        }]] std::optional<std::optional<std::filesystem::path> >
        install;

        [[= clapp::subcommand{}]] std::optional<std::variant<applet_true, applet_false> > command;
    };

    /** \brief The multicall root; the applets are also reachable directly. */
    struct[[= clapp::cmd{.multicall = true}]] cli {
        [[= clapp::subcommand{}]] std::variant<cmd_busybox, applet_true, applet_false> command;
    };

    /**
     * \brief Exit status of an applet.
     * \param success Whether the applet is the `true` one.
     * \return 0 or 1, the applets' entire behaviour.
     */
    [[nodiscard]] int status_of(bool success) noexcept { return success ? 0 : 1; }
} // namespace

int main(int argc, char **argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);

    if (const auto *box_ = std::get_if<cmd_busybox>(&parsed.command)) {
        if (box_->install.has_value()) {
            const std::filesystem::path where =
                    box_->install->value_or(std::filesystem::path{"/usr/local/bin"});
            std::println("Would install hardlinks in {}", where.string());
            return 0;
        }
        // `arg_required_else_help` guarantees one of the two arrived.
        return status_of(box_->command.has_value() &&
                         std::holds_alternative<applet_true>(*box_->command));
    }

    return status_of(std::holds_alternative<applet_true>(parsed.command));
}
