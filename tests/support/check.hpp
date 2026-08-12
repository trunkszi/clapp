#pragma once

#include <cstdlib>
#include <print>
#include <source_location>
#include <string_view>
#include <vector>

namespace clapp::test {
    class registry {
    public:
        struct entry {
            std::string_view name;

            void (*fn)();
        };

        static registry &instance() {
            static registry r;
            return r;
        }

        void add(entry e) { entries_.push_back(e); }

        [[nodiscard]] int run() {
            for (entry const &e: entries_) {
                e.fn();
                std::println("  ok    {}", e.name);
            }
            std::println("");
            std::println("{} passed, 0 failed", entries_.size());
            return EXIT_SUCCESS;
        }

    private:
        std::vector<entry> entries_;
    };

    struct registrar {
        registrar(std::string_view name, void (*fn)()) {
            registry::instance().add({.name = name, .fn = fn});
        }
    };

    // Accepts any expression contextually convertible to bool (bool, pointer, optional,
    // comparison result, ...). Use a concept-constrained template rather than a bool
    // parameter so the macro need not write static_cast<bool> — that would trip
    // -Wuseless-cast when the argument is already bool.
    template<class T>
        requires requires(T &&t) { static_cast<bool>(std::forward<T>(t)); }
    void require(T &&cond,
                 std::string_view expr,
                 std::source_location loc = std::source_location::current()) {
        if (static_cast<bool>(std::forward<T>(cond))) return;
        std::println("  FAIL  {}:{}  {}", loc.file_name(), loc.line(), expr);
        std::abort();
    }
} // namespace clapp::test

#define CLAPP_CHECK(...) ::clapp::test::require((__VA_ARGS__), #__VA_ARGS__)

// Three levels of indirection are required: ## suppresses macro expansion of
// arguments, so __COUNTER__ must first pass through a non-pasting macro
// (CLAPP_TEST_EXPAND) to be evaluated, then to the pasting layer.
// With one fewer layer every test would generate the same name clapp_test___COUNTER__.
#define CLAPP_TEST_IMPL(fn, reg, name)                                                             \
    [[maybe_unused]] static void fn();                                                             \
    static ::clapp::test::registrar reg{name, fn};                                                 \
    static void fn()

#define CLAPP_TEST_PASTE(counter, name)                                                            \
    CLAPP_TEST_IMPL(clapp_test_##counter, clapp_reg_##counter, name)
#define CLAPP_TEST_EXPAND(counter, name) CLAPP_TEST_PASTE(counter, name)
#define CLAPP_TEST(name) CLAPP_TEST_EXPAND(__COUNTER__, name)

int main() { return ::clapp::test::registry::instance().run(); }
