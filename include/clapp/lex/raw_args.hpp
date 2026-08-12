/**
 * \file
 * \brief The owned argument list and its rewindable position: clapp::raw_args,
 *        clapp::arg_cursor, clapp::seek_from.
 */

#pragma once

#include <clapp/lex/os_str.hpp>
#include <clapp/lex/parsed_arg.hpp>

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
// `<fcntl.h>` and `<unistd.h>` rather than `<fstream>` and `<sstream>`, for the
// same reason the Win32 symbols below are hand-declared: detail::read_all_bytes()
// has one production caller (from_args() on Linux), most programs use
// `raw_args(argc, argv)` instead, and the iostreams headers would instantiate
// their whole template tower in every translation unit that includes any clapp
// header. These three are thin syscall declarations by comparison.
#    include <cerrno>
#    include <fcntl.h>
#    include <unistd.h>
#endif

#if defined(__APPLE__)
#    include <crt_externs.h>
#endif

#if defined(_WIN32)
// Declared here rather than by including <windows.h> and <shellapi.h>: those
// headers define `min`/`max` (and ~1500 other macros) into every translation
// unit that includes any clapp header, which is a heavy and surprising cost for
// three symbols. The signatures below are the ones in the SDK — `LPWSTR` is
// `wchar_t*`, `LPCWSTR` is `const wchar_t*`, `HLOCAL` is `void*` — so a
// translation unit that includes the real headers as well still sees a single
// consistent declaration.
//
// CommandLineToArgvW lives in shell32; MSVC is told here, other Windows
// toolchains need `-lshell32`.
extern "C" {
__declspec(dllimport) wchar_t* __stdcall GetCommandLineW();
__declspec(dllimport) wchar_t** __stdcall CommandLineToArgvW(const wchar_t* command_line,
                                                             int* count);
__declspec(dllimport) void* __stdcall LocalFree(void* memory);
}
#    if defined(_MSC_VER)
#        pragma comment(lib, "shell32.lib")
#    endif
#endif

namespace clapp {

    namespace detail {

        /**
         * \brief `value + 1`, saturating at max (clap_lex cursor advance).
         * \note Saturation is observable — see raw_args::next_os().
         */
        [[nodiscard]] constexpr std::size_t saturating_increment(std::size_t value) noexcept {
            return value == std::numeric_limits<std::size_t>::max() ? value : value + 1;
        }

        /**
         * \brief `base + offset` clamped to `[0, SIZE_MAX]`.
         * \param base Origin.
         * \param offset Signed displacement.
         * \return Saturated position.
         */
        [[nodiscard]] constexpr std::size_t saturating_offset(std::size_t base,
                                                              std::ptrdiff_t offset) noexcept {
            constexpr std::size_t limit = std::numeric_limits<std::size_t>::max();
            if (offset >= 0) {
                const std::size_t delta = static_cast<std::size_t>(offset);
                return base > limit - delta ? limit : base + delta;
            }
            // `-offset` overflows for PTRDIFF_MIN, so fold the sign through the
            // predecessor instead: `-(offset + 1)` is always representable.
            const std::size_t delta = static_cast<std::size_t>(-(offset + 1)) + std::size_t{1};
            return delta > base ? std::size_t{0} : base - delta;
        }

        /**
         * \brief Named `os_string -> os_str` for remaining_view's transform type.
         */
        struct borrow_os_string {
            /** \brief Borrow \p item as a non-owning view. */
            [[nodiscard]] constexpr os_str operator()(const os_string& item) const noexcept {
                return item.view();
            }
        };

        /**
         * \brief Split a NUL-separated blob (`/proc/self/cmdline` form).
         * \param blob Raw bytes; trailing terminator optional.
         * \return One os_string per argument (no extra empty for final NUL).
         * \note Exposed for testing (from_args() Linux path).
         */
        [[nodiscard]] constexpr std::vector<os_string> split_nul_separated(std::string_view blob) {
            std::vector<os_string> items;
            std::size_t begin = 0;
            while (begin < blob.size()) {
                const std::size_t found = blob.find('\0', begin);
                const std::size_t stop  = found == std::string_view::npos ? blob.size() : found;
                items.emplace_back(std::string(blob.substr(begin, stop - begin)));
                begin = stop + 1;
            }
            return items;
        }

#if !defined(_WIN32)
        /**
         * \brief Read all of \p path into a string (`read(2)`, no iostreams).
         * \param path NUL-terminated path.
         * \return Bytes read; empty on open failure or empty file (both "no cmdline").
         * \note For `/proc/self/cmdline` (size 0); exposed for testing.
         */
        [[nodiscard]] inline std::string read_all_bytes(const char* path) {
            const int descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
            if (descriptor < 0) return {};

            std::string blob;
            char buffer[4096];
            // Raw loop: `read(2)` reports "how much fitted this time", so the step is
            // decided by the kernel and the loop has two distinct exits (end of file
            // and error). No range abstracts a file descriptor here.
            for (;;) {
                const ::ssize_t got = ::read(descriptor, buffer, sizeof buffer);
                if (got > 0) {
                    blob.append(buffer, static_cast<std::size_t>(got));
                    continue;
                }
                // A signal can interrupt a read that has produced nothing yet; that is
                // not a failure, and dropping the rest of the command line over it
                // would be a rare, untraceable bug.
                if (got < 0 && errno == EINTR) continue;
                break;
            }
            ::close(descriptor);
            return blob;
        }
#endif

    }  // namespace detail

