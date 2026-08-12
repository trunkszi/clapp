/**
 * \file
 * \brief clapp::any_id and clapp::any_value — RTTI-free type erasure for parsed
 *        argument values.
 */

#pragma once

#include <clapp/detail/std_meta.hpp>
#include <clapp/util/str.hpp>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace clapp {
    namespace detail {
        consteval void write_type_name(std::string &out, std::meta::info item);

        /**
         * \brief Write the `a::b::` scope prefix for \p entity.
         * \note Walks namespaces and enclosing classes. Unnamed scope is always
         *       `(anonymous namespace)` (not display_string_of's spelling).
         */
        consteval void write_scope_prefix(std::string &out, std::meta::info entity) {
            const std::meta::info scope = std::meta::parent_of(entity);
            if (scope == ^^::) return;
            if (std::meta::is_type(scope)) {
                write_type_name(out, scope);
                append_bytes(out, "::");
                return;
            }
            write_scope_prefix(out, scope);
            if (std::meta::has_identifier(scope))
                append_bytes(out, std::meta::identifier_of(scope));
            else
                append_bytes(out, "(anonymous namespace)");
            append_bytes(out, "::");
        }

        /** \brief Append \p value in decimal (consteval-safe). */
        consteval void write_decimal(std::string &out, std::size_t value) {
            if (value == 0) {
                out.push_back('0');
                return;
            }
            char digits[20]{};
            std::size_t count = 0;
            for (std::size_t rest = value; rest != 0; rest /= 10)
                digits[count++] = static_cast<char>('0' + static_cast<int>(rest % 10));
            while (count != 0) out.push_back(digits[--count]);
        }

        /**
         * \brief Fully-qualified spelling of type \p item (injective identity name).
         * \note East `const` on pointers (`int* const` vs `const int*`). Specializations
         *       include all template arguments (defaults included).
         */
        consteval void write_type_name(std::string &out, std::meta::info item) {
            const std::meta::info type = std::meta::dealias(item);

            if (std::meta::is_reference_type(type)) {
                write_type_name(out, std::meta::remove_reference(type));
                append_bytes(out, std::meta::is_rvalue_reference_type(type) ? "&&" : "&");
                return;
            }
            if (std::meta::is_const_type(type) || std::meta::is_volatile_type(type)) {
                const bool constness = std::meta::is_const_type(type);
                const std::meta::info bare = constness
                                                 ? std::meta::remove_const(type)
                                                 : std::meta::remove_volatile(type);
                const std::string_view keyword = constness ? "const" : "volatile";
                if (std::meta::is_pointer_type(bare) || std::meta::is_member_pointer_type(bare)) {
                    write_type_name(out, bare);
                    out.push_back(' ');
                    append_bytes(out, keyword);
                } else {
                    append_bytes(out, keyword);
                    out.push_back(' ');
                    write_type_name(out, bare);
                }
                return;
            }
            if (std::meta::is_array_type(type)) {
                write_type_name(out, std::meta::remove_extent(type));
                out.push_back('[');
                if (const std::size_t bound = std::meta::extent(type); bound != 0)
                    write_decimal(out, bound);
                out.push_back(']');
                return;
            }
            if (std::meta::is_pointer_type(type)) {
                const std::meta::info pointee = std::meta::remove_pointer(type);
                // `void (*)()` and `int (*)[3]` cannot be spelled by appending `*`.
                if (std::meta::is_function_type(pointee) || std::meta::is_array_type(pointee)) {
                    append_bytes(out, std::meta::display_string_of(type));
                } else {
                    write_type_name(out, pointee);
                    out.push_back('*');
                }
                return;
            }
            if (std::meta::has_template_arguments(type)) {
                const std::meta::info tmpl = std::meta::template_of(type);
                write_scope_prefix(out, tmpl);
                append_bytes(out, std::meta::identifier_of(tmpl));
                out.push_back('<');
                bool first = true;
                for (const std::meta::info argument: std::meta::template_arguments_of(type)) {
                    if (!first) append_bytes(out, ", ");
                    first = false;
                    if (std::meta::is_type(argument))
                        write_type_name(out, argument);
                    else
                        append_bytes(out, std::meta::display_string_of(argument));
                }
                out.push_back('>');
                return;
            }
            if (std::meta::has_identifier(type)) {
                write_scope_prefix(out, type);
                append_bytes(out, std::meta::identifier_of(type));
                return;
            }
            // Fundamental and function types: no identifier and no scope to qualify
            // with, so there is nothing display_string_of could ambiguate.
            append_bytes(out, std::meta::display_string_of(type));
        }

        /**
         * \brief Fully-qualified type spelling in static storage.
         * \tparam T Type to name.
         * \return View into static storage.
         * \warning **Never use `std::meta::display_string_of(^^T)` for identity.** It is
         *          implementation-defined and non-unique; clang-p2996 emits unqualified
         *          names (`path` for both `std::filesystem::path` and `mylib::path`,
         *          same for `vector<n1::Mode>` vs `vector<n2::Mode>`). any_value would
         *          then return the wrong type with **no diagnostic**. GCC qualifies and
         *          hides this — a GCC-only gate cannot see it.
         * \note Built from `identifier_of` + `parent_of`. Never assert one exact spelling.
         */
        template<class T>
        consteval std::string_view type_name() {
            std::string name;
            write_type_name(name, ^^T);
            return std::string_view(std::define_static_string(name));
        }
    } // namespace detail

    /**
     * \brief RTTI-free type identity from a fully-qualified static name string.
     * \note Identity/order/diagnostics use characters of type_name(), not the
     *       `define_static_string` pointer (pointer may differ across TUs). Name-based
     *       rather than `&static_tag<T>`: printable in every build, and foldable under
     *       ubsan (pointer compares fail consteval under `-fsanitize=null`).
     * \warning Identity is the *name*. Colliding spellings make any_value hand back
     *          the wrong type with **no diagnostic** (try_get succeeds). type_name()
     *          qualifies scopes so ordinary cross-namespace collisions cannot happen;
     *          same name in unnamed namespaces of different TUs still collides — give
     *          any_value types external linkage.
     * \warning Function types still go through `display_string_of` (unqualified params
     *          on clang). Not any_storable today; decompose if that changes.
     * \note Cv/ref get distinct ids; any_value rejects them to avoid silent miss.
     */
    class any_id {
    public:
        /** \brief Empty id (no type). */
        constexpr any_id() noexcept = default;

        /**
         * \brief Id of \p T.
         * \tparam T Type to identify.
         */
        template<class T>
        [[nodiscard]] static consteval any_id of() noexcept {
            return any_id{detail::type_name<T>()};
        }

        /** \brief Whether a type is named. */
        [[nodiscard]] constexpr bool has_value() const noexcept { return !name_.empty(); }

        /**
         * \brief Fully-qualified spelling for diagnostics.
         * \return `"<none>"` when empty.
         */
        [[nodiscard]] constexpr std::string_view name() const noexcept {
            return has_value() ? name_ : std::string_view("<none>");
        }

        /** \brief Same-type equality. */
        [[nodiscard]] constexpr bool operator==(const any_id &other) const noexcept {
            return name_ == other.name_;
        }

        /** \brief Total order by name (flat_map key; consteval-safe). */
        [[nodiscard]] constexpr std::strong_ordering
        operator<=>(const any_id &other) const noexcept {
            return name_.compare(other.name_) <=> 0;
        }

    private:
        constexpr explicit any_id(std::string_view name) noexcept : name_(name) {
        }

        std::string_view name_{};
    };

    /**
     * \brief Types any_value may store (object, non-cv, non-array, copy+destruct).
     * \note Cv/ref rejected so `try_get<const int>()` on stored `int` is a hard error,
     *       not a quiet nullptr that looks like "absent".
     */
    template<class T>
    concept any_storable =
            std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> &&
            !std::is_array_v<T> && std::copy_constructible<T> && std::destructible<T>;

    namespace detail {
        /** \brief Non-virtual ops table for any_value (ADR-0004). */
        struct any_ops {
            void (*destroy)(void *) noexcept; /**< Destroy/free heap value. */
            void * (*clone)(const void *); /**< Deep-copy heap value. */
            any_id id; /**< Stored type identity. */
        };

        /**
         * \brief Ops table for \p T.
         */
        template<class T>
        inline constexpr any_ops any_ops_for{
            .destroy = +[](void *p) noexcept { delete static_cast<T *>(p); },
            .clone = +[](const void *p) -> void * { return new T(*static_cast<const T *>(p)); },
            .id = any_id::of<T>(),
        };

        /**
         * \brief Abort naming expected vs actual types (`fwrite`, no allocate).
         */
        [[noreturn]] inline void report_any_value_mismatch(any_id expected, any_id actual) {
            const auto put = [](std::string_view text) {
                (void) std::fwrite(text.data(), 1, text.size(), stderr);
            };
            put("clapp: any_value holds ");
            put(actual.name());
            put(" but was read as ");
            put(expected.name());
            put(" -- this is a bug in the calling program, not in its input.\n");
            std::abort();
        }
    } // namespace detail

    /**
     * \brief Owning, copyable type-erased storage for one parsed value.
     * \note try_get() → nullptr on mismatch; get() → print both names and abort.
     *       Not constexpr (void* cast). Deep copy per clone (no shared_ptr).
     * \warning View types (`string_view`, os_str) store the view, not the bytes —
     *          use owning types if the value must outlive argv.
     */
    class any_value {
    public:
        /** \brief An any_value holding nothing. */
        constexpr any_value() noexcept = default;

        /**
         * \brief Construct \p T in place from \p args.
         */
        template<class T, class... Args>
            requires any_storable<T> && std::constructible_from<T, Args...>
        explicit any_value(std::in_place_type_t<T> tag, Args &&... args)
            : data_(new T(std::forward<Args>(args)...)),
              ops_(std::addressof(detail::any_ops_for<T>)) {
            (void) tag;
        }

        /**
         * \brief Store a copy of \p value (type deduced).
         * \note `!same_as<any_value>` must be the **first** constraint — otherwise
         *       any_storable on any_value itself is circular (GCC: depends on itself).
         */
        template<class T, class Decayed = std::remove_cvref_t<T> >
            requires(!std::same_as<Decayed, any_value>) && any_storable<Decayed> &&
                    std::constructible_from<Decayed, T>
        explicit any_value(T &&value)
            : data_(new Decayed(std::forward<T>(value))),
              ops_(std::addressof(detail::any_ops_for<Decayed>)) {
        }

        /** \brief Deep-copy. */
        any_value(const any_value &other)
            : data_(other.ops_ == nullptr ? nullptr : other.ops_->clone(other.data_)),
              ops_(other.ops_) {
        }

        /** \brief Move; \p other left empty. */
        any_value(any_value &&other) noexcept
            : data_(std::exchange(other.data_, nullptr)),
              ops_(std::exchange(other.ops_, nullptr)) {
        }

        /** \brief Deep-copy assignment (copy-and-swap). */
        any_value &operator=(const any_value &other) {
            any_value copy(other);
            swap(copy);
            return *this;
        }

        /** \brief Move assignment; \p other left empty. */
        any_value &operator=(any_value &&other) noexcept {
            if (this != std::addressof(other)) {
                reset();
                data_ = std::exchange(other.data_, nullptr);
                ops_ = std::exchange(other.ops_, nullptr);
            }
            return *this;
        }

        ~any_value() { reset(); }

        /** \brief Destroy stored value and become empty. */
        void reset() noexcept {
            if (ops_ != nullptr) ops_->destroy(data_);
            data_ = nullptr;
            ops_ = nullptr;
        }

        /**
         * \brief Replace with a \p T built from \p args (construct before destroy).
         * \return Reference to the new value.
         */
        template<class T, class... Args>
            requires any_storable<T> && std::constructible_from<T, Args...>
        T &emplace(Args &&... args) {
            T *fresh = new T(std::forward<Args>(args)...);
            reset();
            data_ = fresh;
            ops_ = std::addressof(detail::any_ops_for<T>);
            return *fresh;
        }

        /** \brief Exchange contents. */
        void swap(any_value &other) noexcept {
            std::swap(data_, other.data_);
            std::swap(ops_, other.ops_);
        }

        /** \brief ADL swap. */
        friend void swap(any_value &a, any_value &b) noexcept { a.swap(b); }

        /** \brief Whether a value is stored. */
        [[nodiscard]] bool has_value() const noexcept { return ops_ != nullptr; }

        /**
         * \brief Identity of the stored type.
         * \return Empty any_id when empty.
         */
        [[nodiscard]] any_id type() const noexcept { return ops_ == nullptr ? any_id{} : ops_->id; }

        /**
         * \brief Whether the stored type is exactly \p T.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] bool holds() const noexcept {
            return ops_ != nullptr && ops_->id == any_id::of<T>();
        }

        /**
         * \brief Pointer to stored \p T, or nullptr (empty or wrong type).
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] const T *try_get() const noexcept {
            return holds<T>() ? static_cast<const T *>(data_) : nullptr;
        }

        /** \copydoc try_get() const */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] T *try_get() noexcept {
            return holds<T>() ? static_cast<T *>(data_) : nullptr;
        }

        /**
         * \brief Reference to stored \p T.
         * \pre has_value() and type is \p T.
         * \warning Precondition violation **terminates** after printing both type
         *          names (release and debug; matches clap). Use try_get() when
         *          mismatch is a question, not a bug.
         */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] const T &get() const {
            if (const T *p = try_get<T>()) return *p;
            detail::report_any_value_mismatch(any_id::of<T>(), type());
        }

        /** \copydoc get() const */
        template<class T>
            requires any_storable<T>
        [[nodiscard]] T &get() {
            if (T *p = try_get<T>()) return *p;
            detail::report_any_value_mismatch(any_id::of<T>(), type());
        }

        /**
         * \brief Move out \p T if held; empty this, or nullopt and leave untouched.
         */
        template<class T>
            requires any_storable<T> && std::move_constructible<T>
        [[nodiscard]] std::optional<T> take() {
            T *p = try_get<T>();
            if (p == nullptr) return std::nullopt;

            // GCC 16.1.0 false positive, scoped as tightly as it can be.
            //
            // Inlining this into libstdc++'s optional in-place constructor
            // (<optional>:229) makes GCC report `'*(int*)<unknown>' may be used
            // uninitialized` for `*p`. The pointer is null-checked one line above and
            // try_get<T>() only returns non-null when a live T occupies the storage,
            // so the read is well-defined; GCC simply cannot see through the
            // type-erased ops table to prove it.
            //
            // Not UB: asan and ubsan are both silent on this path, and clang emits
            // nothing at any -O. This single warning is what kept CLAPP_WERROR off for
            // the release / asan / ubsan presets, so it is suppressed here rather
            // than left to weaken three gates.
            //
            // Revocation: drop the pragma and rebuild release with -Werror; if it
            // stays quiet, GCC has learned to see through it.
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
            std::optional<T> out(std::move(*p));
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif
            reset();
            return out;
        }

    private:
        void *data_ = nullptr;
        const detail::any_ops *ops_ = nullptr;
    };
} // namespace clapp
