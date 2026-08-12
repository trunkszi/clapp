#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/value_range.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/parse.hpp>

#include "support/check.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using clapp::arg_action;
    using clapp::arg_builder;
    using clapp::arg_matches;
    using clapp::command_builder;
    using clapp::command_spec;
    using clapp::error;
    using clapp::error_kind;
    using clapp::raw_args;
    using clapp::value_range;

    using outcome = std::expected<arg_matches, error>;

    error_kind kind_of(const outcome& got) {
        return got.has_value() ? error_kind::io : got.error().kind();
    }

    std::optional<std::string> one_string(const arg_matches& matches, std::string_view id) {
        const std::optional<const std::string*> found = matches.get_one<std::string>(id);
        if (!found.has_value()) return std::nullopt;
        return **found;
    }

    std::vector<std::string> raw_of(const arg_matches& matches, std::string_view id) {
        std::vector<std::string> out;
        const clapp::matched_arg* found = matches.find_arg(id);
        if (found == nullptr) return out;
        for (const clapp::os_string& one : found->raw_values()) out.emplace_back(one.chars());
        return out;
    }

    // ---------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------

    consteval command_spec make_self_override_flag() {
        command_builder app("posix");
        std::move(app).arg(arg_builder("flag")
                                   .long_("flag")
                                   .action(arg_action::set_true)
                                   .overrides_with("flag"));
        return app.freeze();
    }
    constexpr command_spec self_override_flag = make_self_override_flag();

    consteval command_spec make_self_override_opt() {
        command_builder app("posix");
        std::move(app).arg(arg_builder("opt").long_("opt").required(false).overrides_with("opt"));
        return app.freeze();
    }
    constexpr command_spec self_override_opt = make_self_override_opt();

    consteval command_spec make_long_flags() {
        command_builder app("posix");
        std::move(app)
                .arg(arg_builder("flag").long_("flag").overrides_with("color").action(
                        arg_action::set_true))
                .arg(arg_builder("color").long_("color").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec long_flags = make_long_flags();

    consteval command_spec make_short_flags() {
        command_builder app("posix");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").overrides_with("color").action(
                        arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec short_flags = make_short_flags();

    consteval command_spec make_long_opts() {
        command_builder app("posix");
        std::move(app)
                .arg(arg_builder("flag").long_("flag").value_name("flag").overrides_with("color"))
                .arg(arg_builder("color").long_("color").value_name("color"));
        return app.freeze();
    }
    constexpr command_spec long_opts = make_long_opts();

    consteval command_spec make_short_opts() {
        command_builder app("posix");
        std::move(app)
                .arg(arg_builder("f").short_('f').value_name("flag").overrides_with("c"))
                .arg(arg_builder("c").short_('c').value_name("color"));
        return app.freeze();
    }
    constexpr command_spec short_opts = make_short_opts();

    consteval command_spec make_conflict_override() {
        command_builder app("conflict_overridden");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").conflicts_with("debug").action(
                        arg_action::set_true))
                .arg(arg_builder("debug").short_('d').long_("debug").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").overrides_with("flag").action(
                        arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec conflict_override = make_conflict_override();

    // clap's `conflict_overridden_3` drops every `action(SetTrue)`, so all three take a
    // value. The arguments are given as bare flags on the command line, which is why the
    // case can end in a conflict rather than a missing value: the conflict is decided
    // before the values are.
    consteval command_spec make_conflict_override_valued() {
        command_builder app("conflict_overridden");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .num_args(value_range::optional())
                             .conflicts_with("debug"))
                .arg(arg_builder("debug").short_('d').long_("debug").num_args(
                        value_range::optional()))
                .arg(arg_builder("color")
                             .short_('c')
                             .long_("color")
                             .num_args(value_range::optional())
                             .overrides_with("flag"));
        return app.freeze();
    }
    constexpr command_spec conflict_override_valued = make_conflict_override_valued();

    consteval command_spec make_required_positional_override() {
        command_builder app("require_overridden");
        std::move(app)
                .arg(arg_builder("pos").index(1).required())
                .arg(arg_builder("color")
                             .short_('c')
                             .long_("color")
                             .num_args(value_range::optional())
                             .overrides_with("pos"));
        return app.freeze();
    }
    constexpr command_spec required_positional_override = make_required_positional_override();

    consteval command_spec make_required_positional_override_flag() {
        command_builder app("require_overridden");
        std::move(app)
                .arg(arg_builder("req_pos").index(1).required())
                .arg(arg_builder("color")
                             .short_('c')
                             .long_("color")
                             .overrides_with("req_pos")
                             .action(arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec required_positional_override_flag =
            make_required_positional_override_flag();

    consteval command_spec make_requires_override() {
        command_builder app("require_overridden");
        std::move(app)
                .arg(arg_builder("flag").short_('f').long_("flag").requires_("debug").action(
                        arg_action::set_true))
                .arg(arg_builder("debug").short_('d').long_("debug").action(arg_action::set_true))
                .arg(arg_builder("color").short_('c').long_("color").overrides_with("flag").action(
                        arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec requires_override = make_requires_override();

    consteval command_spec make_requires_override_valued() {
        command_builder app("require_overridden");
        std::move(app)
                .arg(arg_builder("flag")
                             .short_('f')
                             .long_("flag")
                             .num_args(value_range::optional())
                             .requires_("debug"))
                .arg(arg_builder("debug").short_('d').long_("debug").num_args(
                        value_range::optional()))
                .arg(arg_builder("color")
                             .short_('c')
                             .long_("color")
                             .num_args(value_range::optional())
                             .overrides_with("flag"));
        return app.freeze();
    }
    constexpr command_spec requires_override_valued = make_requires_override_valued();

    consteval command_spec make_incremental() {
        command_builder app("test");
        std::move(app)
                .arg(arg_builder("name")
                             .long_("name")
                             .value_name("NAME")
                             .action(arg_action::append)
                             .required())
                .arg(arg_builder("no-name").long_("no-name").overrides_with("name").action(
                        arg_action::set_true));
        return app.freeze();
    }
    constexpr command_spec incremental = make_incremental();

    static_assert(long_flags.find_arg("flag")->get_overrides().size() == 1);
    static_assert(self_override_flag.find_arg("flag")->get_overrides().front().name() == "flag");

}  // namespace

// ---------------------------------------------------------------------------
// Self-override
// ---------------------------------------------------------------------------

CLAPP_TEST("posix_compatible.rs::flag_overrides_itself") {
    const outcome got = clapp::parse(self_override_flag, raw_args{"", "--flag", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("posix_compatible.rs::option_overrides_itself") {
    const outcome got = clapp::parse(self_override_opt, raw_args{"", "--opt=some", "--opt=other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("opt"));
    CLAPP_CHECK(one_string(*got, "opt") == std::optional<std::string>{"other"});
}

// ---------------------------------------------------------------------------
// Flags, both orders
// ---------------------------------------------------------------------------

CLAPP_TEST("posix_compatible.rs::posix_compatible_flags_long") {
    const outcome got = clapp::parse(long_flags, raw_args{"", "--flag", "--color"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_flags_long_rev") {
    // Only `--flag` declares the override, and here it is `--color` that loses.
    const outcome got = clapp::parse(long_flags, raw_args{"", "--color", "--flag"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->get_flag("color"));
    CLAPP_CHECK(got->get_flag("flag"));
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_flags_short") {
    const outcome got = clapp::parse(short_flags, raw_args{"", "-f", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_flags_short_rev") {
    const outcome got = clapp::parse(short_flags, raw_args{"", "-c", "-f"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->get_flag("color"));
    CLAPP_CHECK(got->get_flag("flag"));
}

// ---------------------------------------------------------------------------
// Options, both orders and both spellings
// ---------------------------------------------------------------------------

CLAPP_TEST("posix_compatible.rs::posix_compatible_opts_long") {
    const outcome got = clapp::parse(long_opts, raw_args{"", "--flag", "some", "--color", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("color"));
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"other"});
    // Removal, not shadowing.
    CLAPP_CHECK(!got->contains_id("flag"));
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_opts_long_rev") {
    const outcome got = clapp::parse(long_opts, raw_args{"", "--color", "some", "--flag", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("color"));
    CLAPP_CHECK(got->contains_id("flag"));
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"other"});
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_opts_long_equals") {
    const outcome got = clapp::parse(long_opts, raw_args{"", "--flag=some", "--color=other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(one_string(*got, "color") == std::optional<std::string>{"other"});
    CLAPP_CHECK(!got->contains_id("flag"));
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_opts_long_equals_rev") {
    const outcome got = clapp::parse(long_opts, raw_args{"", "--color=some", "--flag=other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("color"));
    CLAPP_CHECK(one_string(*got, "flag") == std::optional<std::string>{"other"});
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_opts_short") {
    const outcome got = clapp::parse(short_opts, raw_args{"", "-f", "some", "-c", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->contains_id("c"));
    CLAPP_CHECK(one_string(*got, "c") == std::optional<std::string>{"other"});
    CLAPP_CHECK(!got->contains_id("f"));
}

CLAPP_TEST("posix_compatible.rs::posix_compatible_opts_short_rev") {
    const outcome got = clapp::parse(short_opts, raw_args{"", "-c", "some", "-f", "other"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->contains_id("c"));
    CLAPP_CHECK(one_string(*got, "f") == std::optional<std::string>{"other"});
}

// ---------------------------------------------------------------------------
// An override erases the loser's conflicts
// ---------------------------------------------------------------------------

CLAPP_TEST("posix_compatible.rs::conflict_overridden") {
    // `-c` removes `-f` before `-d` arrives, so the declared conflict never fires.
    const outcome got = clapp::parse(conflict_override, raw_args{"", "-f", "-c", "-d"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(!got->get_flag("flag"));
    CLAPP_CHECK(got->get_flag("debug"));
}

CLAPP_TEST("posix_compatible.rs::conflict_overridden_2") {
    const outcome got = clapp::parse(conflict_override, raw_args{"", "-f", "-d", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(got->get_flag("debug"));
    CLAPP_CHECK(!got->get_flag("flag"));
}

CLAPP_TEST("posix_compatible.rs::conflict_overridden_3") {
    // `-f` arrives LAST, so nothing removed it and the conflict with `-d` stands.
    const outcome got = clapp::parse(conflict_override_valued, raw_args{"", "-d", "-c", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::argument_conflict);
}

CLAPP_TEST("posix_compatible.rs::conflict_overridden_4") {
    const outcome got = clapp::parse(conflict_override, raw_args{"", "-d", "-f", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(!got->get_flag("flag"));
    CLAPP_CHECK(got->get_flag("debug"));
}

// ---------------------------------------------------------------------------
// An override erases the loser's requirements
// ---------------------------------------------------------------------------

CLAPP_TEST("posix_compatible.rs::pos_required_overridden_by_flag") {
    const outcome got = clapp::parse(required_positional_override, raw_args{"", "test", "-c"});
    CLAPP_CHECK(got.has_value());
}

CLAPP_TEST("posix_compatible.rs::require_overridden_2") {
    const outcome got =
            clapp::parse(required_positional_override_flag, raw_args{"", "-c", "req_pos"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(!got->get_flag("color"));
    CLAPP_CHECK(got->contains_id("req_pos"));
}

CLAPP_TEST("posix_compatible.rs::require_overridden_3") {
    // `-f` requires `-d`. `-c` removes `-f`, so `-d` is no longer demanded.
    const outcome got = clapp::parse(requires_override, raw_args{"", "-f", "-c"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(got->get_flag("color"));
    CLAPP_CHECK(!got->get_flag("flag"));
    CLAPP_CHECK(!got->get_flag("debug"));
}

CLAPP_TEST("posix_compatible.rs::require_overridden_4") {
    // The other order: `-f` survives, so its requirement stands and is unmet.
    const outcome got = clapp::parse(requires_override_valued, raw_args{"", "-c", "-f"});
    CLAPP_CHECK(!got.has_value());
    CLAPP_CHECK(kind_of(got) == error_kind::missing_required_argument);
}

CLAPP_TEST("posix_compatible.rs::incremental_override") {
    // `--no-name` clears what `--name=ahmed` collected; `--name=ali` starts over.
    const outcome got =
            clapp::parse(incremental, raw_args{"test", "--name=ahmed", "--no-name", "--name=ali"});
    CLAPP_CHECK(got.has_value());
    CLAPP_CHECK(raw_of(*got, "name") == std::vector<std::string>{"ali"});
    CLAPP_CHECK(!got->get_flag("no-name"));
}
