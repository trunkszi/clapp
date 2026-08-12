#include <clapp/clapp.hpp>

#include <span>

/**
 * Exercises the same headers from a second TU and returns a known sentinel.
 *
 * \return 42, provided the lexer behaved. Any other value means the shared header
 *         state differs between translation units, which is what this file exists
 *         to detect.
 */
int umbrella_second_tu_probe() {
    const clapp::os_string sole{"-v"};
    const clapp::os_str items[] = {sole};
    const clapp::raw_args raw{std::span<const clapp::os_str>{items}};

    auto cur = raw.cursor();
    const auto first = raw.next(cur);

    if (!first.has_value()) return -1;
    if (!first->is_short()) return -2;
    if (!raw.is_end(cur)) return -3;

    return 42;
}

/**
 * Frozen here from a builder chain identical to the one in `umbrella_test.cpp`.
 *
 * \note `static` (internal linkage), so the twin in the other TU raises no ODR
 *       question. Only the builder *inputs* need to match for `define_static_string` /
 *       `define_static_array` to hand both TUs the same promoted storage.
 */
static consteval clapp::command_spec make_storage_probe() {
    clapp::command_builder root("umbrella-probe");
    std::move(root)
            .about("Cross-TU storage probe")
            .version("1.0")
            .arg(clapp::arg_builder("input").long_("input").required())
            .subcommand(clapp::command_builder("child").about("Nested"));
    return root.freeze();
}

static constexpr clapp::command_spec storage_probe = make_storage_probe();

/**
 * Hands this TU's frozen tree to `umbrella_test.cpp` for pointer comparison.
 *
 * \return A reference to a `static constexpr` `command_spec` built in this TU. The
 *         object itself is distinct from the other TU's; everything it points at
 *         should not be.
 */
const clapp::command_spec &umbrella_second_tu_command_spec() { return storage_probe; }
