#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_condition;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::count_type;
    using clapp::error;
    using clapp::error_kind;
    using clapp::raw_args;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    // clap's `matches.get_one::<String>("x")`, flattened to a plain string so an absent
    // argument and an argument with no value are the same observation as in Rust's `None`.
    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    std::vector<std::string> many_strings(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    std::vector<std::size_t> indices_of(const arg_matches& matches, std::string_view id) {
        const std::optional<std::span<const std::size_t>> found = matches.indices_of(id);
        if (!found.has_value()) return {};
        return std::vector<std::size_t>(found->begin(), found->end());
    }

    // ---------------------------------------------------------------------------
    // Fixtures. clap clones one `Command` per case and mutates it; a clapp command tree is
    // frozen, so each mutation is its own `consteval` fixture.
    // ---------------------------------------------------------------------------

    consteval command_spec make_set() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal").long_("mammal").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec set_cmd = make_set();

    consteval command_spec make_set_override_self() {
        command_builder app("test");
        std::move(app).args_override_self().arg(
                arg_builder("mammal").long_("mammal").action(arg_action::set));
        return app.freeze();
    }
    constexpr command_spec set_override_cmd = make_set_override_self();

    consteval command_spec make_append() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal").long_("mammal").action(arg_action::append));
        return app.freeze();
    }
    constexpr command_spec append_cmd = make_append();

    consteval command_spec make_set_true() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal").long_("mammal").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec set_true_cmd = make_set_true();

    consteval command_spec make_set_true_override_self() {
        command_builder app("test");
        std::move(app).args_override_self().arg(
                arg_builder("mammal").long_("mammal").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec set_true_override_cmd = make_set_true_override_self();

    consteval command_spec make_set_true_explicit_default() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal")
                                   .long_("mammal")
                                   .action(arg_action::set_true)
                                   .default_value("false"));
        return app.freeze();
    }
    constexpr command_spec set_true_default_cmd = make_set_true_explicit_default();

    consteval command_spec make_set_true_default_if_present() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::set_true)
                             .default_value_if("dog",
                                               arg_condition::present(),
                                               std::optional<std::string_view>{"true"}))
                .arg(arg_builder("dog").long_("dog").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec set_true_if_present_cmd = make_set_true_default_if_present();

    consteval command_spec make_set_true_default_if_value() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::set_true)
                             .default_value_if("dog", "true", "true"))
                .arg(arg_builder("dog").long_("dog").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec set_true_if_value_cmd = make_set_true_default_if_value();

    consteval command_spec make_set_true_required_if_eq() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::set_true)
                             .required_if_eq("dog", "true"))
                .arg(arg_builder("dog").long_("dog").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec set_true_required_if_eq_cmd = make_set_true_required_if_eq();

    consteval command_spec make_set_false() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal").long_("mammal").action(arg_action::set_false));
        return app.freeze();
    }
    constexpr command_spec set_false_cmd = make_set_false();

    consteval command_spec make_set_false_override_self() {
        command_builder app("test");
        std::move(app).args_override_self().arg(
                arg_builder("mammal").long_("mammal").action(arg_action::set_false));
        return app.freeze();
    }
    constexpr command_spec set_false_override_cmd = make_set_false_override_self();

    consteval command_spec make_set_false_explicit_default() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal")
                                   .long_("mammal")
                                   .action(arg_action::set_false)
                                   .default_value("true"));
        return app.freeze();
    }
    constexpr command_spec set_false_default_cmd = make_set_false_explicit_default();

    consteval command_spec make_set_false_default_if_present() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::set_false)
                             .default_value_if("dog",
                                               arg_condition::present(),
                                               std::optional<std::string_view>{"false"}))
                .arg(arg_builder("dog").long_("dog").action(arg_action::set_false));
        return app.freeze();
    }
    constexpr command_spec set_false_if_present_cmd = make_set_false_default_if_present();

    consteval command_spec make_set_false_default_if_value() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::set_false)
                             .default_value_if("dog", "false", "false"))
                .arg(arg_builder("dog").long_("dog").action(arg_action::set_false));
        return app.freeze();
    }
    constexpr command_spec set_false_if_value_cmd = make_set_false_default_if_value();

    consteval command_spec make_count() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal").long_("mammal").action(arg_action::count));
        return app.freeze();
    }
    constexpr command_spec count_cmd = make_count();

    consteval command_spec make_count_explicit_default() {
        command_builder app("test");
        std::move(app).arg(arg_builder("mammal")
                                   .long_("mammal")
                                   .action(arg_action::count)
                                   .default_value("10"));
        return app.freeze();
    }
    constexpr command_spec count_default_cmd = make_count_explicit_default();

    consteval command_spec make_count_default_if_present() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::count)
                             .default_value_if("dog",
                                               arg_condition::present(),
                                               std::optional<std::string_view>{"10"}))
                .arg(arg_builder("dog").long_("dog").action(arg_action::count));
        return app.freeze();
    }
    constexpr command_spec count_if_present_cmd = make_count_default_if_present();

    consteval command_spec make_count_default_if_value() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("mammal")
                             .long_("mammal")
                             .action(arg_action::count)
                             .default_value_if("dog", "2", "10"))
                .arg(arg_builder("dog").long_("dog").action(arg_action::count));
        return app.freeze();
    }
    constexpr command_spec count_if_value_cmd = make_count_default_if_value();

    // The fixtures really carry the actions the cases below name; a builder-side change that
    // resolved them differently must fail to compile rather than make every case vacuous.
    static_assert(set_cmd.find_arg("mammal")->get_action() == arg_action::set);
    static_assert(append_cmd.find_arg("mammal")->get_action() == arg_action::append);
    static_assert(set_true_cmd.find_arg("mammal")->get_action() == arg_action::set_true);
    static_assert(set_false_cmd.find_arg("mammal")->get_action() == arg_action::set_false);
    static_assert(count_cmd.find_arg("mammal")->get_action() == arg_action::count);
    static_assert(set_override_cmd.is_args_override_self());
    static_assert(!set_cmd.is_args_override_self());

}  // namespace

