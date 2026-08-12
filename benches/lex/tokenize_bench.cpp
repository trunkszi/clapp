#include <clapp/lex/os_str.hpp>
#include <clapp/lex/parsed_arg.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/lex/short_flags.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

namespace {
    using clapp::arg_cursor;
    using clapp::os_str;
    using clapp::parsed_arg;
    using clapp::raw_args;
    using clapp::short_flags;

    // ===========================================================================
    // Command lines
    // ===========================================================================

    /**
     * \brief clap's `ripgrep.rs::startup::complex` — the one realistic mixed line.
     *
     * \note Transcribed from the Rust source exactly as benches/parser/parse_bench.cpp
     *       transcribes it, so that file's `ripgrep_complex` number can be read net of the
     *       tokenization this one reports.
     */
    inline constexpr std::string_view complex_line[] = {
        "rg",
        "pat",
        "-cFlN",
        "-pqr=some",
        "--null",
        "--no-filename",
        "--no-messages",
        "-SH",
        "-C5",
        "--follow",
        "-e some",
    };

    /** \brief Nothing but detached long options. */
    inline constexpr std::string_view long_line[] = {
        "rg",
        "--null",
        "--no-filename",
        "--no-messages",
        "--follow",
        "--hidden",
        "--sort-files",
        "--vimgrep"
    };

    /** \brief Nothing but `--key=value` long options — the `split_once('=')` path. */
    inline constexpr std::string_view long_eq_line[] = {
        "rg",
        "--color=always",
        "--context-separator=--",
        "--path-separator=/",
        "--replace=some",
        "--type-add=src:include:cpp,py,md",
        "--maxdepth=12",
        "--threads=8"
    };

    /** \brief Nothing but short clusters — the scalar-value decode path. */
    inline constexpr std::string_view short_line[] = {
        "rg", "-cFlN", "-SH", "-pqr", "-anN", "-vwiL", "-uu", "-tTj"
    };

    /** \brief Nothing that is a flag at all — the path that does the least work. */
    inline constexpr std::string_view value_line[] = {
        "rg",
        "pattern",
        "src/main.rs",
        "src/lib.rs",
        "README.md",
        "docs/",
        "tests/",
        "benches/"
    };

    /**
     * \brief The awkward ones: the escape, the stdio placeholder, a negative number, and a
     *        cluster that is not ASCII.
     *
     * \note `-é` is one flag, not two: clapp::short_flags::flag_type is `char32_t` because
     *       Rust's `char` is a Unicode scalar value and a byte would either split the flag
     *       or truncate it. That decision costs a UTF-8 decode per flag, and this line is
     *       where the cost becomes visible.
     */
    inline constexpr std::string_view awkward_line[] = {
        "rg", "--", "-", "-1.5", "-é", "--=v", "--k=", "---"
    };

    /**
     * \brief `["rg", "pat", "some", "some", …]` — clap's `xargs` fixture shape.
     * \param count How many trailing positionals.
     * \return The tokens, owned.
     */
    std::vector<std::string> xargs_line(std::size_t count) {
        std::vector<std::string> out;
        out.reserve(count + 2);
        out.emplace_back("rg");
        out.emplace_back("pat");
        for (std::size_t i = 0; i < count; ++i) out.emplace_back("some");
        return out;
    }

    /**
     * \brief The bytes of a token table, so a per-byte rate can be reported.
     * \param items The tokens.
     * \return Their total length.
     */
    benchmark::IterationCount byte_count(std::span<const std::string_view> items) {
        std::size_t total = 0;
        for (const std::string_view one: items) total += one.size();
        return static_cast<benchmark::IterationCount>(total);
    }

    // ===========================================================================
    // Text for the os_str cases
    // ===========================================================================

    /** \brief A plain ASCII path, the overwhelmingly common argument shape. */
    inline constexpr std::string_view ascii_arg = "src/parser/very/deeply/nested/module.rs";

    /** \brief Comparable length in multi-byte UTF-8, so the validator's slow path is timed. */
    inline constexpr std::string_view utf8_arg = "src/解析器/非常/深/嵌套/模块.rs";

