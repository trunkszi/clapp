#include "support/check.hpp"
#include "support/subprocess.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#if CLAPP_E2E_HAS_SUBPROCESS

using clapp::test::run;
using clapp::test::run_result;

namespace {

    /**
     * \brief Position of \p needle in \p haystack, or npos.
     * \param haystack The captured stdout.
     * \param needle The line to look for.
     * \return The offset, so two calls can be compared for ordering.
     */
    [[nodiscard]] std::size_t line_at(const std::string& haystack, const char* needle) {
        return haystack.find(needle);
    }

}  // namespace

CLAPP_TEST("find: interleaved flags come back in argv order") {
    const run_result r = run({"--empty", "--name", "foo", "-o", "--name", "bar"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    CLAPP_CHECK(r.out == "2 empty = true\n"
                         "4 name = foo\n"
                         "6 or = true\n"
                         "8 name = bar\n");
}

CLAPP_TEST("find: the same flags in the other order give a different answer") {
    // The case that a position-blind implementation cannot pass. Both runs use `--name`
    // and `--empty`; only the order differs, and the output must differ with it.
    const run_result first  = run({"--empty", "-o", "--name", "foo"});
    const run_result second = run({"--name", "foo", "-o", "--empty"});
    CLAPP_CHECK(first.status == 0);
    CLAPP_CHECK(second.status == 0);
    CLAPP_CHECK(first.out != second.out);

    CLAPP_CHECK(line_at(first.out, "empty = true") < line_at(first.out, "name = foo"));
    CLAPP_CHECK(line_at(second.out, "name = foo") < line_at(second.out, "empty = true"));
}

CLAPP_TEST("find: a repeated flag is reported once per occurrence") {
    // `action::append` plus the value-less-option trick: two `--empty` are two entries,
    // not one. A `set_true` flag would collapse them.
    const run_result r = run({"--empty", "-a", "--empty"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "2 empty = true\n"
                         "4 and = true\n"
                         "6 empty = true\n");
}

CLAPP_TEST("find: nothing on the command line reports nothing") {
    // The default_value of "false" is recorded for every flag, and must NOT be printed:
    // the example filters on clapp::value_source::command_line. Without that filter this
    // case would print four lines.
    const run_result r = run({});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err.empty());
}

CLAPP_TEST("find: one flag reports exactly one line") {
    const run_result r = run({"--empty"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "2 empty = true\n");
}

CLAPP_TEST("find: --name carries its value, not a bool") {
    const run_result r = run({"--name", "*.cpp"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.out == "2 name = *.cpp\n");
}

CLAPP_TEST("find: short and long spellings of an operator agree") {
    const run_result short_form = run({"--empty", "-o", "--empty"});
    const run_result long_form  = run({"--empty", "--or", "--empty"});
    CLAPP_CHECK(short_form.status == 0);
    CLAPP_CHECK(short_form.out == long_form.out);
}

CLAPP_TEST("find: an unknown option is an error") {
    const run_result r = run({"--bogus"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("bogus"));
}

CLAPP_TEST("find: --name with no value is an error") {
    const run_result r = run({"--name"});
    CLAPP_CHECK(r.status == 2);
    CLAPP_CHECK(r.out.empty());
    CLAPP_CHECK(r.err_has("--name"));
}

CLAPP_TEST("find: --help goes to stdout with status 0") {
    const run_result r = run({"--help"});
    CLAPP_CHECK(r.status == 0);
    CLAPP_CHECK(r.err.empty());
    // This example exists for its custom `help_heading`s, so the page is pinned whole:
    // the two custom sections come **after** `Options:`, in the order they were first
    // declared, and each measures its own description column rather than the page's.
    // Byte for byte what clap 4.6 prints for `examples/find.rs` (`clap/examples/find.md`),
    // apart from the example's own help prose.
    CLAPP_CHECK(r.out ==
                std::string_view{"Walk a directory tree, testing each entry\n"
                                 "\n"
                                 "Usage: find [OPTIONS]\n"
                                 "\n"
                                 "Options:\n"
                                 "  -h, --help     Print help\n"
                                 "  -V, --version  Print version\n"
                                 "\n"
                                 "TESTS:\n"
                                 "      --empty        File is empty and is either a regular "
                                 "file or a directory\n"
                                 "      --name <name>  Base of file name matches shell pattern "
                                 "pattern\n"
                                 "\n"
                                 "OPERATORS:\n"
                                 "  -o, --or   expr2 is not evaluated if expr1 is true\n"
                                 "  -a, --and  Same as `expr1 expr1`\n"});
}

#else

CLAPP_TEST("find: skipped, this platform has no fork/exec") { CLAPP_CHECK(true); }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
