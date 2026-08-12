# clapp

**C**ommand **L**ine **A**rgument **P**arser for **P**lus-plus.

`clapp` 是一个仅头文件的 C++26 命令行参数解析库。命令结构、帮助文本、用法行、校验与
值转换均在编译期通过静态反射与注解推导完成。全局禁用异常；可恢复失败以
`std::expected` 返回。

[English](README.md)

## 特性

- **Derive API** — 为结构体添加注解，通过反射生成命令模型
- **Builder API** — 用流畅的 `command_builder` 在编译期组装同一模型
- 支持**子命令**、开关、选项、位置参数、参数组、默认值、环境变量与校验
- 开箱即用的**类型化取值**（`std::string`、整数、路径、`optional`、`vector` 等）
- **仅头文件** — 包含 `<clapp/clapp.hpp>` 即可，无需单独链接库
- **无异常** — 错误通过 `std::expected` 与结构化的 `clapp::error` 表达

## 快速开始（derive）

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

`clapp::parse` 会在需要时打印帮助 / 版本 / 错误并退出。若需要不退出的结果，请使用
`clapp::try_parse<T>(argc, argv)`，返回 `std::expected<T, clapp::error>`。

## 快速开始（builder）

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
        // 处理或渲染 got.error()
        return got.error().exit_code();
    }
    if (auto name = got->get_one<std::string>("name"); name) {
        std::println("Hello {}!", **name);
    }
}
```

## 环境要求

- CMake 4.3 或更高
- 支持 P2996（反射）与 P3394（注解）的 C++26 编译器
- GCC 16+ 并启用 `-freflection`，或兼容的 `clang-p2996` 构建

## 构建与测试

默认预设使用 **GCC 16**（`g++-16`）。完整配置 + 构建 + 测试：

```bash
cmake --workflow --preset dev
```

针对 `dev` 树的分步命令：

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset units   # 仅单元测试（label: units）
ctest --preset e2e     # 仅端到端测试（label: e2e）
ctest --preset dev     # dev 构建的完整测试套件
```

### 配置 / 构建 / 工作流预设

| 预设 | 构建类型 | 说明 |
|------|----------|------|
| `debug` | Debug | 含测试、示例与基准；警告不当作错误 |
| `dev` | RelWithDebInfo | 主要开发构建；含测试与示例；`-Werror` |
| `release` | Release | 优化构建；完整测试套件 |
| `debug-clang` / `dev-clang` / `release-clang` | 同上 | 需设置 `CLAPP_CLANG_P2996` 指向 clang-p2996 工具链 |
| `asan` / `ubsan` | Debug | Address / UndefinedBehavior 消毒器 |
| `tsan` | Debug | Thread 消毒器（**仅 Linux**） |
| `coverage` | Debug | GCC 覆盖率插桩 |
| `bench` | RelWithDebInfo | 仅基准（关闭测试与示例）；工作流为配置 + 构建 |

上表中每个名称同时是同名的 configure、build 与（若有）workflow 预设。
工作流一般为配置 → 构建 → 测试；`bench` 仅为配置 → 构建。

## 作为依赖使用

**子工程（add_subdirectory / FetchContent）：**

```cmake
add_subdirectory(path/to/clapp)
target_link_libraries(your_app PRIVATE clapp::clapp)
```

**已安装的包：**

```cmake
find_package(clapp CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE clapp::clapp)
```

包含统一入口头文件：

```cpp
#include <clapp/clapp.hpp>
```

## 示例

| 路径 | 说明 |
|------|------|
| [`examples/tutorial/derive/`](examples/tutorial/derive/) | Derive API 教程（快速入门 → 校验） |
| [`examples/tutorial/builder/`](examples/tutorial/builder/) | 相同主题的 Builder API 版本 |
| [`examples/scenarios/`](examples/scenarios/) | 完整应用：demo、git、pacman、find、repl、typed 等 |
| [`examples/meta/reflection_probe_example.cpp`](examples/meta/reflection_probe_example.cpp) | 反射能力探测 |

## 仓库结构

```
include/clapp/   公开的仅头文件库
examples/        教程与场景示例
tests/units/     单元测试（与公开模块对应）
tests/e2e/       对照示例输出的端到端测试
tests/install/   安装包与子工程消费方门禁
benches/         可选的性能测量
cmake/           工具链辅助与包配置
```

## 许可证

本项目采用 [MIT License](LICENSE) 授权。

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

