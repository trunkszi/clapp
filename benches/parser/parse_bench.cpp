#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/lex/os_str.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/meta/parse.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include <benchmark/benchmark.h>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::group_builder;
    using clapp::raw_args;
    using clapp::value_range;

    // ===========================================================================
    // ripgrep — 58 arguments, no subcommands
    // ===========================================================================

    /**
     * \brief `--color`'s domain, clap's `value_parser(["never", "auto", "always", "ansi"])`.
     *
     * \note An enum rather than a list of strings because that is how a clapp user spells
     *       it: clapp::value_parser reads the enumerators by reflection, so the possible
     *       values and the parse come from one declaration. `auto` is a keyword, hence the
     *       rename — which is also the cheapest available check that enumerator annotations
     *       survive into the frozen tree.
     */
    enum class color_when : unsigned char {
        never [[maybe_unused]],
        automatic [[maybe_unused]] [[= clapp::value{.name = "auto"}]],
        always [[maybe_unused]],
        ansi [[maybe_unused]],
    };

    /**
     * \brief A `--flag` with no value, clap's `flag(name)` closure.
     * \param name The long spelling, which is also the id.
     * \return The builder, for chaining.
     */
    consteval arg_builder flag(std::string_view name) {
        return arg_builder(name).long_(name).action(arg_action::set_true);
    }

    /**
     * \brief Build clap's `ripgrep.rs::cmd()`, argument for argument.
     * \return The frozen tree.
     *
     * \note Help strings are omitted. clap's fixture pulls them from a `lazy_static`
     *       HashMap of 40-odd paragraphs; they change the size of the `.rodata` image and
     *       the cost of rendering help, neither of which this file measures, and carrying
     *       them would put 400 lines of prose between the reader and the argument list.
     *       benches/output/ is where they would belong.
     */
    consteval command_spec make_ripgrep() {
        command_builder app("ripgrep");
        std::move(app)
                .author("BurntSushi")
                .version("0.4.0")
                .about("ripgrep recursively searches your current directory for a regex pattern")
                .max_term_width(100)
                .disable_help_flag()
                .disable_version_flag()
                // Handled by hand in ripgrep, so that -h and --help can differ.
                .arg(arg_builder("help-short").short_('h'))
                .arg(flag("help"))
                .arg(flag("version").short_('V'))
                // Primary positionals.
                .arg(arg_builder("pattern").required_unless_present_any(
                        {"file", "files", "help-short", "help", "regexp", "type-list", "version"}))
                .arg(arg_builder("path").num_args(value_range::at_least(1)))
                .arg(flag("regexp")
                             .short_('e')
                             .allow_hyphen_values()
                             .action(arg_action::append)
                             .value_name("pattern"))
                .arg(flag("files").conflicts_with_all({"file", "regexp", "type-list"}))
                .arg(flag("type-list").conflicts_with_all({"file", "files", "pattern", "regexp"}))
                // Common flags.
                .arg(flag("text").short_('a'))
                .arg(flag("count").short_('c'))
                .arg(flag("color")
                             .value_name("WHEN")
                             .action(arg_action::set)
                             .hide_possible_values()
                             .value_parser<color_when>())
                .arg(flag("colors").value_name("SPEC").action(arg_action::append))
                .arg(flag("fixed-strings").short_('F'))
                .arg(flag("glob").short_('g').action(arg_action::append).value_name("GLOB"))
                .arg(flag("ignore-case").short_('i'))
                .arg(flag("line-number").short_('n'))
                .arg(flag("no-line-number").short_('N'))
                .arg(flag("quiet").short_('q'))
                .arg(flag("type").short_('t').action(arg_action::append).value_name("TYPE"))
                .arg(flag("type-not").short_('T').action(arg_action::append).value_name("TYPE"))
                .arg(flag("unrestricted").short_('u').action(arg_action::append))
                .arg(flag("invert-match").short_('v'))
                .arg(flag("word-regexp").short_('w'))
                // Less common flags.
                .arg(flag("after-context")
                             .short_('A')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("before-context")
                             .short_('B')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("context")
                             .short_('C')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("column"))
                .arg(flag("context-separator").value_name("SEPARATOR"))
                .arg(flag("debug"))
                .arg(flag("file").short_('f').value_name("FILE").action(arg_action::append))
                .arg(flag("files-with-matches").short_('l'))
                .arg(flag("files-without-match"))
                .arg(flag("with-filename").short_('H'))
                .arg(flag("no-filename"))
                .arg(flag("heading").overrides_with("no-heading"))
                .arg(flag("no-heading").overrides_with("heading"))
                .arg(flag("hidden"))
                .arg(flag("ignore-file").value_name("FILE").action(arg_action::append))
                .arg(flag("follow").short_('L'))
                .arg(flag("max-count")
                             .short_('m')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("maxdepth")
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("mmap"))
                .arg(flag("no-messages"))
                .arg(flag("no-mmap"))
                .arg(flag("no-ignore"))
                .arg(flag("no-ignore-parent"))
                .arg(flag("no-ignore-vcs"))
                .arg(flag("null"))
                .arg(flag("path-separator").value_name("SEPARATOR"))
                .arg(flag("pretty").short_('p'))
                .arg(flag("replace").short_('r').action(arg_action::set).value_name("ARG"))
                .arg(flag("case-sensitive").short_('s'))
                .arg(flag("smart-case").short_('S'))
                .arg(flag("sort-files"))
                .arg(flag("threads")
                             .short_('j')
                             .action(arg_action::set)
                             .value_name("ARG")
                             .value_parser<std::size_t>())
                .arg(flag("vimgrep"))
                .arg(flag("type-add").value_name("TYPE").action(arg_action::append))
                .arg(flag("type-clear").value_name("TYPE").action(arg_action::append));
        return app.freeze();
    }

    inline constexpr command_spec ripgrep_cli = make_ripgrep();

    // The fixture is asserted, not assumed: a transcription error that dropped half the
    // argument list would otherwise make every number below look excellent.
    static_assert(ripgrep_cli.get_arguments().size() == 58);
    static_assert(ripgrep_cli.get_subcommands().empty());
    static_assert(ripgrep_cli.has_arg("pattern"));
    static_assert(ripgrep_cli.contains_short('e'));

    // ===========================================================================
    // rustup — 43 commands, three levels deep
    // ===========================================================================

    /**
     * \brief `Arg::new(name).required(true)`, the only argument shape rustup's tree uses.
     * \param name The positional's id and placeholder.
     * \return The builder, for chaining.
     */
    consteval arg_builder required_positional(std::string_view name) {
        return arg_builder(name).required();
    }

    /**
     * \brief `--toolchain <toolchain>`, repeated on nine of rustup's leaves.
     * \return The builder, for chaining.
     */
    consteval arg_builder toolchain_option() {
        return arg_builder("toolchain").long_("toolchain").action(arg_action::set);
    }

    /**
     * \brief Build clap's `rustup.rs::build_cli()`, command for command.
     * \return The frozen tree.
     */
    consteval command_spec make_rustup() {
        command_builder app("rustup");
        std::move(app)
                .version("0.9.0")
                .about("The Rust toolchain installer")
                .arg(arg_builder("verbose")
                             .help("Enable verbose output")
                             .short_('v')
                             .long_("verbose")
                             .action(arg_action::set_true))
                .subcommand(
                        command_builder("show").about("Show the active and installed toolchains"))
                .subcommand(command_builder("install")
                                    .about("Update Rust toolchains")
                                    .hide()
                                    .arg(required_positional("toolchain")))
                .subcommand(command_builder("update")
                                    .about("Update Rust toolchains")
                                    .arg(required_positional("toolchain"))
                                    .arg(arg_builder("no-self-update")
                                                 .help("Don't perform self update when running "
                                                       "the `rustup` command")
                                                 .long_("no-self-update")
                                                 .action(arg_action::set_true)
                                                 .hide()))
                .subcommand(command_builder("default")
                                    .about("Set the default toolchain")
                                    .arg(required_positional("toolchain")))
                .subcommand(
                        command_builder("toolchain")
                                .about("Modify or query the installed toolchains")
                                .subcommand(
                                        command_builder("list").about("List installed toolchains"))
                                .subcommand(command_builder("install")
                                                    .about("Install or update a given toolchain")
                                                    .arg(required_positional("toolchain")))
                                .subcommand(command_builder("uninstall")
                                                    .about("Uninstall a toolchain")
                                                    .arg(required_positional("toolchain")))
                                .subcommand(
                                        command_builder("link")
                                                .about("Create a custom toolchain by symlinking "
                                                       "to a directory")
                                                .arg(required_positional("toolchain"))
                                                .arg(required_positional("path")))
                                .subcommand(command_builder("update").hide().arg(
                                        required_positional("toolchain")))
                                .subcommand(command_builder("add").hide().arg(
                                        required_positional("toolchain")))
                                .subcommand(command_builder("remove").hide().arg(
                                        required_positional("toolchain"))))
                .subcommand(
                        command_builder("target")
                                .about("Modify a toolchain's supported targets")
                                .subcommand(command_builder("list")
                                                    .about("List installed and available targets")
                                                    .arg(toolchain_option()))
                                .subcommand(command_builder("add")
                                                    .about("Add a target to a Rust toolchain")
                                                    .arg(required_positional("target"))
                                                    .arg(toolchain_option()))
                                .subcommand(command_builder("remove")
                                                    .about("Remove a target from a Rust toolchain")
                                                    .arg(required_positional("target"))
                                                    .arg(toolchain_option()))
                                .subcommand(command_builder("install")
                                                    .hide()
                                                    .arg(required_positional("target"))
                                                    .arg(toolchain_option()))
                                .subcommand(command_builder("uninstall")
                                                    .hide()
                                                    .arg(required_positional("target"))
                                                    .arg(toolchain_option())))
                .subcommand(
                        command_builder("component")
                                .about("Modify a toolchain's installed components")
                                .subcommand(
                                        command_builder("list")
                                                .about("List installed and available components")
                                                .arg(toolchain_option()))
                                .subcommand(
                                        command_builder("add")
                                                .about("Add a component to a Rust toolchain")
                                                .arg(required_positional("component"))
                                                .arg(toolchain_option())
                                                .arg(arg_builder("target").long_("target").action(
                                                        arg_action::set)))
                                .subcommand(
                                        command_builder("remove")
                                                .about("Remove a component from a Rust toolchain")
                                                .arg(required_positional("component"))
                                                .arg(toolchain_option())
                                                .arg(arg_builder("target").long_("target").action(
                                                        arg_action::set))))
                .subcommand(
                        command_builder("override")
                                .about("Modify directory toolchain overrides")
                                .subcommand(command_builder("list").about(
                                        "List directory toolchain overrides"))
                                .subcommand(
                                        command_builder("set")
                                                .about("Set the override toolchain for a directory")
                                                .arg(required_positional("toolchain")))
                                .subcommand(command_builder("unset")
                                                    .about("Remove the override toolchain for a "
                                                           "directory")
                                                    .arg(arg_builder("path")
                                                                 .long_("path")
                                                                 .action(arg_action::set)
                                                                 .help("Path to the directory"))
                                                    .arg(arg_builder("nonexistent")
                                                                 .long_("nonexistent")
                                                                 .action(arg_action::set_true)
                                                                 .help("Remove override toolchain "
                                                                       "for all "
                                                                       "nonexistent directories")))
                                .subcommand(command_builder("add").hide().arg(
                                        required_positional("toolchain")))
                                .subcommand(command_builder("remove")
                                                    .hide()
                                                    .about("Remove the override toolchain for a "
                                                           "directory")
                                                    .arg(arg_builder("path").long_("path").action(
                                                            arg_action::set))
                                                    .arg(arg_builder("nonexistent")
                                                                 .long_("nonexistent")
                                                                 .action(arg_action::set_true)
                                                                 .help("Remove override toolchain "
                                                                       "for all "
                                                                       "nonexistent directories"))))
                .subcommand(command_builder("run")
                                    .about("Run a command with an environment configured for a "
                                           "given toolchain")
                                    .arg(required_positional("toolchain"))
                                    .arg(arg_builder("command")
                                                 .required()
                                                 .num_args(value_range::at_least(1))
                                                 .trailing_var_arg()))
                .subcommand(command_builder("which")
                                    .about("Display which binary will be run for a given command")
                                    .arg(required_positional("command")))
                .subcommand(command_builder("doc")
                                    .about("Open the documentation for the current toolchain")
                                    .arg(arg_builder("book")
                                                 .long_("book")
                                                 .action(arg_action::set_true)
                                                 .help("The Rust Programming Language book"))
                                    .arg(arg_builder("std")
                                                 .long_("std")
                                                 .action(arg_action::set_true)
                                                 .help("Standard library API documentation"))
                                    .group(group_builder("page").args({"book", "std"})))
                .subcommand(command_builder("man")
                                    .about("View the man page for a given command")
                                    .arg(required_positional("command"))
                                    .arg(toolchain_option()))
                .subcommand(
                        command_builder("self")
                                .about("Modify the rustup installation")
                                .subcommand(command_builder("update").about(
                                        "Download and install updates to rustup"))
                                .subcommand(command_builder("uninstall")
                                                    .about("Uninstall rustup.")
                                                    .arg(arg_builder("no-prompt")
                                                                 .short_('y')
                                                                 .action(arg_action::set_true)))
                                .subcommand(command_builder("upgrade-data")
                                                    .about("Upgrade the internal data format.")))
                .subcommand(command_builder("telemetry")
                                    .about("rustup telemetry commands")
                                    .hide()
                                    .subcommand(command_builder("enable").about(
                                            "Enable rustup telemetry"))
                                    .subcommand(command_builder("disable").about(
                                            "Disable rustup telemetry"))
                                    .subcommand(command_builder("analyze").about(
                                            "Analyze stored telemetry")))
                .subcommand(
                        command_builder("set")
                                .about("Alter rustup settings")
                                .subcommand(command_builder("default-host")
                                                    .about("The triple used to identify toolchains "
                                                           "when not specified")
                                                    .arg(required_positional("host_triple"))));
        return app.freeze();
    }

    inline constexpr command_spec rustup_cli = make_rustup();

    // 15 top-level commands; `help` is injected, which is why the count is 16 rather than
    // clap's 15. The nested assertions pin the two levels the dispatch benchmark descends.
    static_assert(rustup_cli.get_subcommands().size() == 16);
    static_assert(rustup_cli.has_subcommand("override"));
    static_assert(rustup_cli.find_subcommand("override")->has_subcommand("set"));
    static_assert(rustup_cli.find_subcommand("toolchain")->get_subcommands().size() == 8);

    // ===========================================================================
    // A wall of flags — how the fixed cost scales with the SIZE OF THE CLI
    // ===========================================================================
    //
    // `ripgrep_simple` parses two tokens and takes microseconds, while `rustup_empty` parses
    // one token against a one-argument root and takes nanoseconds. The difference is not the
    // command line, it is the 58 arguments the parser sweeps whatever the user typed:
    // defaults, environment, then the validator's pass over every argument and group. These
    // fixtures isolate that term, because a single point cannot tell a large linear constant
    // from a quadratic.

    /**
     * \brief `opt-N`, built without forming a `std::string` from a pointer and a length.
     * \param index The flag's ordinal.
     * \return An owning string; `freeze()` promotes it into static storage.
     *
     * \note `push_back` rather than `std::to_string` or `operator+=`: libstdc++'s
     *       `basic_string(const CharT*, size_type)` tests its source pointer, and GCC under
     *       `-fsanitize=null` will not fold that comparison during constant evaluation
     *       (CLAUDE.md trap 10). The bench preset carries no sanitizer, but a fixture that
     *       only compiles on one preset is a trap for whoever moves it.
     */
    consteval std::string flag_name(std::size_t index) {
        std::string out;
        out.push_back('o');
        out.push_back('p');
        out.push_back('t');
        if (index == 0) {
            out.push_back('0');
            return out;
        }
        std::string digits;
        for (std::size_t rest = index; rest > 0; rest /= 10)
            digits.push_back(static_cast<char>('0' + (rest % 10)));
        for (std::size_t i = digits.size(); i > 0; --i) out.push_back(digits[i - 1]);
        return out;
    }

    /**
     * \brief A command with \p N distinct long flags and nothing else.
     * \tparam N How many.
     * \return The frozen tree.
     */
    template<std::size_t N>
    consteval command_spec make_flag_wall() {
        command_builder app("wall");
        for (std::size_t i = 0; i < N; ++i) {
            const std::string name = flag_name(i);
            std::move(app).arg(arg_builder(name).long_(name).action(arg_action::set_true));
        }
        return app.freeze();
    }

    inline constexpr command_spec wall_1  = make_flag_wall<1>();
    inline constexpr command_spec wall_8  = make_flag_wall<8>();
    inline constexpr command_spec wall_16 = make_flag_wall<16>();
    inline constexpr command_spec wall_32 = make_flag_wall<32>();
    inline constexpr command_spec wall_64 = make_flag_wall<64>();

    static_assert(wall_64.get_arguments().size() == 65);  // + the injected --help
    static_assert(wall_64.has_arg("opt63"));

    /**
     * \brief Pick the wall for a benchmark argument.
     * \param count One of the sizes built above.
     * \return The frozen tree of that size.
     */
    const command_spec& wall_of(benchmark::IterationCount count) {
        switch (count) {
        case 1:
            return wall_1;
        case 8:
            return wall_8;
        case 16:
            return wall_16;
        case 32:
            return wall_32;
        default:
            return wall_64;
        }
    }

    // ===========================================================================
    // The derive layer, for the one step the builder fixtures never take
    // ===========================================================================

    /**
     * \brief A small annotated CLI, so that `parse<T>` can be timed against `parse(spec, …)`.
     *
     * \note Deliberately small. The interesting quantity is the *difference* between the
     *       two, which is clapp::from_matches — one pass over the fields pulling values out
     *       of clapp::arg_matches. A large struct would bury that difference under the same
     *       parse cost both spellings pay.
     */
    struct[[= clapp::cmd{.name = "demo", .version = "1.0.0"}]] demo_cli {
        [[maybe_unused]] [[= clapp::arg{.short_ = 'n', .long_ = "name"}]]
        std::string name;
        [[maybe_unused]] [[= clapp::arg{
            .short_ = 'c',
            .long_ = "count",
            .default_value = "1"
        }]] unsigned count;
        [[maybe_unused]] [[= clapp::arg{.short_ = 'v', .long_ = "verbose"}]]
        bool verbose;
        [[maybe_unused]] std::vector<std::string> files;
    };

    // ===========================================================================
    // Command lines
    // ===========================================================================

    /**
     * \brief clap's `ripgrep.rs::startup::xargs` fixture.
     * \param count How many repetitions of the trailing positional.
     * \return `["rg", "pat", "some", "some", …]`.
     */
    std::vector<std::string> xargs_line(std::size_t count) {
        std::vector<std::string> out;
        out.reserve(count + 2);
        out.emplace_back("rg");
        out.emplace_back("pat");
        for (std::size_t i = 0; i < count; ++i) out.emplace_back("some");
        return out;
    }

    // ===========================================================================
    // Benchmarks
    // ===========================================================================

    /**
     * \brief What clap's `build()` bench measures, on the clapp side.
     *
     * clap re-runs the whole builder chain here — 58 `Arg::new`, one `Vec<Arg>` allocation
     * and 58 string clones. clapp's tree was frozen by the compiler, so what remains is
     * reading a `.rodata` object. Report it, do not hide it.
     */
    void build_frozen_tree_is_free(benchmark::State& state) {
        for (auto _ : state) {
            const command_spec& spec = ripgrep_cli;
            benchmark::DoNotOptimize(spec.get_arguments().size());
            benchmark::ClobberMemory();
        }
    }
    BENCHMARK(build_frozen_tree_is_free);

    /** \brief Tokenization alone, so the parse numbers can be read net of it. */
    void ripgrep_tokenize_only(benchmark::State& state) {
        for (auto _ : state) {
            raw_args line{"rg", "pat"};
            benchmark::DoNotOptimize(line.size());
        }
    }
    BENCHMARK(ripgrep_tokenize_only);

    /** \brief clap's `ripgrep.rs::startup::simple` — two tokens against 58 arguments. */
    void ripgrep_simple(benchmark::State& state) {
        const raw_args line{"rg", "pat"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(ripgrep_cli, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(ripgrep_simple);

    /** \brief clap's `ripgrep.rs::startup::complex` — clusters, `=` values and typed values. */
    void ripgrep_complex(benchmark::State& state) {
        const raw_args line{"rg",
                            "pat",
                            "-cFlN",
                            "-pqr=some",
                            "--null",
                            "--no-filename",
                            "--no-messages",
                            "-SH",
                            "-C5",
                            "--follow",
                            "-e some"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(ripgrep_cli, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(ripgrep_complex);

    /**
     * \brief clap's `ripgrep.rs::startup::xargs` — 1000 trailing positionals.
     *
     * \note Parameterized rather than pasted, and swept, because the shape of the curve is
     *       the finding: a single point cannot tell linear from quadratic.
     */
    void ripgrep_xargs(benchmark::State& state) {
        const raw_args line{std::from_range, xargs_line(static_cast<std::size_t>(state.range(0)))};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(ripgrep_cli, line);
            benchmark::DoNotOptimize(got);
        }
        state.SetItemsProcessed(state.iterations() * state.range(0));
    }
    // 1754 is clap's literal fixture size, kept so one row is a like-for-like comparison;
    // the smaller sizes are there to show the curve.
    BENCHMARK(ripgrep_xargs)->Arg(10)->Arg(100)->Arg(1000)->Arg(1754);

    /**
     * \brief Two tokens against an N-flag CLI: the cost of *having* arguments.
     *
     * \note The interesting quantity is nanoseconds per declared argument, which the
     *       `SetItemsProcessed` line makes Google Benchmark print directly. Flat means the
     *       sweep is linear; rising means it is not.
     */
    void fixed_cost_by_arg_count(benchmark::State& state) {
        const command_spec& spec = wall_of(state.range(0));
        const raw_args line{"wall", "--opt0"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(spec, line);
            benchmark::DoNotOptimize(got);
        }
        state.SetItemsProcessed(state.iterations() * state.range(0));
    }
    BENCHMARK(fixed_cost_by_arg_count)->Arg(1)->Arg(8)->Arg(16)->Arg(32)->Arg(64);

    /** \brief clap's `rustup.rs::startup::empty` — no subcommand chosen. */
    void rustup_empty(benchmark::State& state) {
        const raw_args line{"rustup"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(rustup_cli, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(rustup_empty);

    /**
     * \brief clap's `rustup.rs::startup::sc`, transcribed as the tokens a shell would pass.
     *
     * \note clap's fixture is the single token `"rustup override add stable"`, which reaches
     *       the parser as one argument and therefore never descends. Both spellings are
     *       benchmarked: `rustup_subcommand` is the real dispatch, `rustup_one_token` is the
     *       literal clap fixture, kept so the published Rust number has a like-for-like
     *       counterpart.
     */
    void rustup_subcommand(benchmark::State& state) {
        const raw_args line{"rustup", "override", "add", "stable"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(rustup_cli, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(rustup_subcommand);

    /** \brief clap's literal `startup::sc` fixture: one token that happens to contain spaces. */
    void rustup_one_token(benchmark::State& state) {
        const raw_args line{"rustup override add stable"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(rustup_cli, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(rustup_one_token);

    /** \brief The parse half of the derive path, for subtraction against the case below. */
    void derive_parse_only(benchmark::State& state) {
        static constexpr command_spec spec = clapp::command_of<demo_cli>();
        const raw_args line{"demo", "-n", "Alice", "--count", "3", "-v", "a.txt", "b.txt"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(spec, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(derive_parse_only);

    /** \brief `clapp::try_parse_from<T>` — parse plus filling the user's struct. */
    void derive_parse_and_materialize(benchmark::State& state) {
        const raw_args line{"demo", "-n", "Alice", "--count", "3", "-v", "a.txt", "b.txt"};
        for (auto _ : state) {
            std::expected<demo_cli, error> got = clapp::try_parse_from<demo_cli>(line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(derive_parse_and_materialize);

    /**
     * \brief The error path, which allocates more than the success path and is never timed
     *        by clap's benches at all.
     *
     * \note Worth a case because a CLI's *slow* path is the one a user hits by mistake, and
     *       because clapp::error carries a context map plus a rendered usage line — three to
     *       five `flat_map` inserts per error, none of which the success path performs.
     */
    void ripgrep_unknown_flag(benchmark::State& state) {
        const raw_args line{"rg", "--this-flag-does-not-exist"};
        for (auto _ : state) {
            std::expected<arg_matches, error> got = clapp::parse(ripgrep_cli, line);
            benchmark::DoNotOptimize(got);
        }
    }
    BENCHMARK(ripgrep_unknown_flag);

}  // namespace

BENCHMARK_MAIN();
