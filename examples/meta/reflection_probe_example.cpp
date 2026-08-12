#if __has_include(<meta>)
#    include <meta>  // GCC 16+, clang-p2996 current p2996 branch
#elif __has_include(<experimental/meta>)
#    include <experimental/meta>  // earlier clang-p2996 commits
#else
#    error "C++26 static reflection required: neither <meta> nor <experimental/meta> is available."
#endif

#include <algorithm>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
namespace probe {

    // ---------------------------------------------------------------------------
    // [1] cstr: fixed-capacity string
    //
    // P3394 requires annotations to have structural type. std::string_view does not
    // (private members); const char* does, but reflect_constant fails on extract
    // (points at a string literal). An all-public array + length is structural and
    // can be fully extracted.
    // ---------------------------------------------------------------------------
    template<std::size_t Cap>
    struct cstr {
        char data[Cap]{};
        std::size_t size = 0;

        constexpr cstr() = default;

        template<std::size_t N>
            requires(N <= Cap)
        consteval cstr(const char (&s)[N]) : size(N - 1) {
            std::copy_n(s, N - 1, data);
        }

        constexpr bool empty() const { return size == 0; }
        constexpr std::string_view view() const { return {data, size}; }
    };

    enum class action {
        infer,
        set [[maybe_unused]],
        append [[maybe_unused]],
        set_true [[maybe_unused]],
        set_false [[maybe_unused]],
        count
    };

    struct arg_attr {
        char short_ = '\0';
        cstr<64> long_{};
        cstr<256> help{};
        [[maybe_unused]] cstr<64> env{};
        action act    = action::infer;
        [[maybe_unused]] bool required = false;
    };

    // ---------------------------------------------------------------------------
    // [2] annotation_of: annotation extraction
    //
    // Four pitfalls that must be hit correctly:
    //   a. Must call constant_of first — direct type_of(annotation) comparisons are
    //      always false; splicing the annotation directly errors with
    //      "cannot use an annotation in a splice expression"
    //   b. The type is const-qualified — compare against const A, not A
    //   c. Spell that const with std::meta::add_const(^^A), NOT ^^const A. Inside a
    //      template whose parameter is A, clang-p2996 0.0.0-p2996.5cc3eb319 never
    //      matches ^^const A: annotation_of returns nullopt for every annotation and
    //      emits no diagnostic at all, so the whole DSL quietly stops working while
    //      still compiling. GCC 16 accepts the bad spelling, which is how it survived
    //      two milestones. Outside a template the two agree — that asymmetry is the
    //      tell. remove_const(type_of(c)) == ^^A is portable too.
    //   d. Annotation values must not hold pointers to string literals — hence cstr
    // ---------------------------------------------------------------------------
    template<class A>
    consteval std::optional<A> annotation_of(std::meta::info item) {
        for (std::meta::info a : std::meta::annotations_of(item)) {
            std::meta::info c = std::meta::constant_of(a);
            if (std::meta::type_of(c) == std::meta::add_const(^^A)) return std::meta::extract<A>(c);
        }
        return std::nullopt;
    }

    // ---------------------------------------------------------------------------
    // [3] Type-shape recognition
    //
    // Key: dealias first. For type aliases (using sub = std::variant<...>), calling
    // template_arguments_of without dealias reports that the reflection does not
    // have template arguments.
    // ---------------------------------------------------------------------------
    consteval bool is_specialization_of(std::meta::info t, std::meta::info tmpl) {
        t = std::meta::dealias(t);
        return std::meta::has_template_arguments(t) && std::meta::template_of(t) == tmpl;
    }

    // ---------------------------------------------------------------------------
    // [4] Name conversion: snake_case -> kebab-case, lifted to static storage
    // ---------------------------------------------------------------------------
    consteval std::string_view to_kebab(std::string_view id) {
        std::string s{id};
        while (!s.empty() && s.back() == '_') s.pop_back();  // auto_ -> auto
        for (char& c : s)
            if (c == '_') c = '-';
        return std::define_static_string(s);
    }

} // namespace probe

// ===========================================================================
// Sample types under reflection
// ===========================================================================

enum class color_choice {
    always [[maybe_unused]],
    auto_ [[maybe_unused]],
    never [[maybe_unused]]
};

struct cmd_add {};
struct cmd_commit {};
using subcommands = std::variant<cmd_add, cmd_commit>;

struct cli {
    [[maybe_unused]] [[= probe::arg_attr{.short_ = 'n', .long_ = "name", .help = "Who to greet"}]]
    std::string name;

    [[maybe_unused]] [[= probe::arg_attr{.short_ = 'v', .act = probe::action::count}]]
    int verbose;

