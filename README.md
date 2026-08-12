# clapp

**C**ommand **L**ine **A**rgument **P**arser for **P**lus-plus.

`clapp` is a header-only C++26 command-line parser. Command structure, help text, usage lines,
validation and value conversion are derived at compile time with static reflection and
annotations. Exceptions are disabled; fallible public operations return `std::expected`.

[中文](README.zh-CN.md)

## Features

- **Derive API** — annotate structs; the command model is generated via reflection
- **Builder API** — assemble the same model with a fluent `command_builder` at compile time
- **Subcommands**, flags, options, positionals, groups, defaults, env vars and validation
- **Typed values** out of the box (`std::string`, integers, paths, optionals, vectors, …)
- **Header-only** — include `<clapp/clapp.hpp>`; no separate library link step
- **No exceptions** — errors use `std::expected` and structured `clapp::error`

## Quick start (derive)

```cpp
#include <clapp/clapp.hpp>

#include <print>
#include <string>

struct [[= clapp::cmd{.name = "demo", .version = "1.0.0"}]] args {
    [[= clapp::arg{.short_ = 'n', .long_ = "name", .help = "Name to greet"}]]
    std::string name;
};

int main(int argc, char** argv) {
    const args parsed = clapp::parse<args>(argc, argv);
    std::println("Hello {}!", parsed.name);
}
```

`clapp::parse` prints help / version / errors and exits when appropriate. Prefer
`clapp::try_parse<T>(argc, argv)` when you need a non-exiting `std::expected<T, clapp::error>`.

## Quick start (builder)

```cpp
#include <clapp/clapp.hpp>

#include <expected>
#include <print>
#include <string>

using clapp::arg_builder;
using clapp::command_builder;
using clapp::command_spec;
using clapp::raw_args;

[[nodiscard]] consteval command_spec build() {
    command_builder app("demo");
    std::move(app)
        .version("1.0.0")
        .arg(arg_builder("name")
                 .short_('n')
                 .long_("name")
                 .help("Name to greet")
                 .value_parser<std::string>());
    return app.freeze();
}

constexpr command_spec spec = build();

int main(int argc, char** argv) {
    auto got = clapp::parse(spec, raw_args(argc, argv));
    if (!got) {
        // handle or render got.error()
        return got.error().exit_code();
    }
    if (auto name = got->get_one<std::string>("name"); name) {
        std::println("Hello {}!", **name);
    }
}
```

## Requirements

- CMake 4.3 or newer
- A C++26 compiler with P2996 (reflection) and P3394 (annotations)
- GCC 16+ with `-freflection`, or a compatible `clang-p2996` build

## Build and test

Default presets use **GCC 16** (`g++-16`). Full configure + build + test:

```bash
cmake --workflow --preset dev
```

Focused commands against the `dev` tree:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset units   # unit tests only (label: units)
ctest --preset e2e     # end-to-end tests only (label: e2e)
ctest --preset dev     # full test suite for the dev build
```

### Configure / build / workflow presets

| Preset | Build type | Notes |
|--------|------------|--------|
| `debug` | Debug | Tests, examples and benchmarks; warnings not as errors |
| `dev` | RelWithDebInfo | Primary development build; tests and examples; `-Werror` |
| `release` | Release | Optimized; full test suite |
| `debug-clang` / `dev-clang` / `release-clang` | same as above | Requires `CLAPP_CLANG_P2996` pointing at a clang-p2996 toolchain |
| `asan` / `ubsan` | Debug | Address / undefined-behavior sanitizers |
| `tsan` | Debug | Thread sanitizer (**Linux only**) |
| `coverage` | Debug | GCC coverage flags |
| `bench` | RelWithDebInfo | Benchmarks only (tests and examples off); workflow is configure + build |

Each name above is a configure, build and (where applicable) workflow preset of the same name.
Workflows run configure → build → test, except `bench` (configure → build only).

## Use as a dependency

**Subproject (add_subdirectory / FetchContent):**

```cmake
add_subdirectory(path/to/clapp)
target_link_libraries(your_app PRIVATE clapp::clapp)
```

**Installed package:**

```cmake
find_package(clapp CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE clapp::clapp)
```

Include the umbrella header:

```cpp
#include <clapp/clapp.hpp>
```

## Examples

| Path | Description |
|------|-------------|
| [`examples/tutorial/derive/`](examples/tutorial/derive/) | Derive API walkthrough (quick start → validation) |
| [`examples/tutorial/builder/`](examples/tutorial/builder/) | Same topics with the builder API |
| [`examples/scenarios/`](examples/scenarios/) | Full apps: demo, git, pacman, find, repl, typed, … |
| [`examples/meta/reflection_probe_example.cpp`](examples/meta/reflection_probe_example.cpp) | Reflection capability probe |

## Repository layout

```
include/clapp/   Public header-only library
examples/        Tutorials and scenario apps
tests/units/     Unit tests (mirror public modules)
tests/e2e/       End-to-end checks against example output
tests/install/   Installed and subproject consumer gates
benches/         Optional performance measurements
cmake/           Toolchain helpers and package config
```

## License

This project is licensed under the [MIT License](LICENSE).

```
MIT License

Copyright (c) 2026 Nathan Shea <itzrustz@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