    /** \brief Origin for a seek_from offset. */
    enum class seek_origin : unsigned char {
        /** Absolute index from the front. */
        start,
        /** Relative to one-past-last (`-1` = last). */
        end,
        /** Relative to current cursor. */
        current,
    };

    /**
     * \brief Cursor seek target (Rust `SeekFrom`); factories preferred at call sites.
     * \note raw_args::seek() clamps every result into `[0, size()]`.
     */
    struct seek_from {
        seek_origin origin = seek_origin::start;
        std::ptrdiff_t offset = 0;

        /**
         * \brief Absolute index from the front (clamped by seek).
         * \note Stored signed; saturates at `PTRDIFF_MAX` (unobservable in practice).
         */
        [[nodiscard]] static constexpr seek_from start(std::size_t index) noexcept {
            constexpr std::size_t limit =
                    static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
            return seek_from{.origin = seek_origin::start,
                             .offset = static_cast<std::ptrdiff_t>(index < limit ? index : limit)};
        }

        /** \brief Relative to end; `0` = end, `-1` = last. */
        [[nodiscard]] static constexpr seek_from end(std::ptrdiff_t delta) noexcept {
            return seek_from{.origin = seek_origin::end, .offset = delta};
        }

        /** \brief Relative to current; `-1` undoes one next(). */
        [[nodiscard]] static constexpr seek_from current(std::ptrdiff_t delta) noexcept {
            return seek_from{.origin = seek_origin::current, .offset = delta};
        }

        /** \brief Member-wise equality. */
        [[nodiscard]] constexpr bool operator==(const seek_from&) const noexcept = default;
    };

    /**
     * \brief Savable position in a raw_args (index value, not an iterator).
     * \note Untied to any particular list; use with another is well-defined but wrong.
     * \code
     *     const arg_cursor saved = position;
     *     // ... speculative parse ...
     *     position = saved;  // rewind
     * \endcode
     */
    class arg_cursor {
    public:
        /** \brief Front of the list (same as raw_args::cursor()). */
        constexpr arg_cursor() noexcept = default;

        /**
         * \brief Cursor at \p index (may be past end; does not clamp).
         * \note Prefer raw_args::seek() when clamping is wanted.
         */
        constexpr explicit arg_cursor(std::size_t index) noexcept : index_(index) {}

        /**
         * \brief Zero-based index (may exceed size(); see next_os()).
         */
        [[nodiscard]] constexpr std::size_t index() const noexcept { return index_; }

        /** \brief Positional equality. */
        [[nodiscard]] constexpr bool operator==(const arg_cursor&) const noexcept = default;

        /** \brief Positional ordering. */
        [[nodiscard]] constexpr std::strong_ordering
        operator<=>(const arg_cursor&) const noexcept = default;

    private:
        std::size_t index_ = 0;
    };

    /**
     * \brief Owned command line and parser walk operations (`clap_lex::RawArgs`).
     * \note argv copied once at construction; reads return borrowed os_str/parsed_arg.
     * \warning Views (os_str, parsed_arg, short_flags) point into storage. They dangle
     *          on destroy/move/assign and on insert() reallocation. Nothing diagnoses it.
     */
    class raw_args {
        /**
         * \brief Copy a range of os_string-constructible items into storage.
         * \tparam R Source range.
         * \param items Arguments in order.
         * \return Fresh owning vector.
         * \warning Load-bearing: (1) no raw_args constructor may delegate to another;
         *          (2) this must stay **above** every constructor that calls it.
         *          clang does not instantiate a member template used from a
         *          mem-initializer if the definition appears later (consteval dies
         *          with "undefined constructor"). GCC accepts either order. Do not
         *          move this or reintroduce delegating constructors.
         */
        template<std::ranges::input_range R>
            requires std::constructible_from<os_string, std::ranges::range_reference_t<R>>
        [[nodiscard]] static constexpr std::vector<os_string> copy_items(R&& items) {
            return std::views::transform(std::forward<R>(items),
                                         []<class T>(T&& item) -> os_string {
                                             return os_string(std::forward<T>(item));
                                         }) |
                   std::ranges::to<std::vector<os_string>>();
        }

