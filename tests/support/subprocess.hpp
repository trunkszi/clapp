#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if __has_include(<poll.h>) && __has_include(<unistd.h>) && __has_include(<sys/wait.h>)
#    define CLAPP_E2E_HAS_SUBPROCESS 1
#    include <poll.h>
#    include <sys/wait.h>
#    include <unistd.h>
#else
#    define CLAPP_E2E_HAS_SUBPROCESS 0
#endif

namespace clapp::test {
    /** \brief What one child process did. */
    struct run_result {
        /**
         * \brief The exit status, or -1 when the child was killed by a signal or could not
         *        be started. Never confuse it with a status of 255.
         */
        int status = -1;

        /** \brief Everything the child wrote to file descriptor 1. */
        std::string out;

        /** \brief Everything the child wrote to file descriptor 2. */
        std::string err;

        /** \brief Whether the child exited normally (as opposed to dying on a signal). */
        [[nodiscard]] bool exited() const noexcept { return status >= 0; }

        /** \brief Whether \p needle appears anywhere in stdout. */
        [[nodiscard]] bool out_has(std::string_view needle) const noexcept {
            return out.find(needle) != std::string::npos;
        }

        /** \brief Whether \p needle appears anywhere in stderr. */
        [[nodiscard]] bool err_has(std::string_view needle) const noexcept {
            return err.find(needle) != std::string::npos;
        }
    };

    /**
     * \brief Path of the example executable this test drives.
     *
     * \return The value CMake put in `CLAPP_E2E_BINARY`, or an empty view when the test was
     *         compiled without `DRIVES`.
     *
     * \note Defined even when the macro is absent so that a driver compiled by hand — which
     *       is how these files are checked before the Integrate stage wires them up — still
     *       builds and reports honestly rather than failing to compile.
     */
    [[nodiscard]] inline std::string_view binary_path() noexcept {
#ifdef CLAPP_E2E_BINARY
        return CLAPP_E2E_BINARY;
#else
        return {};
#endif
    }

    /**
     * \brief Path of the *other* executable this test compares against, when it has one.
     *
     * \return The value CMake put in `CLAPP_E2E_PEER_BINARY`, or an empty view when the test
     *         was compiled without a peer.
     *
     * \note Exists for the tutorial pairs, where `examples/tutorial/builder/<step>.cpp` and
     *       `examples/tutorial/derive/<step>.cpp` are two spellings of one CLI whose whole
     *       promise is that they behave identically. Asserting that promise by writing the
     *       same expected bytes into both drivers proves only that someone typed them twice;
     *       running both binaries and comparing proves it, and keeps proving it after
     *       somebody edits one of the two examples. `clapp_add_tutorial_pair()` in
     *       tests/CMakeLists.txt is what sets the macro.
     */
    [[nodiscard]] inline std::string_view peer_binary_path() noexcept {
#ifdef CLAPP_E2E_PEER_BINARY
        return CLAPP_E2E_PEER_BINARY;
#else
        return {};
#endif
    }

    /**
     * \brief Whether peer_binary_path() names anything.
     * \return `true` when this test was compiled with a peer executable.
     */
    [[nodiscard]] inline bool has_peer() noexcept { return !peer_binary_path().empty(); }

#if CLAPP_E2E_HAS_SUBPROCESS

    namespace detail {
        /**
         * \brief Read both descriptors until each reports end of file.
         *
         * \param out_fd Read end of the child's stdout pipe; closed on return.
         * \param err_fd Read end of the child's stderr pipe; closed on return.
         * \param out Receives everything read from \p out_fd.
         * \param err Receives everything read from \p err_fd.
         *
         * \note Concurrent by construction. See the file header for why sequential draining is
         *       a deadlock rather than a slower equivalent.
         */
        inline void drain_both(int out_fd, int err_fd, std::string &out, std::string &err) {
            ::pollfd fds[2] = {};
            fds[0].fd = out_fd;
            fds[0].events = POLLIN;
            fds[1].fd = err_fd;
            fds[1].events = POLLIN;
            std::string *sink[2] = {&out, &err};

            int open_count = 2;
            while (open_count > 0) {
                if (::poll(fds, 2, -1) < 0) break;
                for (int i = 0; i < 2; ++i) {
                    if (fds[i].fd < 0) continue;
                    if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;

                    char buffer[4096];
                    const ::ssize_t got = ::read(fds[i].fd, buffer, sizeof buffer);
                    if (got > 0) {
                        sink[i]->append(buffer, static_cast<std::size_t>(got));
                    } else {
                        ::close(fds[i].fd);
                        fds[i].fd = -1;
                        --open_count;
                    }
                }
            }
            for (::pollfd &p: fds) {
                if (p.fd >= 0) ::close(p.fd);
            }
        }
    } // namespace detail