// ---------------------------------------------------------------------------
// ArgAction::Set
// ---------------------------------------------------------------------------

CLAPP_TEST("action.rs::set") {
    const outcome absent = clapp::parse(set_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!one_string(*absent, "mammal").has_value());
    CLAPP_CHECK(!absent->contains_id("mammal"));
    CLAPP_CHECK(!absent->index_of("mammal").has_value());

    const outcome once = clapp::parse(set_cmd, raw_args{"test", "--mammal", "dog"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(one_string(*once, "mammal") == std::optional<std::string>{"dog"});
    CLAPP_CHECK(once->contains_id("mammal"));
    // The index of the VALUE, not of `--mammal`.
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{2});

    const outcome twice =
            clapp::parse(set_cmd, raw_args{"test", "--mammal", "dog", "--mammal", "cat"});
    CLAPP_CHECK(!twice.has_value());
    CLAPP_CHECK(kind_of(twice) == error_kind::argument_conflict);

    const outcome overridden =
            clapp::parse(set_override_cmd, raw_args{"test", "--mammal", "dog", "--mammal", "cat"});
    CLAPP_CHECK(overridden.has_value());
    CLAPP_CHECK(one_string(*overridden, "mammal") == std::optional<std::string>{"cat"});
    CLAPP_CHECK(overridden->contains_id("mammal"));
    CLAPP_CHECK(overridden->index_of("mammal") == std::optional<std::size_t>{4});
}

// ---------------------------------------------------------------------------
// ArgAction::Append
// ---------------------------------------------------------------------------

CLAPP_TEST("action.rs::append") {
    const outcome absent = clapp::parse(append_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!one_string(*absent, "mammal").has_value());
    CLAPP_CHECK(!absent->contains_id("mammal"));
    CLAPP_CHECK(!absent->index_of("mammal").has_value());

    const outcome once = clapp::parse(append_cmd, raw_args{"test", "--mammal", "dog"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(one_string(*once, "mammal") == std::optional<std::string>{"dog"});
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(indices_of(*once, "mammal") == std::vector<std::size_t>{2});

    const outcome twice =
            clapp::parse(append_cmd, raw_args{"test", "--mammal", "dog", "--mammal", "cat"});
    CLAPP_CHECK(twice.has_value());
    CLAPP_CHECK(many_strings(*twice, "mammal") == std::vector<std::string>{"dog", "cat"});
    CLAPP_CHECK(twice->contains_id("mammal"));
    CLAPP_CHECK(indices_of(*twice, "mammal") == std::vector<std::size_t>{2, 4});
}

// ---------------------------------------------------------------------------
// ArgAction::SetTrue
// ---------------------------------------------------------------------------

CLAPP_TEST("action.rs::set_true") {
    const outcome absent = clapp::parse(set_true_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!absent->get_flag("mammal"));
    // Present, from the injected `default_value("false")` — NOT absent like `Set` above.
    CLAPP_CHECK(absent->contains_id("mammal"));
    CLAPP_CHECK(absent->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome once = clapp::parse(set_true_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(once->get_flag("mammal"));
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome twice = clapp::parse(set_true_cmd, raw_args{"test", "--mammal", "--mammal"});
    CLAPP_CHECK(!twice.has_value());
    CLAPP_CHECK(kind_of(twice) == error_kind::argument_conflict);

    const outcome overridden =
            clapp::parse(set_true_override_cmd, raw_args{"test", "--mammal", "--mammal"});
    CLAPP_CHECK(overridden.has_value());
    CLAPP_CHECK(overridden->get_flag("mammal"));
    CLAPP_CHECK(overridden->contains_id("mammal"));
    CLAPP_CHECK(overridden->index_of("mammal") == std::optional<std::size_t>{2});
}

CLAPP_TEST("action.rs::set_true_with_explicit_default_value") {
    const outcome once = clapp::parse(set_true_default_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(once->get_flag("mammal"));
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome absent = clapp::parse(set_true_default_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!absent->get_flag("mammal"));
    CLAPP_CHECK(absent->contains_id("mammal"));
    CLAPP_CHECK(absent->index_of("mammal") == std::optional<std::size_t>{1});
}

CLAPP_TEST("action.rs::set_true_with_default_value_if_present") {
    const outcome absent = clapp::parse(set_true_if_present_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!absent->get_flag("dog"));
    CLAPP_CHECK(!absent->get_flag("mammal"));

    const outcome dog = clapp::parse(set_true_if_present_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(dog.has_value());
    CLAPP_CHECK(dog->get_flag("dog"));
    CLAPP_CHECK(dog->get_flag("mammal"));

    const outcome mammal = clapp::parse(set_true_if_present_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(!mammal->get_flag("dog"));
    CLAPP_CHECK(mammal->get_flag("mammal"));
}

CLAPP_TEST("action.rs::set_true_with_default_value_if_value") {
    const outcome absent = clapp::parse(set_true_if_value_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(!absent->get_flag("dog"));
    CLAPP_CHECK(!absent->get_flag("mammal"));

    const outcome dog = clapp::parse(set_true_if_value_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(dog.has_value());
    CLAPP_CHECK(dog->get_flag("dog"));
    // `--dog` stores the literal string "true", which is what the predicate compares to.
    CLAPP_CHECK(dog->get_flag("mammal"));

    const outcome mammal = clapp::parse(set_true_if_value_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(!mammal->get_flag("dog"));
    CLAPP_CHECK(mammal->get_flag("mammal"));
}

CLAPP_TEST("action.rs::set_true_with_required_if_eq") {
    const outcome mammal = clapp::parse(set_true_required_if_eq_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(!mammal->get_flag("dog"));
    CLAPP_CHECK(mammal->get_flag("mammal"));

    // `--dog` stores "true", so `--mammal` becomes required and is missing.
    const outcome dog_only = clapp::parse(set_true_required_if_eq_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(!dog_only.has_value());

    const outcome both =
            clapp::parse(set_true_required_if_eq_cmd, raw_args{"test", "--dog", "--mammal"});
    CLAPP_CHECK(both.has_value());
    CLAPP_CHECK(both->get_flag("dog"));
    CLAPP_CHECK(both->get_flag("mammal"));
}

// ---------------------------------------------------------------------------
// ArgAction::SetFalse
// ---------------------------------------------------------------------------

CLAPP_TEST("action.rs::set_false") {
    const outcome absent = clapp::parse(set_false_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    // The mirror image of SetTrue: the injected default is "true".
    CLAPP_CHECK(absent->get_flag("mammal"));
    CLAPP_CHECK(absent->contains_id("mammal"));
    CLAPP_CHECK(absent->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome once = clapp::parse(set_false_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(!once->get_flag("mammal"));
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome twice = clapp::parse(set_false_cmd, raw_args{"test", "--mammal", "--mammal"});
    CLAPP_CHECK(!twice.has_value());
    CLAPP_CHECK(kind_of(twice) == error_kind::argument_conflict);

    const outcome overridden =
            clapp::parse(set_false_override_cmd, raw_args{"test", "--mammal", "--mammal"});
    CLAPP_CHECK(overridden.has_value());
    CLAPP_CHECK(!overridden->get_flag("mammal"));
    CLAPP_CHECK(overridden->contains_id("mammal"));
    CLAPP_CHECK(overridden->index_of("mammal") == std::optional<std::size_t>{2});
}

CLAPP_TEST("action.rs::set_false_with_explicit_default_value") {
    const outcome once = clapp::parse(set_false_default_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(!once->get_flag("mammal"));
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome absent = clapp::parse(set_false_default_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_flag("mammal"));
    CLAPP_CHECK(absent->contains_id("mammal"));
    CLAPP_CHECK(absent->index_of("mammal") == std::optional<std::size_t>{1});
}

CLAPP_TEST("action.rs::set_false_with_default_value_if_present") {
    const outcome absent = clapp::parse(set_false_if_present_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_flag("dog"));
    CLAPP_CHECK(absent->get_flag("mammal"));

    const outcome dog = clapp::parse(set_false_if_present_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(dog.has_value());
    CLAPP_CHECK(!dog->get_flag("dog"));
    CLAPP_CHECK(!dog->get_flag("mammal"));

    const outcome mammal = clapp::parse(set_false_if_present_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(mammal->get_flag("dog"));
    CLAPP_CHECK(!mammal->get_flag("mammal"));
}

CLAPP_TEST("action.rs::set_false_with_default_value_if_value") {
    const outcome absent = clapp::parse(set_false_if_value_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_flag("dog"));
    CLAPP_CHECK(absent->get_flag("mammal"));

    const outcome dog = clapp::parse(set_false_if_value_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(dog.has_value());
    CLAPP_CHECK(!dog->get_flag("dog"));
    CLAPP_CHECK(!dog->get_flag("mammal"));

    const outcome mammal = clapp::parse(set_false_if_value_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(mammal->get_flag("dog"));
    CLAPP_CHECK(!mammal->get_flag("mammal"));
}

// ---------------------------------------------------------------------------
// ArgAction::Count
// ---------------------------------------------------------------------------

CLAPP_TEST("action.rs::count") {
    const outcome absent = clapp::parse(count_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_count("mammal") == count_type{0});
    CLAPP_CHECK(absent->contains_id("mammal"));
    CLAPP_CHECK(absent->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome once = clapp::parse(count_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(once.has_value());
    CLAPP_CHECK(once->get_count("mammal") == count_type{1});
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome twice = clapp::parse(count_cmd, raw_args{"test", "--mammal", "--mammal"});
    CLAPP_CHECK(twice.has_value());
    CLAPP_CHECK(twice->get_count("mammal") == count_type{2});
    CLAPP_CHECK(twice->contains_id("mammal"));
    CLAPP_CHECK(twice->index_of("mammal") == std::optional<std::size_t>{2});
}

CLAPP_TEST("action.rs::count_with_explicit_default_value") {
    const outcome once = clapp::parse(count_default_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(once.has_value());
    // A present counter always starts from zero; the default only fills an absent one.
    CLAPP_CHECK(once->get_count("mammal") == count_type{1});
    CLAPP_CHECK(once->contains_id("mammal"));
    CLAPP_CHECK(once->index_of("mammal") == std::optional<std::size_t>{1});

    const outcome absent = clapp::parse(count_default_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_count("mammal") == count_type{10});
    CLAPP_CHECK(absent->contains_id("mammal"));
    CLAPP_CHECK(absent->index_of("mammal") == std::optional<std::size_t>{1});
}

CLAPP_TEST("action.rs::count_with_default_value_if_present") {
    const outcome absent = clapp::parse(count_if_present_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_count("dog") == count_type{0});
    CLAPP_CHECK(absent->get_count("mammal") == count_type{0});

    const outcome dog = clapp::parse(count_if_present_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(dog.has_value());
    CLAPP_CHECK(dog->get_count("dog") == count_type{1});
    CLAPP_CHECK(dog->get_count("mammal") == count_type{10});

    const outcome mammal = clapp::parse(count_if_present_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(mammal->get_count("dog") == count_type{0});
    CLAPP_CHECK(mammal->get_count("mammal") == count_type{1});
}

CLAPP_TEST("action.rs::count_with_default_value_if_value") {
    // The case that separates a real predicate from `contains_id`: the rule fires only
    // when `--dog` has been seen EXACTLY twice, so one `--dog` must leave `mammal` at 0.
    const outcome absent = clapp::parse(count_if_value_cmd, raw_args{"test"});
    CLAPP_CHECK(absent.has_value());
    CLAPP_CHECK(absent->get_count("dog") == count_type{0});
    CLAPP_CHECK(absent->get_count("mammal") == count_type{0});

    const outcome one_dog = clapp::parse(count_if_value_cmd, raw_args{"test", "--dog"});
    CLAPP_CHECK(one_dog.has_value());
    CLAPP_CHECK(one_dog->get_count("dog") == count_type{1});
    CLAPP_CHECK(one_dog->get_count("mammal") == count_type{0});

    const outcome two_dogs = clapp::parse(count_if_value_cmd, raw_args{"test", "--dog", "--dog"});
    CLAPP_CHECK(two_dogs.has_value());
    CLAPP_CHECK(two_dogs->get_count("dog") == count_type{2});
    CLAPP_CHECK(two_dogs->get_count("mammal") == count_type{10});

    const outcome mammal = clapp::parse(count_if_value_cmd, raw_args{"test", "--mammal"});
    CLAPP_CHECK(mammal.has_value());
    CLAPP_CHECK(mammal->get_count("dog") == count_type{0});
    CLAPP_CHECK(mammal->get_count("mammal") == count_type{1});
}