    [[maybe_unused]] std::optional<int> output_file; // no annotation: fully inferred

    [[maybe_unused]] int plain;
};

consteval std::meta::info member(std::size_t i) {
    return std::meta::nonstatic_data_members_of(^^cli, std::meta::access_context::current())[i];
}

// ===========================================================================
// Assertions: every rule from the design doc is verified here
// ===========================================================================

// [1] Annotations are extractable and values are complete
static_assert(probe::annotation_of<probe::arg_attr>(member(0)).has_value());
static_assert(probe::annotation_of<probe::arg_attr>(member(0))->short_ == 'n');
static_assert(probe::annotation_of<probe::arg_attr>(member(0))->long_.view() == "name");
static_assert(probe::annotation_of<probe::arg_attr>(member(0))->help.view() == "Who to greet");

// [2] Unset fields keep defaults (distinguish "user said nothing" from "user said the default")
static_assert(probe::annotation_of<probe::arg_attr>(member(1))->act == probe::action::count);
static_assert(probe::annotation_of<probe::arg_attr>(member(1))->long_.empty());

// [3] Members without annotations return nullopt
static_assert(!probe::annotation_of<probe::arg_attr>(member(2)).has_value());

// [4] Field name reflection
static_assert(std::meta::identifier_of(member(0)) == "name");
static_assert(std::meta::identifier_of(member(2)) == "output_file");

// [5] Name deduction: output_file -> output-file
static_assert(probe::to_kebab(std::meta::identifier_of(member(2))) == "output-file");

// [6] Type shape: optional recognition
static_assert(probe::is_specialization_of(std::meta::type_of(member(2)), ^^std::optional));
static_assert(!probe::is_specialization_of(std::meta::type_of(member(3)), ^^std::optional));
static_assert(std::meta::template_arguments_of(std::meta::dealias(^^std::optional<int>))[0] ==
              ^^int);

// [7] Enum: information source for automatic value_enum
static_assert(std::meta::enumerators_of(^^color_choice).size() == 3);
static_assert(std::meta::identifier_of(std::meta::enumerators_of (^^color_choice)[1]) == "auto_");
static_assert(probe::to_kebab(std::meta::identifier_of(
                      std::meta::enumerators_of (^^color_choice)[1])) == "auto");

// [8] variant alternatives: subcommand source (note dealias)
static_assert(std::meta::template_arguments_of(std::meta::dealias(^^subcommands)).size() == 2);
static_assert(std::meta::identifier_of(std::meta::template_arguments_of(
                      std::meta::dealias(^^subcommands))[0]) == "cmd_add");

// ===========================================================================
// [9] template for + splicer write-back — core of from_matches<T>()
// ===========================================================================

struct filled {
    int a;
    double b;
    std::string c;
};

auto demo_splice_writeback() -> filled {
    filled out{};
    template for (constexpr std::meta::info m :
                  std::define_static_array(std::meta::nonstatic_data_members_of(
                          ^^filled, std::meta::access_context::current()))) {
        using M                         = [:std::meta::type_of(m):];
        constexpr std::string_view name = std::meta::identifier_of(m);

        if constexpr (std::is_same_v<M, int>)
            out.[:m:] = 42;
        else if constexpr (std::is_same_v<M, double>)
            out.[:m:] = 3.5;
        else if constexpr (std::is_same_v<M, std::string>)
            out.[:m:] = std::string{name};
    }
    return out;
}
} // namespace

int main() {
    std::println("clapp reflection probe — all compile-time assertions passed\n");

    filled v = demo_splice_writeback();
    std::println("[9] splicer write-back: a={} b={} c={}", v.a, v.b, v.c);

    std::println("\n[7] enum -> possible values:");
    template for (constexpr std::meta::info e :
                  std::define_static_array(std::meta::enumerators_of(^^color_choice)))
            std::println("      {} -> {}",
                         std::meta::identifier_of(e),
                         probe::to_kebab(std::meta::identifier_of(e)));

    std::println("\n[8] variant -> subcommands:");
    template for (constexpr std::meta::info t : std::define_static_array(
                          std::meta::template_arguments_of(std::meta::dealias(^^subcommands))))
            std::println("      {}", std::meta::identifier_of(t));

    std::println("\n[5] field name -> long option:");
    template for (constexpr std::meta::info m :
                  std::define_static_array(std::meta::nonstatic_data_members_of(
                          ^^cli, std::meta::access_context::current())))
            std::println("      {} -> --{}",
                         std::meta::identifier_of(m),
                         probe::to_kebab(std::meta::identifier_of(m)));
}