    /**
     * \brief Run the example this test drives, with \p args after `argv[0]`.
     *
     * \param args The command line *without* the program name; `argv[0]` is
     *        binary_path(), which is what a real invocation looks like and what
     *        clapp::parse() expects to skip.
     * \param stdin_text Fed to the child's stdin, which is then closed. Pass `{}` — the
     *        default — for a child that reads nothing; the descriptor is still a closed
     *        pipe rather than the parent's terminal, so a child that *does* read sees EOF
     *        immediately instead of hanging the suite.
     * \param argv0 What the child sees as `argv[0]`. Empty means the executed file, which is
     *        the ordinary case. A **multicall** program
     *        (`[[= clapp::cmd{.multicall = true}]]`) selects its applet from `argv[0]`, so
     *        testing one requires saying that argv[0] is `hostname` while still executing the
     *        file CMake built. `execv` separates the two by construction — the path it opens
     *        and `argv[0]` are different parameters — so no symlink, hard link or copy is
     *        needed.
     * \param program Which executable to run. Empty — the default — means binary_path(), the
     *        example this test drives. run_peer() passes peer_binary_path() here; nothing
     *        else should pass anything.
     * \return What the child wrote and how it exited; `run_result::status` is -1 when the
     *         child could not be started at all.
     *
     * \warning Requires that the test was compiled with `CLAPP_E2E_BINARY`. Without it the
     *          path is empty, `execv` fails, and the result is a status of -1 with both
     *          streams empty — which every caller's assertions will reject, as they should.
     *
     * \note **`COLUMNS` is removed from the environment the child inherits.** Since M5 the
     *       drivers compare whole `--help` pages byte for byte, and clapp wraps them at
     *       `clapp::detect_terminal_width()`, which falls back to `COLUMNS` when stdout is
     *       not a terminal — which it never is here, because stdout is a pipe. Leaving the
     *       variable alone would make the suite's verdict depend on the width of the
     *       terminal the developer happened to run `ctest` from: the widest line any pinned
     *       page currently holds is 90 cells (`typed builtin -h`), so an exported
     *       `COLUMNS=80` would re-flow it and fail assertions that have nothing to do with
     *       wrapping. Unsetting it pins every page at clapp::default_terminal_width (100).
     *       A driver that wants to *test* the wrapping should set `COLUMNS` itself, in the
     *       child, and none does yet.
     */
    [[nodiscard]] inline run_result run(std::vector<std::string> args,
                                        std::string_view stdin_text = {},
                                        std::string_view argv0 = {},
                                        std::string_view program_path = {}) {
        run_result result;

        // Before the fork, not after: `unsetenv` is not async-signal-safe, and the child
        // inherits `environ` as it stands at fork time. Idempotent, so calling it once per
        // run() rather than once per process costs nothing and needs no static state.
        static_cast<void>(::unsetenv("COLUMNS"));

        int in_pipe[2] = {-1, -1};
        int out_pipe[2] = {-1, -1};
        int err_pipe[2] = {-1, -1};
        if (::pipe(in_pipe) != 0) return result;
        if (::pipe(out_pipe) != 0) return result;
        if (::pipe(err_pipe) != 0) return result;

        // The child inherits this process's stdio buffers; flush before it can duplicate them
        // down the pipe. See the file header.
        static_cast<void>(std::fflush(nullptr));

        const std::string program{program_path.empty() ? binary_path() : program_path};

        // Build argv before forking: after fork() in a multi-threaded program only
        // async-signal-safe calls are legal, and every allocation here is one that is not.
        std::vector<std::string> owned;
        owned.reserve(args.size() + 1);
        owned.emplace_back(argv0.empty() ? std::string_view{program} : argv0);
        for (std::string &a: args) owned.push_back(std::move(a));

        std::vector<char *> argv;
        argv.reserve(owned.size() + 1);
        for (std::string &a: owned) argv.push_back(a.data());
        argv.push_back(nullptr);

        // The file to execute is always binary_path(); only what the child reads back as
        // argv[0] varies. Keeping `program` separate from `argv[0]` is the whole mechanism
        // behind the multicall tests.
        std::vector<char> path_bytes(program.begin(), program.end());
        path_bytes.push_back('\0');

        const ::pid_t child = ::fork();
        if (child < 0) return result;

        if (child == 0) {
            static_cast<void>(::dup2(in_pipe[0], 0));
            static_cast<void>(::dup2(out_pipe[1], 1));
            static_cast<void>(::dup2(err_pipe[1], 2));
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            static_cast<void>(::execv(path_bytes.data(), argv.data()));
            // Only reachable when execv failed; 127 is the shell's convention for it.
            ::_exit(127);
        }

        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);

