#include <clapp/output/complete.hpp>

#include "support/check.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace {
    enum class mode { fast, slow, spaced[[= clapp::value{.name = "value with space"}]], quoted[[= clapp::value{.name = "a'$(false)"}]], secret[[= clapp::value{.hide = true}]] };
    consteval auto completion_command() {
        clapp::command_builder root("app");
        std::move(root)
                .disable_help_subcommand()
                .arg(clapp::arg_builder("mode").long_("mode").short_('m').alias("speed").value_parser<mode>())
                .arg(clapp::arg_builder("pair").long_("pair").num_args(clapp::value_range::exactly(2)).value_parser<mode>())
                .arg(clapp::arg_builder("many").long_("many").num_args(clapp::value_range::between(1, 3)).value_parser<mode>())
                .arg(clapp::arg_builder("negative").long_("negative").num_args(clapp::value_range::exactly(2)).allow_negative_numbers())
                .arg(clapp::arg_builder("literal").long_("literal").num_args(clapp::value_range::exactly(2)).allow_hyphen_values())
                .arg(clapp::arg_builder("dir").long_("dir").value_hint(clapp::value_hint::dir_path))
                .arg(clapp::arg_builder("user").long_("user").value_hint(clapp::value_hint::username))
                .arg(clapp::arg_builder("host").long_("host").value_hint(clapp::value_hint::hostname))
                .arg(clapp::arg_builder("url").long_("url").value_hint(clapp::value_hint::url))
                .arg(clapp::arg_builder("hidden").long_("hidden").hide())
                .subcommand(clapp::command_builder("run").alias("r")
                        .arg(clapp::arg_builder("output").long_("output").value_hint(clapp::value_hint::file_path))
                        .subcommand(clapp::command_builder("nested")))
                .subcommand(clapp::command_builder("secret-command").hide());
        return root.freeze();
    }
    constexpr auto command = completion_command();
}

CLAPP_TEST("all completion shells expose commands aliases options and visible values") {
    for (auto sh : {clapp::shell::bash, clapp::shell::zsh, clapp::shell::fish, clapp::shell::powershell}) {
        const auto script = clapp::generate_completion(command, sh);
        CLAPP_CHECK(script.contains("mode"));
        CLAPP_CHECK(script.contains("speed"));
        CLAPP_CHECK(script.contains("fast"));
        CLAPP_CHECK(script.contains("slow"));
        CLAPP_CHECK(script.contains("run"));
        CLAPP_CHECK(script.contains("nested"));
        CLAPP_CHECK(!script.contains("secret"));
    }
}

CLAPP_TEST("completion hints select native shell completers") {
    const auto bash = clapp::generate_completion(command, clapp::shell::bash);
    CLAPP_CHECK(bash.contains("compgen -d"));
    CLAPP_CHECK(bash.contains("compgen -f"));
    CLAPP_CHECK(bash.contains("compgen -u"));
    CLAPP_CHECK(bash.contains("compgen -A hostname"));
    const auto zsh = clapp::generate_completion(command, clapp::shell::zsh);
    CLAPP_CHECK(zsh.contains("_files -/"));
    CLAPP_CHECK(zsh.contains("_hosts"));
    CLAPP_CHECK(zsh.contains("_users"));
    CLAPP_CHECK(zsh.contains("_urls"));
    const auto fish = clapp::generate_completion(command, clapp::shell::fish);
    CLAPP_CHECK(fish.contains("__fish_complete_directories"));
    CLAPP_CHECK(fish.contains("__fish_complete_users"));
    const auto powershell = clapp::generate_completion(command, clapp::shell::powershell);
    CLAPP_CHECK(powershell.contains("Register-ArgumentCompleter"));
    CLAPP_CHECK(powershell.contains("Get-ChildItem -Directory"));
}

CLAPP_TEST("completion bin overrides are quoted as data") {
    for (auto sh : {clapp::shell::bash, clapp::shell::zsh, clapp::shell::fish, clapp::shell::powershell}) {
        auto script = clapp::generate_completion(command, sh, "odd'bin;$(false)");
        CLAPP_CHECK(script.contains("bin;$(false)"));
        CLAPP_CHECK(!script.contains("_clapp_odd"));
    }
}

CLAPP_TEST("completion state models arity trailing tokens and positional hints") {
    constexpr auto paths = clapp::command_builder("paths")
            .arg(clapp::arg_builder("input").value_hint(clapp::value_hint::dir_path)).freeze();
    for (auto sh : {clapp::shell::bash, clapp::shell::zsh, clapp::shell::fish, clapp::shell::powershell}) {
        const auto script = clapp::generate_completion(command, sh);
        CLAPP_CHECK(script.contains("pending_min") || script.contains("pendingMin"));
        CLAPP_CHECK(script.contains("pending_max") || script.contains("pendingMax"));
        CLAPP_CHECK(script.contains("options_done") || script.contains("optionsDone"));
        CLAPP_CHECK(script.contains("pending_hyphen") || script.contains("pendingHyphen"));
        CLAPP_CHECK(script.contains("pending_negative") || script.contains("pendingNegative"));
        const auto path_script = clapp::generate_completion(paths, sh);
        CLAPP_CHECK(path_script.contains("_files -/") || path_script.contains("compgen -d") ||
                    path_script.contains("__fish_complete_directories") || path_script.contains("Get-ChildItem -Directory"));
    }
    const auto multiline = clapp::generate_completion(command, clapp::shell::zsh, "app\necho injected\n#");
    CLAPP_CHECK(multiline.starts_with("# zsh completion\n"));
    CLAPP_CHECK(!multiline.starts_with("#compdef"));
}

