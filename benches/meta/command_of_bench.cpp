#include <clapp/clapp.hpp>

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// ===========================================================================
// Knobs
// ===========================================================================

#ifndef CLAPP_BENCH_FIELDS
#    define CLAPP_BENCH_FIELDS 50
#endif

#ifndef CLAPP_BENCH_SUBCOMMANDS
#    define CLAPP_BENCH_SUBCOMMANDS 5
#endif

/** 1 = headers only, 2 = also define the types, 3 = also call `command_of`. */
#ifndef CLAPP_BENCH_MODE
#    define CLAPP_BENCH_MODE 3
#endif

// ===========================================================================
// Field generation
// ===========================================================================

/**
 * \brief Ten members, one per distinct cost row of the deduction table.
 *
 * \note Each name is unique because \p P is, and each yields a distinct kebab-cased long
 *       option — `freeze()`'s duplicate detection sorts them and scans adjacent pairs,
 *       so a file whose options all collided would measure a different code path.
 */
#define CLAPP_BENCH_TEN_FIELDS(P)                                                                  \
    bool P##_flag_a{};                                                                             \
    std::optional<std::string> P##_name_b{};                                                       \
    std::optional<int> P##_count_c{};                                                              \
    std::vector<std::string> P##_list_d{};                                                         \
    bool P##_flag_e{};                                                                             \
    std::optional<std::string> P##_path_f{};                                                       \
    std::optional<unsigned> P##_size_g{};                                                          \
    std::optional<double> P##_ratio_h{};                                                           \
    bool P##_flag_i{};                                                                             \
    std::optional<std::string> P##_text_j{};