    public:
        /** \brief How an argument is stored. */
        using value_type = os_string;

        /** \brief Count and index type. */
        using size_type = std::size_t;

        /**
         * \brief Lazy range of os_str from remaining() / peek_remaining().
         * \warning Borrowed; invalidated by insert() and by destroying the raw_args.
         */
        using remaining_view =
                std::ranges::transform_view<std::span<const os_string>, detail::borrow_os_string>;

        /** \brief Empty command line. */
        constexpr raw_args() = default;

        /**
         * \brief From braced list (non-explicit; bytes copied).
         * \param items Arguments in order (include argv[0] if wanted).
         * \note Uses copy_items(); see its \warning (no delegating ctors).
         */
        constexpr raw_args(std::initializer_list<os_str> items) : items_(copy_items(items)) {}

        /**
         * \brief From a span of borrowed arguments (copied).
         */
        constexpr explicit raw_args(std::span<const os_str> items) : items_(copy_items(items)) {}

        /**
         * \brief From any range of os_string-constructible elements.
         * \tparam R Source range.
         * \param items Arguments in order.
         * \note Tagged `std::from_range` to avoid competing with the braced ctor.
         */
        template<std::ranges::input_range R>
            requires std::constructible_from<os_string, std::ranges::range_reference_t<R>>
        constexpr raw_args(std::from_range_t, R&& items)
            : items_(copy_items(std::forward<R>(items))) {}

        /**
         * \brief From `main`'s parameters.
         * \param argc Count; `<= 0` → empty.
         * \param argv Vector; null → empty; null element → empty argument (no UB).
         * \note POSIX takes bytes verbatim (including non-UTF-8).
         * \warning On Windows `native_char` is `wchar_t` — binds to `wmain`, not
         *          narrow `main`. Narrow-main Windows programs must use from_args()
         *          (ANSI argv has already lost unrepresentable characters).
         */
        constexpr raw_args(int argc, const native_char* const* argv) {
            if (argc <= 0 || argv == nullptr) return;
            const std::span<const native_char* const> raw{argv, static_cast<std::size_t>(argc)};
            items_ = raw | std::views::transform([](const native_char* item) -> os_string {
                         return item == nullptr ? os_string{}
                                                : os_string::from_native(native_string_view{item});
                     }) |
                     std::ranges::to<std::vector<os_string>>();
        }

        /**
         * \brief Command line of the running process (argv[0] first).
         * \return Arguments; empty on unsupported platforms.
         * \note macOS: `_NSGetArgc`/`_NSGetArgv` (verified). Linux: `/proc/self/cmdline`
         *       (helpers unit-tested). Windows: `GetCommandLineW`/`CommandLineToArgvW`
         *       (never compiled here; UTF-16→WTF-8 only here, ADR-0003).
         * \warning Prefer `raw_args(argc, argv)` when available. On Windows this
         *          re-parses via `CommandLineToArgvW`, whose quoting can differ from CRT.
         */
        [[nodiscard]] static raw_args from_args() {
#if defined(_WIN32)
            int count             = 0;
            wchar_t** const parts = ::CommandLineToArgvW(::GetCommandLineW(), &count);
            if (parts == nullptr || count <= 0) return raw_args{};
            raw_args result{count, parts};
            ::LocalFree(parts);
            return result;
#elif defined(__APPLE__)
            const int* const argc = ::_NSGetArgc();
            char*** const argv    = ::_NSGetArgv();
            if (argc == nullptr || argv == nullptr || *argv == nullptr) return raw_args{};
            return raw_args(*argc, *argv);
#elif defined(__linux__)
            const std::string blob = detail::read_all_bytes("/proc/self/cmdline");
            return raw_args{std::from_range, detail::split_nul_separated(blob)};
#else
            return raw_args{};
#endif
        }

        /**
         * \name Reading
         * \{
         */

        /** \brief Fresh cursor at the first argument (index 0). */
        [[nodiscard]] static constexpr arg_cursor cursor() noexcept { return arg_cursor{}; }

        /**
         * \brief Argument at \p position without advancing.
         * \return Bytes, or `std::nullopt` at/past end.
         */
        [[nodiscard]] constexpr std::optional<os_str>
        peek_os(const arg_cursor& position) const noexcept {
            if (position.index() >= items_.size()) return std::nullopt;
            return items_[position.index()].view();
        }

        /**
         * \brief Classified argument at \p position without advancing.
         * \return parsed_arg, or `std::nullopt` at/past end.
         */
        [[nodiscard]] constexpr std::optional<parsed_arg>
        peek(const arg_cursor& position) const noexcept {
            const std::optional<os_str> item = peek_os(position);
            if (!item.has_value()) return std::nullopt;
            return parsed_arg{*item};
        }