        if (!stdin_text.empty()) {
            std::size_t written = 0;
            while (written < stdin_text.size()) {
                const ::ssize_t got = ::write(
                    in_pipe[1], stdin_text.data() + written, stdin_text.size() - written);
                if (got <= 0) break;
                written += static_cast<std::size_t>(got);
            }
        }
        ::close(in_pipe[1]);

        detail::drain_both(out_pipe[0], err_pipe[0], result.out, result.err);

        int raw = 0;
        static_cast<void>(::waitpid(child, &raw, 0));
        const bool exited = (raw & 0x7f) == 0;
        result.status = exited ? ((raw >> 8) & 0xff) : -1;
        return result;
    }

    /**
     * \brief run() spelled with a braced list of literals.
     *
     * \param args The command line without the program name.
     * \return As run(std::vector<std::string>, std::string_view).
     *
     * \note A separate overload rather than a default argument because
     *       `run({"--name", "Alice"})` would otherwise have to name the vector type at
     *       every call site; an e2e file has dozens of calls.
     */
    [[nodiscard]] inline run_result run(std::initializer_list<std::string_view> args) {
        std::vector<std::string> owned;
        owned.reserve(args.size());
        for (std::string_view a: args) owned.emplace_back(a);
        return run(std::move(owned));
    }

    /**
     * \brief run() with text on the child's stdin.
     * \param args The command line without the program name.
     * \param stdin_text Written to the child's stdin, which is then closed.
     * \return As run(std::vector<std::string>, std::string_view).
     */
    [[nodiscard]] inline run_result run_with_input(std::initializer_list<std::string_view> args,
                                                   std::string_view stdin_text) {
        std::vector<std::string> owned;
        owned.reserve(args.size());
        for (std::string_view a: args) owned.emplace_back(a);
        return run(std::move(owned), stdin_text);
    }

    /**
     * \brief run() with a chosen `argv[0]`, for multicall programs.
     *
     * \param argv0 What the child reads as `argv[0]`; the file executed is still
     *        binary_path().
     * \param args The rest of the command line.
     * \return As run(std::vector<std::string>, std::string_view, std::string_view).
     *
     * \note This is how a busybox-style applet is selected without installing anything:
     *       `run_as("hostname", {})` executes the built binary but tells it that it was
     *       invoked as `hostname`.
     */
    [[nodiscard]] inline run_result run_as(std::string_view argv0,
                                           std::initializer_list<std::string_view> args) {
        std::vector<std::string> owned;
        owned.reserve(args.size());
        for (std::string_view a: args) owned.emplace_back(a);
        return run(std::move(owned), {}, argv0);
    }

    /**
     * \brief run() against peer_binary_path() instead of binary_path().
     *
     * \param args The command line without the program name.
     * \return As run(std::vector<std::string>, std::string_view, std::string_view,
     *         std::string_view).
     *
     * \warning Guard every call with has_peer(). Without a peer the path is empty, `execv`
     *          fails, and the result is a status of -1 with both streams empty — which would
     *          compare *unequal* to any real run and so fail loudly rather than silently, but
     *          for the wrong reason.
     */
    [[nodiscard]] inline run_result run_peer(std::initializer_list<std::string_view> args) {
        std::vector<std::string> owned;
        owned.reserve(args.size());
        for (std::string_view a: args) owned.emplace_back(a);
        return run(std::move(owned), {}, {}, peer_binary_path());
    }

#endif  // CLAPP_E2E_HAS_SUBPROCESS
} // namespace clapp::test
