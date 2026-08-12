#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/meta/annotations.hpp>
#include <clapp/output/help.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/output/textwrap.hpp>
#include <clapp/output/usage.hpp>

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::help_style;
    using clapp::styled_str;
    using clapp::value_range;

    // ===========================================================================
    // clap's `ripgrep.rs` prose table
    // ===========================================================================

    /**
     * \brief One row of clap's `lazy_static! { USAGES }` map.
     *
     * \note `std::string_view` members are fine here and are *not* the structural-type trap
     *       (CLAUDE.md trap 5): this table is an ordinary namespace-scope `constexpr` array
     *       that nothing reflects over and nothing feeds to `std::define_static_array`.
     */
    struct usage_doc {
        std::string_view name;       /**< The argument id, clap's map key. */
        std::string_view short_help; /**< clap's `Usage::short`. */
        std::string_view long_help;  /**< clap's `Usage::long`, with its trailing `"\n "`. */
    };

    /** \brief clap's 58 `doc!(h, …)` entries, mechanically transcribed. */
    inline constexpr usage_doc ripgrep_docs[] = {
            {.name       = "help-short",
             .short_help = "Show short help output.",
             .long_help  = "Show short help output. Use --help to show more details.\n "},
            {.name       = "help",
             .short_help = "Show verbose help output.",
             .long_help  = "When given, more details about flags are provided.\n "},
            {.name       = "version",
             .short_help = "Print version information.",
             .long_help  = "Print version information.\n "},
            {.name       = "pattern",
             .short_help = "A regular expression used for searching.",
             .long_help =
                     "A regular expression used for searching. Multiple patterns may be given. To"
                     " match a pattern beginning with a -, use [-].\n "},
            {.name       = "regexp",
             .short_help = "A regular expression used for searching.",
             .long_help =
                     "A regular expression used for searching. Multiple patterns may be given. To"
                     " match a pattern beginning with a -, use [-].\n "},
            {.name       = "path",
             .short_help = "A file or directory to search.",
             .long_help =
                     "A file or directory to search. Directories are searched recursively.\n "},
            {.name       = "files",
             .short_help = "Print each file that would be searched.",
             .long_help =
                     "Print each file that would be searched without actually performing the sear"
                     "ch. This is useful to determine whether a particular file is being searched"
                     " or not.\n "},
            {.name       = "type-list",
             .short_help = "Show all supported file types.",
             .long_help  = "Show all supported file types and their corresponding globs.\n "},
            {.name       = "text",
             .short_help = "Search binary files as if they were text.",
             .long_help  = "Search binary files as if they were text.\n "},
            {.name       = "count",
             .short_help = "Only show count of matches for each file.",
             .long_help  = "Only show count of matches for each file.\n "},
            {.name       = "color",
             .short_help = "When to use color. [default: auto]",
             .long_help =
                     "When to use color in the output. The possible values are never, auto, alway"
                     "s or ansi. The default is auto. When always is used, coloring is attempted "
                     "based on your environment. When ansi used, coloring is forcefully done usin"
                     "g ANSI escape color codes.\n "},
            {.name       = "colors",
             .short_help = "Configure color settings and styles.",
             .long_help =
                     "This flag specifies color settings for use in the output. This flag may be "
                     "provided multiple times. Settings are applied iteratively. Colors are limit"
                     "ed to one of eight choices: red, blue, green, cyan, magenta, yellow, white "
                     "and black. Styles are limited to nobold, bold, nointense or intense.\n\nThe"
                     " format of the flag is {type}:{attribute}:{value}. {type} should be one of "
                     "path, line or match. {attribute} can be fg, bg or style. {value} is either "
                     "a color (for fg and bg) or a text style. A special format, {type}:none, wil"
                     "l clear all color settings for {type}.\n\nFor example, the following comman"
                     "d will change the match color to magenta and the background color for line "
                     "numbers to yellow:\n\nrg --colors 'match:fg:magenta' --colors 'line:bg:yell"
                     "ow' foo.\n "},
            {.name       = "fixed-strings",
             .short_help = "Treat the pattern as a literal string.",
             .long_help =
                     "Treat the pattern as a literal string instead of a regular expression. When"
                     " this flag is used, special regular expression meta characters such as (){}"
                     "*+. do not need to be escaped.\n "},
            {.name       = "glob",
             .short_help = "Include or exclude files/directories.",
             .long_help =
                     "Include or exclude files/directories for searching that match the given glo"
                     "b. This always overrides any other ignore logic. Multiple glob flags may be"
                     " used. Globbing rules match .gitignore globs. Precede a glob with a ! to ex"
                     "clude it.\n "},
            {.name       = "ignore-case",
             .short_help = "Case insensitive search.",
             .long_help  = "Case insensitive search. This is overridden by --case-sensitive.\n "},
            {.name       = "line-number",
             .short_help = "Show line numbers.",
             .long_help =
                     "Show line numbers (1-based). This is enabled by default when searching in a"
                     " tty.\n "},
            {.name       = "no-line-number",
             .short_help = "Suppress line numbers.",
             .long_help =
                     "Suppress line numbers. This is enabled by default when NOT searching in a t"
                     "ty.\n "},
            {.name       = "quiet",
             .short_help = "Do not print anything to stdout.",
             .long_help =
                     "Do not print anything to stdout. If a match is found in a file, stop search"
                     "ing. This is useful when ripgrep is used only for its exit code.\n "},
            {.name       = "type",
             .short_help = "Only search files matching TYPE.",
             .long_help =
                     "Only search files matching TYPE. Multiple type flags may be provided. Use t"
                     "he --type-list flag to list all available types.\n "},
            {.name       = "type-not",
             .short_help = "Do not search files matching TYPE.",
             .long_help =
                     "Do not search files matching TYPE. Multiple type-not flags may be provided."
                     " Use the --type-list flag to list all available types.\n "},
            {.name       = "unrestricted",
             .short_help = "Reduce the level of \"smart\" searching.",
             .long_help =
                     "Reduce the level of \"smart\" searching. A single -u won't respect .gitigno"
                     "re (etc.) files. Two -u flags will additionally search hidden files and dir"
                     "ectories. Three -u flags will additionally search binary files. -uu is roug"
                     "hly equivalent to grep -r and -uuu is roughly equivalent to grep -a -r.\n "},
            {.name       = "invert-match",
             .short_help = "Invert matching.",
             .long_help  = "Invert matching. Show lines that don't match given patterns.\n "},
            {.name       = "word-regexp",
             .short_help = "Only show matches surrounded by word boundaries.",
             .long_help =
                     "Only show matches surrounded by word boundaries. This is equivalent to putt"
                     "ing \\b before and after all of the search patterns.\n "},
            {.name       = "after-context",
             .short_help = "Show NUM lines after each match.",
             .long_help  = "Show NUM lines after each match.\n "},
            {.name       = "before-context",
             .short_help = "Show NUM lines before each match.",
             .long_help  = "Show NUM lines before each match.\n "},
            {.name       = "context",
             .short_help = "Show NUM lines before and after each match.",
             .long_help  = "Show NUM lines before and after each match.\n "},
            {.name       = "column",
             .short_help = "Show column numbers",
             .long_help =
                     "Show column numbers (1-based). This only shows the column numbers for the f"
                     "irst match on each line. This does not try to account for Unicode. One byte"
                     " is equal to one column. This implies --line-number.\n "},
            {.name       = "context-separator",
             .short_help = "Set the context separator string. [default: --]",
             .long_help =
                     "The string used to separate non-contiguous context lines in the output. Esc"
                     "ape sequences like \\x7F or \\t may be used. The default value is --.\n "},
            {.name       = "debug",
             .short_help = "Show debug messages.",
             .long_help  = "Show debug messages. Please use this when filing a bug report.\n "},
            {.name       = "file",
             .short_help = "Search for patterns from the given file.",
             .long_help =
                     "Search for patterns from the given file, with one pattern per line. When th"
                     "is flag is used or multiple times or in combination with the -e/--regexp fl"
                     "ag, then all patterns provided are searched. Empty pattern lines will match"
                     " all input lines, and the newline is not counted as part of the pattern.\n "},
            {.name       = "files-with-matches",
             .short_help = "Only show the path of each file with at least one match.",
             .long_help  = "Only show the path of each file with at least one match.\n "},
            {.name       = "files-without-match",
             .short_help = "Only show the path of each file that contains zero matches.",
             .long_help  = "Only show the path of each file that contains zero matches.\n "},
            {.name       = "with-filename",
             .short_help = "Show file name for each match.",
             .long_help =
                     "Prefix each match with the file name that contains it. This is the default "
                     "when more than one file is searched.\n "},
            {.name       = "no-filename",
             .short_help = "Never show the file name for a match.",
             .long_help =
                     "Never show the file name for a match. This is the default when one file is "
                     "searched.\n "},
            {.name       = "heading",
             .short_help = "Show matches grouped by each file.",
             .long_help =
                     "This shows the file name above clusters of matches from each file instead o"
                     "f showing the file name for every match. This is the default mode at a tty."
                     "\n "},
            {.name       = "no-heading",
             .short_help = "Don't group matches by each file.",
             .long_help =
                     "Don't group matches by each file. If -H/--with-filename is enabled, then fi"
                     "le names will be shown for every line matched. This is the default mode whe"
                     "n not at a tty.\n "},
            {.name       = "hidden",
             .short_help = "Search hidden files and directories.",
             .long_help =
                     "Search hidden files and directories. By default, hidden files and directori"
                     "es are skipped.\n "},
            {.name       = "ignore-file",
             .short_help = "Specify additional ignore files.",
             .long_help =
                     "Specify additional ignore files for filtering file paths. Ignore files shou"
                     "ld be in the gitignore format and are matched relative to the current worki"
                     "ng directory. These ignore files have lower precedence than all other ignor"
                     "e files. When specifying multiple ignore files, earlier files have lower pr"
                     "ecedence than later files.\n "},
            {.name       = "follow",
             .short_help = "Follow symbolic links.",
             .long_help  = "Follow symbolic links.\n "},
            {.name       = "max-count",
             .short_help = "Limit the number of matches.",
             .long_help  = "Limit the number of matching lines per file searched to NUM.\n "},
            {.name       = "maxdepth",
             .short_help = "Descend at most NUM directories.",
             .long_help =
                     "Limit the depth of directory traversal to NUM levels beyond the paths given"
                     ". A value of zero only searches the starting-points themselves.\n\nFor exam"
                     "ple, 'rg --maxdepth 0 dir/' is a no-op because dir/ will not be descended i"
                     "nto. 'rg --maxdepth 1 dir/' will search only the direct children of dir/.\n"
                     " "},
            {.name       = "mmap",
             .short_help = "Searching using memory maps when possible.",
             .long_help =
                     "Search using memory maps when possible. This is enabled by default when rip"
                     "grep thinks it will be faster. Note that memory map searching doesn't curre"
                     "ntly support all options, so if an incompatible option (e.g., --context) is"
                     " given with --mmap, then memory maps will not be used.\n "},
            {.name       = "no-messages",
             .short_help = "Suppress all error messages.",
             .long_help =
                     "Suppress all error messages. This is equivalent to redirecting stderr to /d"
                     "ev/null.\n "},
            {.name       = "no-mmap",
             .short_help = "Never use memory maps.",
             .long_help  = "Never use memory maps, even when they might be faster.\n "},
            {.name       = "no-ignore",
             .short_help = "Don't respect ignore files.",
             .long_help =
                     "Don't respect ignore files (.gitignore, .ignore, etc.). This implies --no-i"
                     "gnore-parent and --no-ignore-vcs.\n "},
            {.name       = "no-ignore-parent",
             .short_help = "Don't respect ignore files in parent directories.",
             .long_help =
                     "Don't respect ignore files (.gitignore, .ignore, etc.) in parent directorie"
                     "s.\n "},
            {.name       = "no-ignore-vcs",
             .short_help = "Don't respect VCS ignore files",
             .long_help =
                     "Don't respect version control ignore files (.gitignore, etc.). This implies"
                     " --no-ignore-parent. Note that .ignore files will continue to be respected."
                     "\n "},
            {.name       = "null",
             .short_help = "Print NUL byte after file names",
             .long_help =
                     "Whenever a file name is printed, follow it with a NUL byte. This includes p"
                     "rinting file names before matches, and when printing a list of matching fil"
                     "es such as with --count, --files-with-matches and --files. This option is u"
                     "seful for use with xargs.\n "},
            {.name       = "path-separator",
             .short_help = "Path separator to use when printing file paths.",
             .long_help =
                     "The path separator to use when printing file paths. This defaults to your p"
                     "latform's path separator, which is / on Unix and \\ on Windows. This flag i"
                     "s intended for overriding the default when the environment demands it (e.g."
                     ", cygwin). A path separator is limited to a single byte.\n "},
            {.name       = "pretty",
             .short_help = "Alias for --color always --heading -n.",
             .long_help  = "Alias for --color always --heading -n.\n "},
            {.name       = "replace",
             .short_help = "Replace matches with string given.",
             .long_help =
                     "Replace every match with the string given when printing results. Neither th"
                     "is flag nor any other flag will modify your files.\n\nCapture group indices"
                     " (e.g., $5) and names (e.g., $foo) are supported in the replacement string."
                     "\n\nNote that the replacement by default replaces each match, and NOT the e"
                     "ntire line. To replace the entire line, you should match the entire line.\n"
                     " "},
            {.name       = "case-sensitive",
             .short_help = "Search case sensitively.",
             .long_help =
                     "Search case sensitively. This overrides -i/--ignore-case and -S/--smart-cas"
                     "e.\n "},
            {.name       = "smart-case",
             .short_help = "Smart case search.",
             .long_help =
                     "Searches case insensitively if the pattern is all lowercase. Search case se"
                     "nsitively otherwise. This is overridden by either -s/--case-sensitive or -i"
                     "/--ignore-case.\n "},
            {.name       = "sort-files",
             .short_help = "Sort results by file path. Implies --threads=1.",
             .long_help =
                     "Sort results by file path. Note that this currently disables all parallelis"
                     "m and runs search in a single thread.\n "},
            {.name       = "threads",
             .short_help = "The approximate number of threads to use.",
             .long_help =
                     "The approximate number of threads to use. A value of 0 (which is the defaul"
                     "t) causes ripgrep to choose the thread count using heuristics.\n "},
            {.name       = "vimgrep",
             .short_help = "Show results in vim compatible format.",
             .long_help =
                     "Show results with every match on its own line, including line numbers and c"
                     "olumn numbers. With this option, a line with more than one match will be pr"
                     "inted more than once.\n "},
            {.name       = "type-add",
             .short_help = "Add a new glob for a file type.",
             .long_help =
                     "Add a new glob for a particular file type. Only one glob can be added at a "
                     "time. Multiple --type-add flags can be provided. Unless --type-clear is use"
                     "d, globs are added to any existing globs defined inside of ripgrep.\n\nNote"
                     " that this MUST be passed to every invocation of ripgrep. Type settings are"
                     " NOT persisted.\n\nExample: rg --type-add 'foo:*.foo' -tfoo PATTERN.\n\n--t"
                     "ype-add can also be used to include rules from other types with the special"
                     " include directive. The include directive permits specifying one or more ot"
                     "her type names (separated by a comma) that have been defined and its rules "
                     "will automatically be imported into the type specified. For example, to cre"
                     "ate a type called src that matches C++, Python and Markdown files, one can "
                     "use:\n\n--type-add 'src:include:cpp,py,md'\n\nAdditional glob rules can sti"
                     "ll be added to the src type by using the --type-add flag again:\n\n--type-a"
                     "dd 'src:include:cpp,py,md' --type-add 'src:*.foo'\n\nNote that type names m"
                     "ust consist only of Unicode letters or numbers. Punctuation characters are "
                     "not allowed.\n "},
            {.name       = "type-clear",
             .short_help = "Clear globs for given file type.",
             .long_help =
                     "Clear the file type globs previously defined for TYPE. This only clears the"
                     " default type definitions that are found inside of ripgrep.\n\nNote that th"
                     "is MUST be passed to every invocation of ripgrep. Type settings are NOT per"
                     "sisted.\n "},
    };

    /**
     * \brief clap's `|k| USAGES[k].short` / `.long` closure.
     * \param name     The argument id.
     * \param use_long Which column.
     * \return The help text.
     *
     * \note A miss aborts constant evaluation rather than returning an empty string: a fixture
     *       that quietly renders a page with one description missing would look faster.
     */
    consteval std::string_view doc_of(std::string_view name, bool use_long) {
        for (const usage_doc& one : ripgrep_docs)
            if (one.name == name) return use_long ? one.long_help : one.short_help;
        std::abort();
    }

    // ===========================================================================
    // The command tree
    // ===========================================================================

    /** \brief clap's `ABOUT`. */
    inline constexpr std::string_view ripgrep_about =
            "\nripgrep (rg) recursively searches your current directory for a regex "
            "pattern.\n\nripgrep's regex engine uses finite automata and guarantees linear "
            "time\nsearching. Because of this, features like backreferences and "
            "arbitrary\nlookaround are not supported.\n\nProject home page: "
            "https://github.com/BurntSushi/ripgrep\n\nUse -h for short descriptions and "
            "--help for more details.";

    /** \brief clap's `USAGE`, handed to `override_usage()`. */
    inline constexpr std::string_view ripgrep_usage =
            "\n    rg [OPTIONS] <pattern> [<path> ...]\n    rg [OPTIONS] [-e PATTERN | -f "
            "FILE ]... [<path> ...]\n    rg [OPTIONS] --files [<path> ...]\n    rg [OPTIONS] "
            "--type-list";

    /**
     * \brief clap's `TEMPLATE`.
     *
     * \note Every placeholder in it — `{name}` `{version}` `{author}` `{about}` `{usage}`
     *       `{positionals}` `{options}` — is one clapp implements, so the transcription is
     *       exact rather than approximate. That matters for the comparison: `{positionals}`
     *       and `{options}` hand the renderer the *unfiltered* argument list and make it
     *       re-filter, which is a different amount of work from the `{all-args}` default.
     */
    inline constexpr std::string_view ripgrep_template =
            "{name} {version}\n{author}\n{about}\n\nUSAGE:{usage}\n\nARGS:\n{positionals}"
            "\n\nOPTIONS:\n{options}";

    /**
     * \brief `--color`'s domain — clap's `value_parser(["never", "auto", "always", "ansi"])`.
     *
     * \note Identical to the enum in benches/parser/parse_bench.cpp, and deliberately so:
     *       the `[possible values: …]` line it produces is part of what the help renderer
     *       lays out, and long help spells that list out where short help does not.
     */
    enum class color_when : unsigned char {
        never [[maybe_unused]],
        automatic [[maybe_unused]] [[= clapp::value{.name = "auto"}]],
        always [[maybe_unused]],
        ansi [[maybe_unused]],
    };

    /** \brief clap's `let arg = |name| Arg::new(name).help(doc(name));`. */
    consteval arg_builder documented(std::string_view name, bool use_long) {
        return arg_builder(name).help(doc_of(name, use_long));
    }

    /** \brief clap's `let flag = |name| arg(name).long(name).action(SetTrue);`. */
    consteval arg_builder flag(std::string_view name, bool use_long) {
        return documented(name, use_long).long_(name).action(arg_action::set_true);
    }

    /**
     * \brief Build clap's `ripgrep.rs::cmd()`, argument for argument.
     * \param use_long Which column of the `doc!` table to hand to every `help()`.
     * \return The frozen tree.
     */
    consteval command_spec make_ripgrep(bool use_long) {
        command_builder app("ripgrep");
        std::move(app)
                .author("BurntSushi")
                .version("0.4.0")
                .about(ripgrep_about)
                .max_term_width(100)
                .override_usage(ripgrep_usage)
                .help_template(ripgrep_template)
                .disable_help_flag()
                .disable_version_flag()
                .arg(documented("help-short", use_long).short_('h'))
                .arg(flag("help", use_long))
                .arg(flag("version", use_long).short_('V'))
                .arg(documented("pattern", use_long)
                             .required_unless_present_any({"file",
                                                           "files",
                                                           "help-short",
                                                           "help",
                                                           "regexp",
                                                           "type-list",
                                                           "version"}))
                .arg(documented("path", use_long).num_args(value_range::at_least(1)))
                .arg(flag("regexp", use_long)
                             .short_('e')
                             .allow_hyphen_values()
                             .action(arg_action::append)
                             .value_name("pattern"))
                .arg(flag("files", use_long).conflicts_with_all({"file", "regexp", "type-list"}))
                .arg(flag("type-list", use_long)
                             .conflicts_with_all({"file", "files", "pattern", "regexp"}))
                .arg(flag("text", use_long).short_('a'))
                .arg(flag("count", use_long).short_('c'))
                .arg(flag("color", use_long)
                             .value_name("WHEN")
                             .action(arg_action::set)
                             .hide_possible_values()
                             .value_parser<color_when>())
                .arg(flag("colors", use_long).value_name("SPEC").action(arg_action::append))
                .arg(flag("fixed-strings", use_long).short_('F'))
                .arg(flag("glob", use_long)
                             .short_('g')
                             .action(arg_action::append)
                             .value_name("GLOB"))
                .arg(flag("ignore-case", use_long).short_('i'))
                .arg(flag("line-number", use_long).short_('n'))
                .arg(flag("no-line-number", use_long).short_('N'))
                .arg(flag("quiet", use_long).short_('q'))
                .arg(flag("type", use_long)
                             .short_('t')
                             .action(arg_action::append)
                             .value_name("TYPE"))
                .arg(flag("type-not", use_long)
                             .short_('T')
                             .action(arg_action::append)
                             .value_name("TYPE"))
                .arg(flag("unrestricted", use_long).short_('u').action(arg_action::append))
                .arg(flag("invert-match", use_long).short_('v'))
                .arg(flag("word-regexp", use_long).short_('w'))
                .arg(flag("after-context", use_long)
                             .short_('A')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("before-context", use_long)
                             .short_('B')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("context", use_long)
                             .short_('C')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("column", use_long))
                .arg(flag("context-separator", use_long).value_name("SEPARATOR"))
                .arg(flag("debug", use_long))
                .arg(flag("file", use_long)
                             .short_('f')
                             .value_name("FILE")
                             .action(arg_action::append))
                .arg(flag("files-with-matches", use_long).short_('l'))
                .arg(flag("files-without-match", use_long))
                .arg(flag("with-filename", use_long).short_('H'))
                .arg(flag("no-filename", use_long))
                .arg(flag("heading", use_long).overrides_with("no-heading"))
                .arg(flag("no-heading", use_long).overrides_with("heading"))
                .arg(flag("hidden", use_long))
                .arg(flag("ignore-file", use_long).value_name("FILE").action(arg_action::append))
                .arg(flag("follow", use_long).short_('L'))
                .arg(flag("max-count", use_long)
                             .short_('m')
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("maxdepth", use_long)
                             .action(arg_action::set)
                             .value_name("NUM")
                             .value_parser<std::size_t>())
                .arg(flag("mmap", use_long))
                .arg(flag("no-messages", use_long))
                .arg(flag("no-mmap", use_long))
                .arg(flag("no-ignore", use_long))
                .arg(flag("no-ignore-parent", use_long))
                .arg(flag("no-ignore-vcs", use_long))
                .arg(flag("null", use_long))
                .arg(flag("path-separator", use_long).value_name("SEPARATOR"))
                .arg(flag("pretty", use_long).short_('p'))
                .arg(flag("replace", use_long)
                             .short_('r')
                             .action(arg_action::set)
                             .value_name("ARG"))
                .arg(flag("case-sensitive", use_long).short_('s'))
                .arg(flag("smart-case", use_long).short_('S'))
                .arg(flag("sort-files", use_long))
                .arg(flag("threads", use_long)
                             .short_('j')
                             .action(arg_action::set)
                             .value_name("ARG")
                             .value_parser<std::size_t>())
                .arg(flag("vimgrep", use_long))
                .arg(flag("type-add", use_long).value_name("TYPE").action(arg_action::append))
                .arg(flag("type-clear", use_long).value_name("TYPE").action(arg_action::append));
        return app.freeze();
    }

    inline constexpr command_spec ripgrep_short_cli = make_ripgrep(false);
    inline constexpr command_spec ripgrep_long_cli  = make_ripgrep(true);

    // The fixture is asserted, not assumed. `doc_of()` already refuses to compile on a name
    // the table does not carry, so what is left to pin is that both trees really are the
    // 58-argument ripgrep and that they really do differ — a copy-paste that built the same
    // spec twice would report a flat 43 µs for both and look like a win.
    static_assert(ripgrep_short_cli.get_arguments().size() == 58);
    static_assert(ripgrep_long_cli.get_arguments().size() == 58);
    static_assert(ripgrep_short_cli.has_arg("pattern"));
    static_assert(ripgrep_short_cli.find_arg("pattern")->get_help() !=
                  ripgrep_long_cli.find_arg("pattern")->get_help());
    static_assert(ripgrep_short_cli.get_help_template().has_value());
    static_assert(ripgrep_short_cli.get_override_usage().has_value());

    // ===========================================================================
    // Synthetic trees, for the two size terms
    // ===========================================================================

    /**
     * \brief `opt-N` / `sub-N`, built without forming a `std::string` from a pointer and
     *        a length.
     * \param prefix Three letters; `opt` or `sub`.
     * \param index  The ordinal.
     * \return An owning string; `freeze()` promotes it into static storage.
     *
     * \note `push_back` rather than `std::to_string` or `operator+=`, for the reason
     *       benches/parser/parse_bench.cpp gives: libstdc++'s
     *       `basic_string(const CharT*, size_type)` tests its source pointer, and GCC under
     *       `-fsanitize=null` will not fold that comparison during constant evaluation
     *       (CLAUDE.md trap 10). The bench preset carries no sanitizer; a fixture that only
     *       compiles under one preset is a trap for whoever moves it.
     */
    consteval std::string numbered(std::string_view prefix, std::size_t index) {
        std::string out;
        for (const char byte : prefix) out.push_back(byte);
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

    /** \brief A command with \p N documented long flags and nothing else. */
    template<std::size_t N>
    consteval command_spec make_flag_wall() {
        command_builder app("wall");
        for (std::size_t i = 0; i < N; ++i) {
            const std::string name = numbered("opt", i);
            std::move(app).arg(arg_builder(name)
                                       .long_(name)
                                       .action(arg_action::set_true)
                                       .help("Turn the corresponding thing on or off."));
        }
        return app.freeze();
    }

    /** \brief A command with \p N documented subcommands and nothing else. */
    template<std::size_t N>
    consteval command_spec make_subcommand_wall() {
        command_builder app("tree");
        for (std::size_t i = 0; i < N; ++i)
            std::move(app).subcommand(
                    command_builder(numbered("sub", i)).about("Do the corresponding thing."));
        return app.freeze();
    }

    inline constexpr command_spec arg_wall_1  = make_flag_wall<1>();
    inline constexpr command_spec arg_wall_8  = make_flag_wall<8>();
    inline constexpr command_spec arg_wall_16 = make_flag_wall<16>();
    inline constexpr command_spec arg_wall_32 = make_flag_wall<32>();
    inline constexpr command_spec arg_wall_64 = make_flag_wall<64>();

    inline constexpr command_spec sub_wall_1  = make_subcommand_wall<1>();
    inline constexpr command_spec sub_wall_8  = make_subcommand_wall<8>();
    inline constexpr command_spec sub_wall_16 = make_subcommand_wall<16>();
    inline constexpr command_spec sub_wall_32 = make_subcommand_wall<32>();
    inline constexpr command_spec sub_wall_64 = make_subcommand_wall<64>();

    static_assert(arg_wall_64.get_arguments().size() == 65);    // + the injected --help
    static_assert(sub_wall_64.get_subcommands().size() == 65);  // + the injected `help`

    /** \brief Pick the argument wall for a benchmark argument. */
    const command_spec& arg_wall_of(benchmark::IterationCount count) {
        switch (count) {
        case 1:
            return arg_wall_1;
        case 8:
            return arg_wall_8;
        case 16:
            return arg_wall_16;
        case 32:
            return arg_wall_32;
        default:
            return arg_wall_64;
        }
    }

    /** \brief Pick the subcommand wall for a benchmark argument. */
    const command_spec& sub_wall_of(benchmark::IterationCount count) {
        switch (count) {
        case 1:
            return sub_wall_1;
        case 8:
            return sub_wall_8;
        case 16:
            return sub_wall_16;
        case 32:
            return sub_wall_32;
        default:
            return sub_wall_64;
        }
    }

    // ===========================================================================
    // Text for the wrapper cases
    // ===========================================================================

    /**
     * \brief ripgrep's longest description, as plain prose: `--type-add`, 1,050 bytes.
     *
     * \note Indexed rather than pasted so that it stays the same bytes clap wraps. It is
     *       the longest entry in the table, which is why it is the one worth wrapping.
     */
    inline constexpr std::string_view plain_paragraph = ripgrep_docs[56].long_help;  // type-add

    /**
     * \brief The same paragraph with SGR colour around a few words.
     *
     * \note The escapes here contain no `U+0020`, so this is the *common* case — the one
     *       that was always safe and that kept the whole suite green over trap 15.
     */
    inline constexpr std::string_view sgr_paragraph =
            "Add a new glob for a particular \x1B[1mfile type\x1B[0m. Only one glob can be "
            "added at a time. Multiple \x1B[32m--type-add\x1B[0m flags can be provided. "
            "Unless \x1B[32m--type-clear\x1B[0m is used, globs are added to any existing "
            "globs defined inside of \x1B[1mripgrep\x1B[0m.";

    /**
     * \brief The same paragraph carrying sequences that contain a space.
     *
     * \note `ESC [ 1 SP q` is DECSCUSR — the space is a CSI *intermediate* byte — and the
     *       OSC payload is prose, so it is full of them. This is the input CLAUDE.md trap 15
     *       is about, and the reason `display_width()` and the word splitter must agree that
     *       an escape is one atomic unit. Benchmark it: the step-over is a per-byte branch,
     *       and the next person to "optimise" the splitter needs to see what it costs.
     */
    inline constexpr std::string_view spacey_escape_paragraph =
            "Add a new glob \x1B[1 qfor a particular file type. Only one glob can be added "
            "at a time. \x1B]0;rg — searching the whole tree\x07Multiple --type-add flags "
            "can be provided. Unless --type-clear is used, globs are added to any existing "
            "globs defined inside of ripgrep.";

    /**
     * \brief A run-time copy of a compile-time paragraph, plus a view the optimiser cannot
     *        see through.
     *
     * \warning **Not a formality — this is the whole reason the wrapper cases mean
     *          anything.** clapp::display_width() and clapp::wrap() are `constexpr`, and
     *          every paragraph above is a `constexpr std::string_view`, so GCC 16.1.0 folds
     *          the call at `-O2` and the benchmark times nothing. Measured before this type
     *          existed: `display_width_plain` reported **0.222 ns for a 1,050-byte scan**,
     *          i.e. 4.3 TiB/s, which is not a rate any machine produces — and it is the
     *          shape of wrong answer that reads as a spectacular result rather than as a
     *          broken measurement. Copying at run time and passing the view through
     *          `benchmark::DoNotOptimize` is what makes the loop do the work.
     */
    class opaque_text {
    public:
        /** \brief Copy \p text into heap storage the compiler must treat as unknown. */
        explicit opaque_text(std::string_view text) : owned_(text.begin(), text.end()) {}

        /** \brief A view of the copy, laundered through `DoNotOptimize`. */
        [[nodiscard]] std::string_view view() const {
            std::string_view borrowed{owned_};
            benchmark::DoNotOptimize(borrowed);
            return borrowed;
        }

        /** \brief How many bytes, for `SetBytesProcessed`. */
        [[nodiscard]] benchmark::IterationCount size() const {
            return static_cast<benchmark::IterationCount>(owned_.size());
        }

    private:
        std::string owned_;
    };

    // ===========================================================================
    // Benchmarks
    // ===========================================================================

    /**
     * \brief clap's `ripgrep.rs::render_help::short_help` — `-h` layout, short strings.
     *
     * \note `.to_string()` is included because clap's `build_help()` includes it
     *       (`cmd.render_help(); help.to_string()`). `styled_str_to_string` below reports
     *       that half separately so the comparison can be made either way.
     */
    void ripgrep_short_strings(benchmark::State& state) {
        const benchmark::IterationCount page_bytes = static_cast<benchmark::IterationCount>(
                clapp::render_help(ripgrep_short_cli, help_style::short_form()).to_string().size());
        for (auto _ : state) {
            std::string page =
                    clapp::render_help(ripgrep_short_cli, help_style::short_form()).to_string();
            benchmark::DoNotOptimize(page);
        }
        state.SetBytesProcessed(state.iterations() * page_bytes);
    }
    BENCHMARK(ripgrep_short_strings);

    /**
     * \brief clap's `ripgrep.rs::render_help::long_help` — the SAME `-h` layout over the
     *        long strings. Not `--help`; see this file's header.
     */
    void ripgrep_long_strings(benchmark::State& state) {
        const benchmark::IterationCount page_bytes = static_cast<benchmark::IterationCount>(
                clapp::render_help(ripgrep_long_cli, help_style::short_form()).to_string().size());
        for (auto _ : state) {
            std::string page =
                    clapp::render_help(ripgrep_long_cli, help_style::short_form()).to_string();
            benchmark::DoNotOptimize(page);
        }
        state.SetBytesProcessed(state.iterations() * page_bytes);
    }
    BENCHMARK(ripgrep_long_strings);

    /**
     * \brief What `--help` actually costs: long layout over the long strings.
     *
     * \note clap's bench has no counterpart, so this number stands alone. It is the one a
     *       clapp user pays, and it is the larger of the two: long form puts every
     *       description on its own line and spells `[possible values: …]` out.
     */
    void ripgrep_long_form_page(benchmark::State& state) {
        for (auto _ : state) {
            std::string page =
                    clapp::render_help(ripgrep_long_cli, help_style::long_form()).to_string();
            benchmark::DoNotOptimize(page);
        }
    }
    BENCHMARK(ripgrep_long_form_page);

    /**
     * \brief The usage line alone, so the page numbers can be read net of it.
     *
     * \note This fixture calls `override_usage()`, so the renderer's smart-usage machinery
     *       is short-circuited — which is the point: it isolates what the *rest* of the page
     *       costs. `usage_line_computed` below is the same call on a tree that has no
     *       override and therefore has to derive the line.
     */
    void usage_line_overridden(benchmark::State& state) {
        for (auto _ : state) {
            std::optional<styled_str> line = clapp::render_usage(ripgrep_short_cli);
            benchmark::DoNotOptimize(line);
        }
    }
    BENCHMARK(usage_line_overridden);

    /** \brief The usage line derived from the tree — `required_graph()` plus the walk. */
    void usage_line_computed(benchmark::State& state) {
        for (auto _ : state) {
            std::optional<styled_str> line = clapp::render_usage(arg_wall_64);
            benchmark::DoNotOptimize(line);
        }
    }
    BENCHMARK(usage_line_computed);

    /** \brief Flattening the spans to bytes — the other subtrahend. */
    void styled_str_to_string(benchmark::State& state) {
        const styled_str page = clapp::render_help(ripgrep_long_cli, help_style::short_form());
        const std::size_t page_bytes = page.to_string().size();
        for (auto _ : state) {
            std::string bytes = page.to_string();
            benchmark::DoNotOptimize(bytes);
        }
        state.SetBytesProcessed(
                state.iterations() * static_cast<benchmark::IterationCount>(page_bytes));
    }
    BENCHMARK(styled_str_to_string);

    /** \brief The version line — clap's `Command::_render_version`. */
    void version_line(benchmark::State& state) {
        for (auto _ : state) {
            std::string line = clapp::render_version(ripgrep_short_cli, false).to_string();
            benchmark::DoNotOptimize(line);
        }
    }
    BENCHMARK(version_line);

    /** \brief `clapp::wrap` over plain prose — the inner loop of every producer. */
    void wrap_plain(benchmark::State& state) {
        opaque_text subject{plain_paragraph};
        for (auto _ : state) {
            std::string wrapped = clapp::wrap(subject.view(), 80);
            benchmark::DoNotOptimize(wrapped);
        }
        state.SetBytesProcessed(state.iterations() * subject.size());
    }
    BENCHMARK(wrap_plain);

    /** \brief `clapp::wrap` over text with space-free SGR escapes — the common case. */
    void wrap_sgr(benchmark::State& state) {
        opaque_text subject{sgr_paragraph};
        for (auto _ : state) {
            std::string wrapped = clapp::wrap(subject.view(), 80);
            benchmark::DoNotOptimize(wrapped);
        }
        state.SetBytesProcessed(state.iterations() * subject.size());
    }
    BENCHMARK(wrap_sgr);

    /** \brief `clapp::wrap` over text with sequences that contain `U+0020` — trap 15's input. */
    void wrap_spacey_escape(benchmark::State& state) {
        opaque_text subject{spacey_escape_paragraph};
        for (auto _ : state) {
            std::string wrapped = clapp::wrap(subject.view(), 80);
            benchmark::DoNotOptimize(wrapped);
        }
        state.SetBytesProcessed(state.iterations() * subject.size());
    }
    BENCHMARK(wrap_spacey_escape);

    /** \brief The measuring half of trap 15's pair, on plain text. */
    void display_width_plain(benchmark::State& state) {
        opaque_text subject{plain_paragraph};
        for (auto _ : state) benchmark::DoNotOptimize(clapp::display_width(subject.view()));
        state.SetBytesProcessed(state.iterations() * subject.size());
    }
    BENCHMARK(display_width_plain);

    /** \brief The measuring half of trap 15's pair, on escape-bearing text. */
    void display_width_sgr(benchmark::State& state) {
        opaque_text subject{sgr_paragraph};
        for (auto _ : state) benchmark::DoNotOptimize(clapp::display_width(subject.view()));
        state.SetBytesProcessed(state.iterations() * subject.size());
    }
    BENCHMARK(display_width_sgr);

    /** \brief `strip_escapes()` — what every producer now ends with (差异清单 entry 29). */
    void strip_escapes_sgr(benchmark::State& state) {
        opaque_text subject{sgr_paragraph};
        for (auto _ : state) {
            std::string clean = clapp::strip_escapes(subject.view());
            benchmark::DoNotOptimize(clean);
        }
        state.SetBytesProcessed(state.iterations() * subject.size());
    }
    BENCHMARK(strip_escapes_sgr);

    /**
     * \brief Rendering a page whose only content is \p N documented flags.
     *
     * \note `SetItemsProcessed` makes Google Benchmark print nanoseconds per declared
     *       argument directly. Flat means the layout is linear in the argument count;
     *       rising means `write_args`'s measure-then-sort is showing.
     */
    void help_by_arg_count(benchmark::State& state) {
        const command_spec& spec = arg_wall_of(state.range(0));
        for (auto _ : state) {
            std::string page = clapp::render_help(spec, help_style::short_form()).to_string();
            benchmark::DoNotOptimize(page);
        }
        state.SetItemsProcessed(state.iterations() * state.range(0));
    }
    BENCHMARK(help_by_arg_count)->Arg(1)->Arg(8)->Arg(16)->Arg(32)->Arg(64);

    /**
     * \brief Rendering a page whose only content is \p N documented subcommands.
     *
     * \note A separate term from the one above: the subcommand section has its own column
     *       measurement and its own sort, and nothing else in this file reaches
     *       `write_subcommands`.
     */
    void help_by_subcommand_count(benchmark::State& state) {
        const command_spec& spec = sub_wall_of(state.range(0));
        for (auto _ : state) {
            std::string page = clapp::render_help(spec, help_style::short_form()).to_string();
            benchmark::DoNotOptimize(page);
        }
        state.SetItemsProcessed(state.iterations() * state.range(0));
    }
    BENCHMARK(help_by_subcommand_count)->Arg(1)->Arg(8)->Arg(16)->Arg(32)->Arg(64);

}  // namespace

BENCHMARK_MAIN();