// Composition, not repetition: each level suffixes its prefix so every generated
// identifier is still unique. 1/2/5/10/20 blocks of ten = 10/20/50/100/200 fields.
#define CLAPP_BENCH_B1(P) CLAPP_BENCH_TEN_FIELDS(P)
#define CLAPP_BENCH_B2(P) CLAPP_BENCH_B1(P##a) CLAPP_BENCH_B1(P##b)
#define CLAPP_BENCH_B5(P)                                                                          \
    CLAPP_BENCH_B1(P##a)                                                                           \
    CLAPP_BENCH_B1(P##b) CLAPP_BENCH_B1(P##c) CLAPP_BENCH_B1(P##d) CLAPP_BENCH_B1(P##e)
#define CLAPP_BENCH_B10(P) CLAPP_BENCH_B5(P##f) CLAPP_BENCH_B5(P##g)
#define CLAPP_BENCH_B20(P) CLAPP_BENCH_B10(P##h) CLAPP_BENCH_B10(P##i)

#if CLAPP_BENCH_FIELDS == 10
#    define CLAPP_BENCH_ROOT_FIELDS CLAPP_BENCH_B1(r)
#elif CLAPP_BENCH_FIELDS == 20
#    define CLAPP_BENCH_ROOT_FIELDS CLAPP_BENCH_B2(r)
#elif CLAPP_BENCH_FIELDS == 50
#    define CLAPP_BENCH_ROOT_FIELDS CLAPP_BENCH_B5(r)
#elif CLAPP_BENCH_FIELDS == 100
#    define CLAPP_BENCH_ROOT_FIELDS CLAPP_BENCH_B10(r)
#elif CLAPP_BENCH_FIELDS == 200
#    define CLAPP_BENCH_ROOT_FIELDS CLAPP_BENCH_B20(r)
#else
#    error "CLAPP_BENCH_FIELDS must be one of 10, 20, 50, 100, 200"
#endif

/**
 * \brief The five members every generated subcommand carries.
 * \note Five keeps each generated subcommand large enough to exercise field deduction.
 */
#define CLAPP_BENCH_FIVE_FIELDS(P)                                                                 \
    bool P##_flag_a{};                                                                             \
    std::optional<std::string> P##_name_b{};                                                       \
    std::optional<int> P##_count_c{};                                                              \
    std::vector<std::string> P##_list_d{};                                                         \
    std::optional<double> P##_ratio_e{};

/** \brief One generated subcommand type, named `sub<N>` and spelled `sub-<N>`. */
#define CLAPP_BENCH_SUBCOMMAND(N)                                                                  \
    struct[[= clapp::cmd{.name = "sub" #N, .about = "generated subcommand " #N}]] sub##N {         \
        CLAPP_BENCH_FIVE_FIELDS(s##N)                                                              \
    };

#if CLAPP_BENCH_MODE >= 2

namespace bench {
namespace {
#    if CLAPP_BENCH_SUBCOMMANDS >= 1
    CLAPP_BENCH_SUBCOMMAND(0)

    CLAPP_BENCH_SUBCOMMAND(1)

    CLAPP_BENCH_SUBCOMMAND(2)
#    endif
#    if CLAPP_BENCH_SUBCOMMANDS >= 5
    CLAPP_BENCH_SUBCOMMAND(3)

    CLAPP_BENCH_SUBCOMMAND(4)
#    endif
#    if CLAPP_BENCH_SUBCOMMANDS >= 10
    CLAPP_BENCH_SUBCOMMAND(5)
    CLAPP_BENCH_SUBCOMMAND(6)
    CLAPP_BENCH_SUBCOMMAND(7)
    CLAPP_BENCH_SUBCOMMAND(8)
    CLAPP_BENCH_SUBCOMMAND(9)
#    endif

    /**
     * \brief The generated root command.
     *
     * \note `.name` is fixed so the frozen tree's identity does not vary with the cell; the
     *       *shape* is what the matrix varies.
     */
    struct[[= clapp::cmd{
                .name = "bench",
                .version = "0.1.0",
                .about = "Generated CLI for the command_of compile-time bench"
            }]] cli {
        CLAPP_BENCH_ROOT_FIELDS

#    if CLAPP_BENCH_SUBCOMMANDS == 3
        [[maybe_unused]] [[= clapp::subcommand{}]]
        std::variant<sub0, sub1, sub2> command{};
#    elif CLAPP_BENCH_SUBCOMMANDS == 5
        [[maybe_unused]] [[= clapp::subcommand{}]]
        std::variant<sub0, sub1, sub2, sub3, sub4> command{};
#    elif CLAPP_BENCH_SUBCOMMANDS == 10
        [[maybe_unused]] [[= clapp::subcommand{}]] std::
        variant<sub0, sub1, sub2, sub3, sub4, sub5, sub6, sub7, sub8, sub9>
        command{};
#    elif CLAPP_BENCH_SUBCOMMANDS != 0
#        error "CLAPP_BENCH_SUBCOMMANDS must be one of 0, 3, 5, 10"
#    endif
    };
} // namespace
} // namespace bench

#endif  // CLAPP_BENCH_MODE >= 2

// ===========================================================================
// The thing being measured
// ===========================================================================

#if CLAPP_BENCH_MODE >= 3

namespace bench {
    /**
     * \brief The frozen command tree.
     *
     * \note `inline constexpr`, hence external linkage, hence actually emitted into the
     *       binary — which is the precondition for counting its startup fixups with
     *       `dyld_info -fixups` (ADR-0005's 「后果」 section, and `--fixups` in the driver).
     *       An internal-linkage `constexpr` that main() only reads through would be folded
     *       away and would measure nothing.
     */
    inline constexpr clapp::command_spec spec = clapp::command_of<cli>();

    // The shape is asserted rather than assumed. A macro slip that emitted ten fields at
    // every setting would make the whole matrix look flat and linear.
    //
    // +2: clapp injects `--help` and `--version` into a command that declares a version.
    static_assert(spec.get_arguments().size() == CLAPP_BENCH_FIELDS + 2);
    // +1 at any non-zero count: the injected `help` subcommand.
    static_assert(spec.get_subcommands().size() ==
                  (CLAPP_BENCH_SUBCOMMANDS == 0 ? 0 : CLAPP_BENCH_SUBCOMMANDS + 1));
} // namespace bench

#endif  // CLAPP_BENCH_MODE >= 3

// ===========================================================================
// Program
// ===========================================================================

/**
 * \brief Report the shape that was compiled, and keep the frozen tree in the image.
 *
 * \param argc `main`'s argument count, used only as a value the optimizer cannot fold.
 * \param argv Unused.
 * \return 0.
 *
 * \note The binary exists so the cell can be linked and inspected — `size`,
 *       `dyld_info -fixups`, `otool -l` for the static-initializer section. ADR-0005
 *       claims the tree costs nothing to *construct* but is explicit that it is not
 *       free of *symbols*; both halves are checkable only on a linked image.
 *
 * \warning **The walk under `argc > 1` is load-bearing, not decoration.** Printing only
 *          `get_arguments().size()` lets the optimizer fold the whole tree away: the
 *          linker then emits none of the promoted strings and `dyld_info -fixups`
 *          reports a single bind for `printf`. Measured — the first version of this file
 *          did exactly that and the "1 fixup" it produced would have read as a much
 *          stronger result than the truth. The loop below consumes data the compiler
 *          cannot prove unreachable, so the promoted strings survive into `.rodata` and
 *          their weak-def symbols get counted.
 */
int main(int argc, char **argv) {
    static_cast<void>(argv);
#if CLAPP_BENCH_MODE >= 3
    std::printf("fields=%d subcommands=%d args=%zu subs=%zu spec_bytes=%zu\n",
                CLAPP_BENCH_FIELDS,
                CLAPP_BENCH_SUBCOMMANDS,
                bench::spec.get_arguments().size(),
                bench::spec.get_subcommands().size(),
                sizeof(clapp::command_spec));

    if (argc > 1) {
        for (const clapp::arg_spec &one: bench::spec.get_arguments()) {
            const std::string_view id = one.get_id().name();
            std::printf("arg %.*s\n", static_cast<int>(id.size()), id.data());
        }
        for (const clapp::command_spec &one: bench::spec.get_subcommands()) {
            const std::string_view name = one.get_name();
            std::printf("sub %.*s\n", static_cast<int>(name.size()), name.data());
        }
    }
#elif CLAPP_BENCH_MODE == 2
    std::printf("fields=%d subcommands=%d (types only, command_of not run)\n",
                CLAPP_BENCH_FIELDS,
                CLAPP_BENCH_SUBCOMMANDS);
#else
    std::printf("headers only\n");
#endif
    return 0;
}
