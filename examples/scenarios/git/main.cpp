#include <clapp/clapp.hpp>

#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
    /**
 * \brief When to colourize output. No `ValueEnum` opt-in: the accepted spellings come
 *        from `std::meta::enumerators_of`, and `auto_` loses its keyword-avoidance
 *        suffix on the way to `--color=auto`.
 */
    enum class color_when : unsigned char { always, auto_, never };


    /**
     * \brief Spell a color_when the way the command line does.
     * \param when The parsed value.
     * \return A view into a string literal.
     */
    [[nodiscard]] constexpr std::string_view name_of(color_when when) noexcept {
        switch (when) {
            case color_when::always:
                return "always";
            case color_when::auto_:
                return "auto";
            case color_when::never:
                return "never";
        }
        return "?";
    }


    /** \brief `git clone <REMOTE>`. */
    struct[[= clapp::cmd{.name = "clone", .about = "Clones repos", .arg_required_else_help = true}]]
            cmd_clone {
        [[= clapp::arg{.index = 1, .help = "The remote to clone"}]] std::string remote;
    };

    /** \brief `git diff [COMMIT] [COMMIT] [-- <COMMIT>] [--color[=WHEN]]`. */
    struct[[= clapp::cmd{.name = "diff", .about = "Compare two commits"}]] cmd_diff {
        [[= clapp::arg{.index = 1, .value_name = "COMMIT"}]] std::optional<std::string> base;
        [[= clapp::arg{.index = 2, .value_name = "COMMIT"}]] std::optional<std::string> head;

        /** Reachable only after `--`, which is what `.last` means. */
        [[= clapp::arg{.index = 3, .value_name = "COMMIT", .last = true}]] std::optional<std::string>
        path;

        /**
         * `std::optional<std::optional<T>>` is the three-state row of the deduction table:
         * absent, present without a value, present with one — i.e. clap's `num_args = 0..=1`.
         * It is what makes `.default_missing_value` reachable, since clapp::arg_attr has no
         * `num_args` field of its own. `--color` alone yields `always`; no `--color` at all
         * yields `auto`.
         */
        [[= clapp::arg{
            .long_ = "color",
            .help = "When to colorize output",
            .value_name = "WHEN",
            .default_value = "auto",
            .default_missing_value = "always",
            .require_equals = true
        }]] std::optional<std::optional<color_when> >
        color;
    };

    /** \brief `git push <REMOTE>`. */
    struct[[= clapp::cmd{.name = "push", .about = "pushes things", .arg_required_else_help = true}]]
            cmd_push {
        [[= clapp::arg{.index = 1, .help = "The remote to target"}]] std::string remote;
    };

    /** \brief `git add <PATH>...`. */
    struct[[= clapp::cmd{.name = "add", .about = "adds things", .arg_required_else_help = true}]]
            cmd_add {
        [[= clapp::arg{
            .index = 1,
            .required = clapp::tri::yes,
            .help = "Stuff to add"
        }]] std::vector<std::filesystem::path>
        path;
    };

    /**
     * \brief The arguments `git stash` and `git stash push` share.
     *
     * Used twice: flattened into #cmd_stash, and as the `push` alternative of its subcommand
     * set. That double duty is the whole reason clap's `StashArgs` exists.
     */
    struct[[= clapp::cmd{.name = "push", .about = "Push a new stash"}]] stash_push_args {
        [[= clapp::arg{.short_ = 'm', .long_ = "message"}]] std::optional<std::string> message;
    };

    /** \brief `git stash pop [STASH]`. */
    struct[[= clapp::cmd{.name = "pop", .about = "Remove a stash and apply it"}]] stash_pop_args {
        [[= clapp::arg{.index = 1}]] std::optional<std::string> stash;
    };

    /** \brief `git stash apply [STASH]`. */
    struct[[= clapp::cmd{.name = "apply", .about = "Apply a stash without removing it"}]]
            stash_apply_args {
        [[= clapp::arg{.index = 1}]] std::optional<std::string> stash;
    };

    /**
     * \brief `git stash [push|pop|apply]`.
     *
     * The nested level. `std::optional<std::variant<...>>` rather than a bare variant,
     * because `git stash` with no sub-subcommand is legal and means `git stash push`.
     */
    struct[[= clapp::cmd{.name = "stash", .about = "Stash the changes in a dirty working directory"}]]
            cmd_stash {
        [[= clapp::subcommand{}]] std::optional<
            std::variant<stash_push_args, stash_pop_args, stash_apply_args> >
        command;

        [[= clapp::flatten{}]] stash_push_args push;
    };


    /** \brief A fictional versioning CLI. */
    struct[[= clapp::cmd{.name = "git", .about = "A fictional versioning CLI"}]] cli {
        /** A **bare** variant, so a subcommand is mandatory and a bare `git` prints help. */
        [[= clapp::subcommand{}]] std::variant<cmd_clone, cmd_diff, cmd_push, cmd_add, cmd_stash>
        command;
    };

    /**
     * \brief Print what `git stash` decided to do.
     * \param stash The parsed `stash` subcommand, including its optional nested one.
     */
    void report_stash(const cmd_stash &stash) {
        // clap's rule: `git stash` with no nested subcommand means `git stash push`, using the
        // flattened arguments.
        const stash_push_args *push = &stash.push;
        if (stash.command.has_value()) {
            if (const auto *popped = std::get_if<stash_pop_args>(&*stash.command)) {
                std::println("Popping {}", popped->stash.value_or("<top>"));
                return;
            }
            if (const auto *applied = std::get_if<stash_apply_args>(&*stash.command)) {
                std::println("Applying {}", applied->stash.value_or("<top>"));
                return;
            }
            push = std::get_if<stash_push_args>(&*stash.command);
        }
        std::println("Pushing StashPushArgs {{ message: {} }}",
                     push->message.has_value()
                         ? "Some(\"" + *push->message + "\")"
                         : std::string{"None"});
    }

    /**
     * \brief Print what `git diff` decided to do.
     * \param diff The parsed `diff` subcommand.
     *
     * \note The base/head/path shuffle is clap's, verbatim: a single positional is a path,
     *       two are base and path, three are base, head and path.
     */
    void report_diff(const cmd_diff &diff) {
        std::optional<std::string> base = diff.base;
        std::optional<std::string> head = diff.head;
        std::optional<std::string> path = diff.path;
        if (!path.has_value()) {
            path = head;
            head.reset();
            if (!path.has_value()) {
                path = base;
                base.reset();
            }
        }
        // `color` is three-state; `.default_value` fills the outer optional, so the inner one
        // is empty only for `--color` written with no value and no default_missing_value.
        const color_when when =
                diff.color.value_or(std::optional<color_when>{}).value_or(color_when::auto_);
        std::println("Diffing {}..{} {} (color={})",
                     base.value_or("stage"),
                     head.value_or("worktree"),
                     path.value_or(""),
                     name_of(when));
    }
} // namespace

int main(int argc, char **argv) {
    const cli args = clapp::parse<cli>(argc, argv);

    if (const auto *cloned = std::get_if<cmd_clone>(&args.command)) {
        std::println("Cloning {}", cloned->remote);
    } else if (const auto *diff = std::get_if<cmd_diff>(&args.command)) {
        report_diff(*diff);
    } else if (const auto *pushed = std::get_if<cmd_push>(&args.command)) {
        std::println("Pushing to {}", pushed->remote);
    } else if (const auto *added = std::get_if<cmd_add>(&args.command)) {
        std::string joined;
        for (const std::filesystem::path &p: added->path) {
            if (!joined.empty()) joined += ", ";
            joined += '"';
            joined += p.string();
            joined += '"';
        }
        std::println("Adding [{}]", joined);
    } else if (const auto *stash = std::get_if<cmd_stash>(&args.command)) {
        report_stash(*stash);
    }
}