    /**
     * \brief Bytes that are not UTF-8 at all — a Latin-1 filename, which POSIX permits and
     *        which is the entire reason clapp::os_str exists.
     *
     * \note The scan has to reach the bad byte before it can fail, so a failure is not
     *       cheaper *per byte* than a success — only shorter. Measured 2026-08 (GCC 16.1.0,
     *       `bench` preset): 17.8 ns over the 18 bytes before the `\xE9`, against 41.5 ns
     *       over all 39 of `ascii_arg`. That is 0.99 vs 1.06 ns per byte, i.e. the same
     *       scan. Read the wall-clock numbers of these three cases against their lengths,
     *       never against each other.
     */
    inline constexpr std::string_view latin1_arg = "src/parser/caf\xE9/module.rs";

    /** \brief One `--type-add` value, the argument shape clapp splits on a separator. */
    inline constexpr std::string_view separated_arg = "src:include:cpp,py,md,rs,toml,json,yaml";

    // ===========================================================================
    // Benchmarks — construction
    // ===========================================================================

    /** \brief `raw_args{std::from_range, …}` over \p N short arguments: the allocation term. */
    void construct_by_arg_count(benchmark::State &state) {
        const std::vector<std::string> tokens =
                xargs_line(static_cast<std::size_t>(state.range(0)));
        for (auto _: state) {
            raw_args line{std::from_range, tokens};
            benchmark::DoNotOptimize(line);
        }
        state.SetItemsProcessed(state.iterations() * (state.range(0) + 2));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const construct_by_arg_count_benchmark =
            benchmark::RegisterBenchmark("construct_by_arg_count", construct_by_arg_count)
                    ->Arg(1)
                    ->Arg(8)
                    ->Arg(64)
                    ->Arg(256)
                    ->Arg(1754);

    /**
     * \brief `raw_args{"rg", "pat"}` — the braced-list constructor, a separate overload with
     *        its own instantiation of `copy_items()`.
     *
     * \note This is `ripgrep_tokenize_only` from benches/parser/parse_bench.cpp, repeated
     *       here so the two files' numbers can be checked against each other. If they ever
     *       disagree by more than noise, one of the two builds is not what it claims.
     */
    void construct_two_tokens(benchmark::State &state) {
        for (auto _: state) {
            raw_args line{"rg", "pat"};
            benchmark::DoNotOptimize(line.size());
        }
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const construct_two_tokens_benchmark =
            benchmark::RegisterBenchmark("construct_two_tokens", construct_two_tokens);

    // ===========================================================================
    // Benchmarks — the walk
    // ===========================================================================

    /**
     * \brief Walk \p line, classifying every token the way a parser would.
     *
     * \param line The command line.
     * \return An accumulator, so nothing can be optimised away.
     *
     * \note The order of the predicates is clap's: escape, then stdio, then long, then
     *       short, then value. Reordering them changes the number, which is why the classes
     *       are measured separately below rather than only in aggregate.
     */
    std::size_t classify(const raw_args &line) {
        std::size_t seen = 0;
        arg_cursor cursor = line.cursor();
        while (const std::optional<parsed_arg> token = line.next(cursor)) {
            if (token->is_escape()) {
                seen += 1;
            } else if (token->is_stdio()) {
                seen += 2;
            } else if (const std::optional<parsed_arg::long_flag> named = token->to_long()) {
                seen += named->first.has_value() ? named->first->size() : 3;
                seen += named->second.has_value() ? named->second->size() : 0;
            } else if (std::optional<short_flags> cluster = token->to_short()) {
                if (cluster->is_negative_number()) {
                    seen += 4;
                } else {
                    while (const std::optional<short_flags::flag_result> flag =
                            cluster->next_flag())
                        seen += flag->has_value() ? static_cast<std::size_t>(**flag) : 5;
                }
            } else {
                seen += token->to_value_os().size();
            }
        }
        return seen;
    }

    /**
     * \brief One `classify()` case. `BENCHMARK_CAPTURE` names it `classify_line/<label>`.
     * \param state The harness state.
     * \param table The tokens; copied as a span, which is why it must name a static array.
     */
    void classify_line(benchmark::State &state, std::span<const std::string_view> table) {
        const raw_args line{std::from_range, table};
        for (auto _: state) benchmark::DoNotOptimize(classify(line));
        state.SetItemsProcessed(
                state.iterations() * static_cast<benchmark::IterationCount>(table.size()));
        state.SetBytesProcessed(state.iterations() * byte_count(table));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const classify_complex_benchmark =
            benchmark::RegisterBenchmark(
                    "classify_line/complex", classify_line,
                    std::span<const std::string_view>{complex_line});
    [[maybe_unused]] benchmark::internal::Benchmark *const classify_long_detached_benchmark =
            benchmark::RegisterBenchmark(
                    "classify_line/long_detached", classify_line,
                    std::span<const std::string_view>{long_line});
    [[maybe_unused]] benchmark::internal::Benchmark *const classify_long_attached_benchmark =
            benchmark::RegisterBenchmark(
                    "classify_line/long_attached", classify_line,
                    std::span<const std::string_view>{long_eq_line});
    [[maybe_unused]] benchmark::internal::Benchmark *const classify_short_clusters_benchmark =
            benchmark::RegisterBenchmark(
                    "classify_line/short_clusters", classify_line,
                    std::span<const std::string_view>{short_line});
    [[maybe_unused]] benchmark::internal::Benchmark *const classify_values_benchmark =
            benchmark::RegisterBenchmark(
                    "classify_line/values", classify_line,
                    std::span<const std::string_view>{value_line});
    [[maybe_unused]] benchmark::internal::Benchmark *const classify_awkward_benchmark =
            benchmark::RegisterBenchmark(
                    "classify_line/awkward", classify_line,
                    std::span<const std::string_view>{awkward_line});

    /**
     * \brief One long option, classified — the walk factored out.
     *
     * \note `next()` is `peek_os()` plus an increment, so the gap between this and the
     *       per-token figure of `classify_line/long_detached` is the bounds check and the
     *       `std::optional`. Worth knowing before anyone tries to make the cursor cheaper.
     */
    void classify_one_long(benchmark::State &state) {
        constexpr parsed_arg token{os_str{"--no-ignore-parent"}};
        for (auto _: state) {
            std::optional<parsed_arg::long_flag> named = token.to_long();
            benchmark::DoNotOptimize(named);
        }
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const classify_one_long_benchmark =
            benchmark::RegisterBenchmark("classify_one_long", classify_one_long);

    /** \brief One short cluster, expanded flag by flag. */
    void short_cluster_ascii(benchmark::State &state) {
        for (auto _: state) {
            short_flags cluster{os_str{"cFlNSHpqr"}};
            std::size_t seen = 0;
            while (const std::optional<short_flags::flag_result> flag = cluster.next_flag())
                seen += flag->has_value() ? static_cast<std::size_t>(**flag) : 0;
            benchmark::DoNotOptimize(seen);
        }
        state.SetItemsProcessed(state.iterations() * 9);
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const short_cluster_ascii_benchmark =
            benchmark::RegisterBenchmark("short_cluster_ascii", short_cluster_ascii);

    /** \brief The same, over a cluster that is not ASCII — one UTF-8 decode per flag. */
    void short_cluster_utf8(benchmark::State &state) {
        for (auto _: state) {
            short_flags cluster{os_str{"éàüñçéàüñ"}};
            std::size_t seen = 0;
            while (const std::optional<short_flags::flag_result> flag = cluster.next_flag())
                seen += flag->has_value() ? static_cast<std::size_t>(**flag) : 0;
            benchmark::DoNotOptimize(seen);
        }
        state.SetItemsProcessed(state.iterations() * 9);
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const short_cluster_utf8_benchmark =
            benchmark::RegisterBenchmark("short_cluster_utf8", short_cluster_utf8);

    // ===========================================================================
    // Benchmarks — the primitives
    // ===========================================================================

    /**
     * \brief `os_str::to_string_view()` on ASCII — the validation every long option's name
     *        and every text value pays.
     */
    void os_str_validate_ascii(benchmark::State &state) {
        constexpr os_str subject{ascii_arg};
        for (auto _: state) {
            std::expected<std::string_view, clapp::invalid_encoding> text =
                    subject.to_string_view();
            benchmark::DoNotOptimize(text);
        }
        state.SetBytesProcessed(
                state.iterations() * static_cast<benchmark::IterationCount>(ascii_arg.size()));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const os_str_validate_ascii_benchmark =
            benchmark::RegisterBenchmark("os_str_validate_ascii", os_str_validate_ascii);

    /** \brief The same on multi-byte UTF-8 — the scanner's slow path. */
    void os_str_validate_utf8(benchmark::State &state) {
        constexpr os_str subject{utf8_arg};
        for (auto _: state) {
            std::expected<std::string_view, clapp::invalid_encoding> text =
                    subject.to_string_view();
            benchmark::DoNotOptimize(text);
        }
        state.SetBytesProcessed(
                state.iterations() * static_cast<benchmark::IterationCount>(utf8_arg.size()));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const os_str_validate_utf8_benchmark =
            benchmark::RegisterBenchmark("os_str_validate_utf8", os_str_validate_utf8);

    /** \brief The same on bytes that are not UTF-8: failure is not the cheap case. */
    void os_str_validate_invalid(benchmark::State &state) {
        constexpr os_str subject{latin1_arg};
        for (auto _: state) {
            std::expected<std::string_view, clapp::invalid_encoding> text =
                    subject.to_string_view();
            benchmark::DoNotOptimize(text);
        }
        state.SetBytesProcessed(
                state.iterations() * static_cast<benchmark::IterationCount>(latin1_arg.size()));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const os_str_validate_invalid_benchmark =
            benchmark::RegisterBenchmark("os_str_validate_invalid", os_str_validate_invalid);

    /**
     * \brief `to_string_lossy()` — the diagnostic path, which allocates.
     *
     * \note Only ever reached once something has already gone wrong, so it is here as a
     *       ceiling rather than as a hot path. The number exists so that nobody
     *       "helpfully" routes the success path through it.
     */
    void os_str_to_string_lossy(benchmark::State &state) {
        constexpr os_str subject{latin1_arg};
        for (auto _: state) {
            std::string text = subject.to_string_lossy();
            benchmark::DoNotOptimize(text);
        }
        state.SetBytesProcessed(
                state.iterations() * static_cast<benchmark::IterationCount>(latin1_arg.size()));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const os_str_to_string_lossy_benchmark =
            benchmark::RegisterBenchmark("os_str_to_string_lossy", os_str_to_string_lossy);

    /** \brief `os_str::split()` — the lazy view, driven to exhaustion. */
    void os_str_split(benchmark::State &state) {
        constexpr os_str subject{separated_arg};
        for (auto _: state) {
            std::size_t pieces = 0;
            for (os_str piece: subject.split(os_str{","})) {
                benchmark::DoNotOptimize(piece);
                ++pieces;
            }
            benchmark::DoNotOptimize(pieces);
        }
        state.SetBytesProcessed(state.iterations() *
                                static_cast<benchmark::IterationCount>(separated_arg.size()));
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const os_str_split_benchmark =
            benchmark::RegisterBenchmark("os_str_split", os_str_split);

    /** \brief `os_str::split_once('=')` — what `to_long()` does to find an attached value. */
    void os_str_split_once(benchmark::State &state) {
        constexpr os_str subject{"type-add=src:include:cpp,py,md"};
        for (auto _: state) {
            std::optional<std::pair<os_str, os_str> > halves = subject.split_once('=');
            benchmark::DoNotOptimize(halves);
        }
    }

    [[maybe_unused]] benchmark::internal::Benchmark *const os_str_split_once_benchmark =
            benchmark::RegisterBenchmark("os_str_split_once", os_str_split_once);
} // namespace

BENCHMARK_MAIN();