        /**
         * \brief Read at \p position and step past it (even when empty).
         * \return Bytes, or `std::nullopt` at/past end. Not `[[nodiscard]]` (skip ok).
         * \warning Cursor advances past end too (clap_lex). After two overshoots,
         *          `seek(..., current(-1))` lands on end, not the last arg.
         *          remaining()/insert() clamp where clap_lex panics.
         */
        constexpr std::optional<os_str> next_os(arg_cursor& position) const noexcept {
            const std::optional<os_str> item = peek_os(position);
            position = arg_cursor{detail::saturating_increment(position.index())};
            return item;
        }

        /**
         * \brief Classify at \p position and step past it.
         * \note Not `[[nodiscard]]`; see next_os() for past-end cursor.
         */
        constexpr std::optional<parsed_arg> next(arg_cursor& position) const noexcept {
            const std::optional<os_str> item = next_os(position);
            if (!item.has_value()) return std::nullopt;
            return parsed_arg{*item};
        }

        /**
         * \brief Tail from \p position to end; leaves cursor at end (`--` handler).
         * \return Lazy os_str range; empty if already at/past end.
         * \note Clamps overshot cursors (clap_lex panics).
         */
        [[nodiscard]] constexpr remaining_view remaining(arg_cursor& position) const noexcept {
            const remaining_view rest = peek_remaining(position);
            position                  = arg_cursor{items_.size()};
            return rest;
        }

        /**
         * \brief Tail from \p position without moving the cursor.
         * \return Lazy os_str range (for diagnostics; no clap_lex counterpart).
         */
        [[nodiscard]] constexpr remaining_view
        peek_remaining(const arg_cursor& position) const noexcept {
            const std::size_t from = std::min(position.index(), items_.size());
            return remaining_view{std::span<const os_string>{items_}.subspan(from),
                                  detail::borrow_os_string{}};
        }

        /** \brief Whether \p position is at or past the end. */
        [[nodiscard]] constexpr bool is_end(const arg_cursor& position) const noexcept {
            return position.index() >= items_.size();
        }

        /** \} */

        /**
         * \name Moving and rewriting
         * \{
         */

        /**
         * \brief Move \p position to \p target (clamped into `[0, size()]`).
         * \note `current(-1)` rewinds from the raw index, which may be past end.
         */
        constexpr void seek(arg_cursor& position, seek_from target) const noexcept {
            const std::size_t size = items_.size();
            std::size_t resolved   = 0;
            switch (target.origin) {
            case seek_origin::start:
                resolved = detail::saturating_offset(0, target.offset);
                break;
            case seek_origin::end:
                resolved = detail::saturating_offset(size, target.offset);
                break;
            case seek_origin::current:
                resolved = detail::saturating_offset(position.index(), target.offset);
                break;
            }
            position = arg_cursor{std::min(resolved, size)};
        }

        /**
         * \brief Splice \p items before \p position (multicall / response file; copied).
         * \param position Clamp to size(); inserted args become "next".
         * \param items Arguments to insert.
         * \note Cursors are indices — not updated; other cursors silently shift meaning.
         *       Overshot cursor clamps/appends (clap_lex panics).
         * \warning **Invalidates every outstanding view** (realloc). Re-read after insert.
         */
        constexpr void insert(const arg_cursor& position, std::span<const os_str> items) {
            const std::size_t at = std::min(position.index(), items_.size());
            items_.insert_range(
                    items_.begin() + static_cast<std::vector<os_string>::difference_type>(at),
                    items | std::views::transform([](os_str item) { return os_string{item}; }));
        }

        /**
         * \brief Splice a braced list before \p position.
         * \warning Invalidates every outstanding view; see the span overload.
         */
        constexpr void insert(const arg_cursor& position, std::initializer_list<os_str> items) {
            insert(position, std::span<const os_str>{items.begin(), items.size()});
        }

        /** \} */

        /**
         * \name Whole-list access
         * \{
         */

        /**
         * \brief All arguments in order (independent of cursors).
         */
        [[nodiscard]] constexpr std::span<const os_string> items() const noexcept {
            return std::span<const os_string>{items_};
        }

        /** \brief Argument count. */
        [[nodiscard]] constexpr size_type size() const noexcept { return items_.size(); }

        /** \brief Whether the list is empty (not is_end()). */
        [[nodiscard]] constexpr bool empty() const noexcept { return items_.empty(); }

        /** \} */

        /** \brief Byte-wise equality, argument by argument. */
        [[nodiscard]] constexpr bool operator==(const raw_args& other) const noexcept {
            return std::ranges::equal(items_, other.items_, {}, &os_string::view, &os_string::view);
        }

    private:
        std::vector<os_string> items_;
    };

}  // namespace clapp
