#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/output/help.hpp>

#include "support/check.hpp"

#include <algorithm>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using clapp::arg_builder;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::help_style;

    /**
     * \brief The page `-h` or `--help` prints, with clap's `use_long` collapse applied.
     *        Same helper, same rationale, as conformance_help_test.cpp.
     */
    std::string page(const command_spec& cmd, bool long_form) {
        return clapp::render_help(cmd,
                                  help_style{.use_long = long_form && clapp::long_help_exists(cmd)})
                .to_string();
    }

    bool same(const std::string& got, std::string_view want) {
        if (got == want) return true;
        std::println("--- got ---\n{}--- want ---\n{}--- end ---", got, want);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    //
    // Each mirrors one clap fixture exactly, including which knobs are absent: several of
    // the cases below differ from `flatten_basic` by a single call, and that single call is
    // the whole test.
    // ---------------------------------------------------------------------------

    /**
     * clap `flatten_short_help` / `flatten_long_help` — one tree, two screens. Both the
     * parent's arg and the child's arg carry `help` *and* `long_help`, and the subcommand
     * carries `about` *and* `long_about`, so every line that can differ between the two
     * screens does.
     */
    consteval command_spec make_short_long() {
        command_builder sub("test");
        std::move(sub)
                .about("test command")
                .long_about("long some")
                .arg(arg_builder("child").long_("child").help("foo").long_help("bar"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent").help("foo").long_help("bar"))
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec short_long = make_short_long();

    /** clap `flatten_with_global` — `flatten_basic` plus `global(true)` on the parent's arg. */
    consteval command_spec make_with_global() {
        command_builder sub("test");
        std::move(sub).about("test command").arg(arg_builder("child").long_("child"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent").global())
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec with_global = make_with_global();

    /**
     * clap `flatten_arg_required` — the only case whose section headings are not
     * `<parent> <sub>`.
     */
    consteval command_spec make_arg_required() {
        command_builder sub("test");
        std::move(sub).about("test command").arg(arg_builder("child").long_("child").required());
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent").required())
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec arg_required = make_arg_required();

    /**
     * clap `flatten_with_external_subcommand` — `flatten_basic` plus
     * `allow_external_subcommands(true)`, which adds nothing to the page.
     */
    consteval command_spec make_with_external() {
        command_builder sub("test");
        std::move(sub).about("test command").arg(arg_builder("child").long_("child"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .allow_external_subcommands()
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec with_external = make_with_external();

    /**
     * clap `flatten_without_subcommands` — the degenerate case: `flatten_help` on a command
     * with nothing to flatten must not change the ordinary page.
     */
    consteval command_spec make_without_subcommands() {
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent"));
        return cmd.freeze();
    }
    constexpr command_spec without_subcommands = make_without_subcommands();

    /**
     * clap `flatten_with_subcommand_required` — deletes the bare `parent [OPTIONS]` usage
     * line while keeping the parent's `Options:` table intact.
     */
    consteval command_spec make_sub_required() {
        command_builder sub("test");
        std::move(sub).about("test command").arg(arg_builder("child").long_("child"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .subcommand_required()
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec sub_required = make_sub_required();

    /**
     * clap `flatten_with_args_conflicts_with_subcommands` — the same tree as `sub_required`
     * plus `args_conflicts_with_subcommands(true)`, which puts the deleted usage line back.
     */
    consteval command_spec make_conflicts() {
        command_builder sub("test");
        std::move(sub).about("test command").arg(arg_builder("child").long_("child"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .subcommand_required()
                .args_conflicts_with_subcommands()
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec conflicts = make_conflicts();

    /**
     * clap `flatten_single_hidden_command` — the only subcommand is hidden, so the page
     * must collapse to `flatten_without_subcommands`'s shape exactly.
     */
    consteval command_spec make_single_hidden() {
        command_builder sub("child1");
        std::move(sub).hide().about("child1 command").arg(arg_builder("child").long_("child1"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(sub));
        return cmd.freeze();
    }
    constexpr command_spec single_hidden = make_single_hidden();

    /** clap `flatten_hidden_command` — one of three hidden; the other two, and `help`, stay. */
    consteval command_spec make_hidden_command() {
        command_builder c1("child1");
        std::move(c1).about("child1 command").arg(arg_builder("child").long_("child1"));
        command_builder c2("child2");
        std::move(c2).about("child2 command").arg(arg_builder("child").long_("child2"));
        command_builder c3("child3");
        std::move(c3).hide().about("child3 command").arg(arg_builder("child").long_("child3"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(c1))
                .subcommand(std::move(c2))
                .subcommand(std::move(c3));
        return cmd.freeze();
    }
    constexpr command_spec hidden_command = make_hidden_command();

    /**
     * clap `flatten_not_recursive` — `child1` has three grandchildren and none of them may
     * appear. The single trace they leave is `[COMMAND]` on `child1`'s usage line.
     */
    consteval command_spec make_not_recursive() {
        command_builder g1("grandchild1");
        std::move(g1)
                .about("grandchild1 command")
                .arg(arg_builder("grandchild").long_("grandchild1"));
        command_builder g2("grandchild2");
        std::move(g2)
                .about("grandchild2 command")
                .arg(arg_builder("grandchild").long_("grandchild2"));
        command_builder g3("grandchild3");
        std::move(g3)
                .about("grandchild3 command")
                .arg(arg_builder("grandchild").long_("grandchild3"));
        command_builder c1("child1");
        std::move(c1)
                .about("child1 command")
                .arg(arg_builder("child").long_("child1"))
                .subcommand(std::move(g1))
                .subcommand(std::move(g2))
                .subcommand(std::move(g3));
        command_builder c2("child2");
        std::move(c2).about("child2 command").arg(arg_builder("child").long_("child2"));
        command_builder c3("child3");
        std::move(c3).about("child3 command").arg(arg_builder("child").long_("child3"));
        command_builder cmd("parent");
        std::move(cmd)
                .flatten_help()
                .about("parent command")
                .arg(arg_builder("parent").long_("parent"))
                .subcommand(std::move(c1))
                .subcommand(std::move(c2))
                .subcommand(std::move(c3));
        return cmd.freeze();
    }
    constexpr command_spec not_recursive = make_not_recursive();

    // ---------------------------------------------------------------------------
    // Spec-shape invariants
    //
    // These are what make the two screens of `short_long` two screens, and what makes every
    // other fixture a single screen. Without them a `--help` case that silently collapsed to
    // the `-h` page would still "pass".
    // ---------------------------------------------------------------------------

    static_assert(short_long.is_flatten_help_set());
    static_assert(clapp::long_help_exists(short_long));
    static_assert(!clapp::long_help_exists(with_global));
    static_assert(!clapp::long_help_exists(arg_required));
    static_assert(!clapp::long_help_exists(not_recursive));

    // The two hiding shapes, asserted on the spec before they are asserted on the page — the
    // page-level difference between them is 20 lines, so a wrong answer here is easy to
    // misread as a layout bug.
    static_assert(!single_hidden.has_visible_subcommands());
    static_assert(hidden_command.has_visible_subcommands());

    // `subcommand_required` and `args_conflicts_with_subcommands` are separate bits; the
    // second does not imply nor clear the first.
    static_assert(sub_required.is_subcommand_required_set());
    static_assert(!sub_required.is_args_conflicts_with_subcommands_set());
    static_assert(conflicts.is_subcommand_required_set());
    static_assert(conflicts.is_args_conflicts_with_subcommands_set());

    // FIXTURE INTEGRITY, FOR THE TWO CASES WHOSE EXPECTED PAGE IS `flatten_basic`'S.
    //
    // `flatten_with_global` and `flatten_with_external_subcommand` each differ from
    // `flatten_basic` by exactly one builder call whose whole point is that it changes
    // nothing on the page. That makes them the two cases in the family that a *no-op*
    // `global()` / `allow_external_subcommands()` would also satisfy — the page would be
    // right for the wrong reason and the case would be vacuous. clap's originals have the
    // same hole; these four lines close it on the clapp side by asserting the bit actually
    // reached the frozen spec, each paired with a control fixture that must NOT have it.
    //
    // Written as `ranges::any_of` over the argument list rather than
    // `find_arg("parent")->is_global_set()`: forming that pointer inside a `static_assert`
    // is trap 10 (CLAUDE.md) and would break the `ubsan` preset only.
    consteval bool arg_is_global(const command_spec& cmd, std::string_view id) {
        return std::ranges::any_of(cmd.get_arguments(), [id](const clapp::arg_spec& a) {
            return a.get_id().name() == id && a.is_global_set();
        });
    }
    consteval bool arg_is_required(const command_spec& cmd, std::string_view id) {
        return std::ranges::any_of(cmd.get_arguments(), [id](const clapp::arg_spec& a) {
            return a.get_id().name() == id && a.is_required_set();
        });
    }
    consteval bool sub_is_hidden(const command_spec& cmd, std::string_view name) {
        return std::ranges::any_of(cmd.get_subcommands(), [name](const command_spec& c) {
            return c.get_name() == name && c.is_hide_set();
        });
    }

    static_assert(arg_is_global(with_global, "parent"));
    static_assert(!arg_is_global(short_long, "parent"));
    static_assert(with_external.is_allow_external_subcommands_set());
    static_assert(!with_global.is_allow_external_subcommands_set());

    // Same argument for the remaining two knobs that the page alone could not distinguish
    // from a coincidence of layout: `required` travels into the usage name, and `hide`
    // removes a section.
    static_assert(arg_is_required(arg_required, "parent"));
    static_assert(!arg_is_required(with_global, "parent"));
    static_assert(sub_is_hidden(single_hidden, "child1"));
    static_assert(sub_is_hidden(hidden_command, "child3"));
    static_assert(!sub_is_hidden(hidden_command, "child1"));

    // ---------------------------------------------------------------------------
    // The pages
    // ---------------------------------------------------------------------------

    CLAPP_TEST("help.rs::flatten_short_help") {
        // The short screen: compact tables everywhere, and both help flags cross-reference
        // `--help` because both levels have long content.
        CLAPP_CHECK(same(page(short_long, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent test [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  foo\n"
                         "  -h, --help             Print help (see more with '--help')\n"
                         "\n"
                         "parent test:\n"
                         "test command\n"
                         "      --child <child>  foo\n"
                         "  -h, --help           Print help (see more with '--help')\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_long_help") {
        // Same tree, `--help`. Every description moves to its own line — inside the flattened
        // sections too — and `long_help` replaces `help`. The section heading is still the
        // subcommand's `about` ("test command"), NOT its `long_about` ("long some"): clap
        // defers the long text to `parent test --help`.
        CLAPP_CHECK(same(page(short_long, true),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent test [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>\n"
                         "          bar\n"
                         "\n"
                         "  -h, --help\n"
                         "          Print help (see a summary with '-h')\n"
                         "\n"
                         "parent test:\n"
                         "test command\n"
                         "      --child <child>\n"
                         "          bar\n"
                         "\n"
                         "  -h, --help\n"
                         "          Print help (see a summary with '-h')\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...\n"
                         "          Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_with_global") {
        // `global(true)` on the parent's argument changes nothing: it is listed once, under
        // the parent, and is not repeated into the child's section.
        CLAPP_CHECK(same(page(with_global, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent test [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent test:\n"
                         "test command\n"
                         "      --child <child>  \n"
                         "  -h, --help           Print help\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_arg_required") {
        // The section heading is the subcommand's *usage name*, so the parent's required
        // argument appears in it — twice, once per section. `parent test:` would be wrong.
        CLAPP_CHECK(same(page(arg_required, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent --parent <parent>\n"
                         "       parent --parent <parent> test --child <child>\n"
                         "       parent --parent <parent> help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent --parent <parent> test:\n"
                         "test command\n"
                         "      --child <child>  \n"
                         "  -h, --help           Print help\n"
                         "\n"
                         "parent --parent <parent> help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_with_external_subcommand") {
        // An external subcommand has no spec, so it contributes no section and no usage line.
        // This page is byte-identical to `flatten_basic`'s.
        CLAPP_CHECK(same(page(with_external, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent test [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent test:\n"
                         "test command\n"
                         "      --child <child>  \n"
                         "  -h, --help           Print help\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_without_subcommands") {
        // Nothing to flatten: one usage line, one table, no sections, and no trailing blank
        // section separator.
        CLAPP_CHECK(same(page(without_subcommands, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"));
    }

    CLAPP_TEST("help.rs::flatten_with_subcommand_required") {
        // `subcommand_required` removes the bare `parent [OPTIONS]` line and NOTHING else —
        // `Options:` still lists `--parent` in full.
        CLAPP_CHECK(same(page(sub_required, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent test [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent test:\n"
                         "test command\n"
                         "      --child <child>  \n"
                         "  -h, --help           Print help\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_with_args_conflicts_with_subcommands") {
        // Same spec as the case above plus `args_conflicts_with_subcommands`, which puts the
        // `parent [OPTIONS]` line back: the arguments alone are now a valid invocation. This
        // is the pair that shows the first usage line is governed by BOTH settings.
        CLAPP_CHECK(same(page(conflicts, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent test [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent test:\n"
                         "test command\n"
                         "      --child <child>  \n"
                         "  -h, --help           Print help\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_single_hidden_command") {
        // The only subcommand is hidden, so `help` is not offered either and the page is
        // byte-identical to `flatten_without_subcommands`'s.
        CLAPP_CHECK(same(page(single_hidden, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"));
        // ...and that identity is the assertion, not a coincidence of the two literals above.
        CLAPP_CHECK(page(single_hidden, false) == page(without_subcommands, false));
    }

    CLAPP_TEST("help.rs::flatten_hidden_command") {
        // One of three hidden: `child3` has no usage line and no section, and the other two
        // are untouched. Note the column width inside each section is computed per section —
        // `--child1 <child>` is one wider than `--help`, so the two rows differ from the
        // parent's table above them.
        CLAPP_CHECK(same(page(hidden_command, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent child1 [OPTIONS]\n"
                         "       parent child2 [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent child1:\n"
                         "child1 command\n"
                         "      --child1 <child>  \n"
                         "  -h, --help            Print help\n"
                         "\n"
                         "parent child2:\n"
                         "child2 command\n"
                         "      --child2 <child>  \n"
                         "  -h, --help            Print help\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

    CLAPP_TEST("help.rs::flatten_not_recursive") {
        // `child1`'s three grandchildren appear nowhere. Their only trace is `[COMMAND]` on
        // `child1`'s usage line — which is also the tell that the recursion guard is a check
        // on the *child's* `flatten_help` bit and not a depth limit: `child1` does not set it.
        CLAPP_CHECK(same(page(not_recursive, false),
                         "parent command\n"
                         "\n"
                         "Usage: parent [OPTIONS]\n"
                         "       parent child1 [OPTIONS] [COMMAND]\n"
                         "       parent child2 [OPTIONS]\n"
                         "       parent child3 [OPTIONS]\n"
                         "       parent help [COMMAND]...\n"
                         "\n"
                         "Options:\n"
                         "      --parent <parent>  \n"
                         "  -h, --help             Print help\n"
                         "\n"
                         "parent child1:\n"
                         "child1 command\n"
                         "      --child1 <child>  \n"
                         "  -h, --help            Print help\n"
                         "\n"
                         "parent child2:\n"
                         "child2 command\n"
                         "      --child2 <child>  \n"
                         "  -h, --help            Print help\n"
                         "\n"
                         "parent child3:\n"
                         "child3 command\n"
                         "      --child3 <child>  \n"
                         "  -h, --help            Print help\n"
                         "\n"
                         "parent help:\n"
                         "Print this message or the help of the given subcommand(s)\n"
                         "  [COMMAND]...  Print help for the subcommand(s)\n"));
    }

}  // namespace