#ifndef _WIN32
CLAPP_TEST("bash generated completion executes nested contexts and value aliases") {
    // All generated files remain in the build tree; no /tmp or global state.
    const auto path = std::filesystem::current_path() / "clapp_completion_runtime_test.sh";
    {
        std::ofstream script(path);
        script << clapp::generate_completion(command, clapp::shell::bash);
        script << R"CLAPP(
COMP_WORDS=(app --speed f); COMP_CWORD=2
_clapp_617070
[[ ${COMPREPLY[*]} == fast ]] || exit 1
COMP_WORDS=(app run --o); COMP_CWORD=2
_clapp_617070
[[ ${COMPREPLY[*]} == --output ]] || exit 2
COMP_WORDS=(app run nested --h); COMP_CWORD=3
_clapp_617070
[[ ${COMPREPLY[*]} == --help ]] || exit 3
COMP_WORDS=(app --mode run ''); COMP_CWORD=3
_clapp_617070
[[ " ${COMPREPLY[*]} " == *" --mode "* ]] || exit 4
[[ " ${COMPREPLY[*]} " != *" --hidden "* ]] || exit 5
COMP_WORDS=(app --mode=fa); COMP_CWORD=1
_clapp_617070
[[ ${COMPREPLY[*]} == --mode=fast ]] || exit 6
COMP_WORDS=(app --mode value); COMP_CWORD=2
_clapp_617070
[[ ${#COMPREPLY[@]} == 1 && ${COMPREPLY[0]} == 'value with space' ]] || exit 7
COMP_WORDS=(app --mode a); COMP_CWORD=2
_clapp_617070
[[ ${COMPREPLY[0]} == "a'\$(false)" ]] || exit 8
COMP_WORDS=(app --pair first run --mo); COMP_CWORD=4
_clapp_617070
[[ ${COMPREPLY[*]} == --mode ]] || exit 9
COMP_WORDS=(app --pair first f); COMP_CWORD=3
_clapp_617070
[[ ${COMPREPLY[*]} == fast ]] || exit 10
COMP_WORDS=(app --pair=first run --mo); COMP_CWORD=3
_clapp_617070
[[ ${COMPREPLY[*]} == --mode ]] || exit 11
COMP_WORDS=(app --many first run --mo); COMP_CWORD=4
_clapp_617070
[[ ${COMPREPLY[*]} == --mode ]] || exit 12
COMP_WORDS=(app -- run --o); COMP_CWORD=3
_clapp_617070
[[ ${#COMPREPLY[@]} == 0 ]] || exit 13
COMP_WORDS=(app -- --mo); COMP_CWORD=2
_clapp_617070
[[ ${#COMPREPLY[@]} == 0 ]] || exit 14
COMP_WORDS=(app --user=roo); COMP_CWORD=1
_clapp_617070
for reply in "${COMPREPLY[@]}"; do [[ $reply == --user=roo* ]] || exit 15; done
COMP_WORDS=(app --url=ht); COMP_CWORD=1
_clapp_617070
[[ ${COMPREPLY[*]} == '--url=https:// --url=http://' ]] || exit 16
COMP_WORDS=(app --negative -3 r); COMP_CWORD=3
_clapp_617070
[[ ${#COMPREPLY[@]} == 0 ]] || exit 17
COMP_WORDS=(app --negative -3 run --mo); COMP_CWORD=4
_clapp_617070
[[ ${COMPREPLY[*]} == --mode ]] || exit 18
COMP_WORDS=(app --literal --literal run --mo); COMP_CWORD=4
_clapp_617070
[[ ${COMPREPLY[*]} == --mode ]] || exit 19
COMP_WORDS=(app --literal --literal run run --o); COMP_CWORD=5
_clapp_617070
[[ ${COMPREPLY[*]} == --output ]] || exit 20
COMP_WORDS=(app --literal -- run --mo); COMP_CWORD=4
_clapp_617070
[[ ${COMPREPLY[*]} == --mode ]] || exit 21
)CLAPP";
    }
    const auto command_line = "bash " + clapp::detail::completion_quote(path.string(), clapp::shell::bash);
    const auto status = std::system(command_line.c_str());
    std::filesystem::remove(path);
    CLAPP_CHECK(status == 0);
}
#endif
