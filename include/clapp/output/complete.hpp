/** \file \brief Shell completion generation from a frozen command tree. */
#pragma once

#include <clapp/builder/command.hpp>
#include <clapp/builder/value_hint.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clapp {
    enum class shell : unsigned char { bash, zsh, fish, powershell };

    namespace detail {
        inline std::string completion_quote(std::string_view value, shell sh) {
            std::string out = "'";
            for (char c : value) {
                if (c == '\'') out += sh == shell::powershell ? "''" : "'\\''";
                else if (c == '\\' && sh == shell::fish) out += "\\\\";
                else out += c;
            }
            return out + "'";
        }

        struct completion_context {
            const command_spec* command;
            std::size_t parent;
        };
        inline void completion_collect(const command_spec& spec, std::size_t parent,
                                       std::vector<completion_context>& contexts) {
            const auto current = contexts.size();
            contexts.push_back({&spec, parent});
            for (const auto& child : spec.get_subcommands()) {
                if (!child.is_hide_set()) completion_collect(child, current, contexts);
            }
        }
        inline std::vector<std::string> completion_names(const arg_spec& arg) {
            std::vector<std::string> names;
            if (auto name = arg.get_long()) names.push_back("--" + std::string(*name));
            if (auto name = arg.get_short()) names.push_back("-" + std::string(1, *name));
            for (const auto& alias : arg.get_all_aliases()) names.push_back("--" + std::string(alias.name.name()));
            for (const auto& alias : arg.get_all_short_aliases()) names.push_back("-" + std::string(1, alias.name));
            return names;
        }
        inline std::vector<std::string> completion_names(const command_spec& spec) {
            std::vector<std::string> names{std::string(spec.get_name())};
            for (const auto& alias : spec.get_all_aliases()) names.emplace_back(alias.name.name());
            if (auto flag = spec.get_long_flag()) names.push_back("--" + std::string(*flag));
            if (auto flag = spec.get_short_flag()) names.push_back("-" + std::string(1, *flag));
            for (const auto& alias : spec.get_all_long_flag_aliases()) names.push_back("--" + std::string(alias.name.name()));
            for (const auto& alias : spec.get_all_short_flag_aliases()) names.push_back("-" + std::string(1, alias.name));
            return names;
        }
        inline std::vector<std::string> completion_values(const arg_spec& arg) {
            std::vector<std::string> values;
            if (!arg.is_hide_possible_values_set()) {
                for (const auto& value : arg.get_possible_values()) {
                    if (!value.is_hide_set()) values.emplace_back(value.get_name());
                }
            }
            return values;
        }
        inline std::string completion_list(const std::vector<std::string>& values, shell sh) {
            std::string out;
            for (const auto& value : values) out += completion_quote(value, sh) + " ";
            return out;
        }
        inline std::string completion_symbol(std::string_view name) {
            // Encode bytes, rather than interpolating command names as shell code.
            constexpr char hex[] = "0123456789abcdef";
            std::string out = "_clapp_";
            for (char character : name) {
                const auto c = static_cast<unsigned char>(character);
                out += hex[c >> 4];
                out += hex[c & 15];
            }
            return out;
        }
        inline std::string completion_hint(value_hint hint, shell sh) {
            if (sh == shell::bash) {
                switch (hint) {
                case value_hint::dir_path: return "while IFS= read -r candidate; do COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -d -- \"$cur\"); return 0";
                case value_hint::any_path:
                case value_hint::file_path: return "while IFS= read -r candidate; do COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -f -- \"$cur\"); return 0";
                case value_hint::executable_path: return "while IFS= read -r candidate; do [[ -x $candidate ]] && COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -f -- \"$cur\"); return 0";
                case value_hint::command_name:
                case value_hint::command_string:
                case value_hint::command_with_arguments: return "while IFS= read -r candidate; do COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -c -- \"$cur\"); return 0";
                case value_hint::username: return "while IFS= read -r candidate; do COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -u -- \"$cur\"); return 0";
                case value_hint::hostname: return "while IFS= read -r candidate; do COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -A hostname -- \"$cur\"); return 0";
                case value_hint::url: return "while IFS= read -r candidate; do COMPREPLY+=(\"$attached_prefix$candidate\"); done < <(compgen -W 'https:// http:// ftp://' -- \"$cur\"); return 0";
                default: return "return";
                }
            }
            if (sh == shell::zsh) {
                switch (hint) {
                case value_hint::dir_path: return "_files -/; return";
                case value_hint::any_path:
                case value_hint::file_path: return "_files; return";
                case value_hint::executable_path: return "_files -g '*(-*)'; return";
                case value_hint::command_name:
                case value_hint::command_string:
                case value_hint::command_with_arguments: return "_command_names; return";
                case value_hint::username: return "_users; return";
                case value_hint::hostname: return "_hosts; return";
                case value_hint::url: return "_urls; return";
                default: return "return";
                }
            }
            return {};
        }

        inline std::string completion_arity(std::size_t bound) {
            return bound == value_range::unbounded ? "-1" :
                    std::to_string(std::min(bound, std::size_t{2147483647}));
        }

        inline std::string completion_posix(const std::vector<completion_context>& contexts,
                                            std::string_view bin, shell sh) {
            const bool zsh = sh == shell::zsh;
            const auto symbol = completion_symbol(bin);
            const auto quote = [sh](std::string_view value) { return completion_quote(value, sh); };
            std::string out = zsh ? "# zsh completion\n" : "# bash completion\n";
            out += symbol + "() {\n  local cur prev word token candidate state=0 pending_min=0 pending_max=0 pending_hyphen=0 pending_negative=0 options_done=0 is_subcommand=0 attached=0 i attached_prefix='' value_key='' negative_pattern='^-[0-9]+([.][0-9]*)?([eE][0-9]+)?$'\n  local -a candidates\n";
            out += zsh ? "  cur=${words[CURRENT]}; prev=${words[CURRENT-1]}\n"
                       : "  local -a words=(\"${COMP_WORDS[@]}\"); local CURRENT=$COMP_CWORD\n  cur=${words[CURRENT]}; prev=${words[CURRENT-1]}; COMPREPLY=()\n";
            out += "  for ((i=" + std::string(zsh ? "2" : "1") + "; i<CURRENT; ++i)); do\n    word=${words[i]}\n    ((options_done)) && continue\n    if [[ $word == -- ]] && ((pending_max == 0 || !pending_hyphen)); then options_done=1; pending_min=0; pending_max=0; value_key=''; continue; fi\n    is_subcommand=0\n    case \"$state:$word\" in\n";
            for (std::size_t i = 1; i < contexts.size(); ++i) {
                if (!contexts[contexts[i].parent].command->is_subcommand_precedence_over_arg_set()) continue;
                for (const auto& name : completion_names(*contexts[i].command))
                    out += "      " + quote(std::to_string(contexts[i].parent) + ":" + name) + ") is_subcommand=1 ;;\n";
            }
            out += "    esac\n    if ((pending_max != 0 && !is_subcommand)) && { [[ $word != -* || $word == - ]] || ((pending_hyphen)) || { ((pending_negative)) && [[ $word =~ $negative_pattern ]]; }; }; then\n      ((pending_min > 0)) && ((--pending_min))\n      ((pending_max > 0)) && ((--pending_max))\n      ((pending_max == 0)) && value_key=''\n      continue\n    fi\n    pending_min=0; pending_max=0; pending_hyphen=0; pending_negative=0; value_key=''; attached=0; token=$word\n    if [[ $word == --*=* ]]; then token=${word%%=*}; attached=1; fi\n    case \"$state:$token\" in\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (!arg.is_takes_value_set() || arg.is_positional()) continue;
                    for (const auto& name : completion_names(arg)) {
                        const auto key = quote(std::to_string(i) + ":" + name);
                        out += "      " + key + ") pending_min=" + completion_arity(arg.get_min_vals()) + "; pending_max=" + completion_arity(arg.get_max_vals()) + "; pending_hyphen=" + ((arg.is_allow_hyphen_values_set() || contexts[i].command->is_allow_hyphen_values_set()) ? "1" : "0") + "; pending_negative=" + ((arg.is_allow_negative_numbers_set() || contexts[i].command->is_allow_negative_numbers_set()) ? "1" : "0") + "; value_key=" + key + " ;;\n";
                    }
                }
            }
            for (std::size_t i = 1; i < contexts.size(); ++i) {
                for (const auto& name : completion_names(*contexts[i].command))
                    out += "      " + quote(std::to_string(contexts[i].parent) + ":" + name) + ") state=" + std::to_string(i) + " ;;\n";
            }
            out += "    esac\n    if ((attached)); then\n      ((pending_min > 0)) && ((--pending_min))\n      ((pending_max > 0)) && ((--pending_max))\n      ((pending_max == 0)) && value_key=''\n    fi\n  done\n  if [[ $cur == -* ]] && ! ((pending_max != 0 && pending_hyphen)) && ! { ((pending_max != 0 && pending_negative)) && [[ $cur =~ $negative_pattern ]]; }; then value_key=''; fi\n  if ((!options_done && (pending_max == 0 || !pending_hyphen))) && [[ $cur == --*=* ]]; then prev=${cur%%=*}; attached_prefix=\"${prev}=\"; cur=${cur#*=}; value_key=\"$state:$prev\"; fi\n";
            if (zsh) out += "  if [[ -n $attached_prefix ]]; then compset -P '*='; fi\n";
            out += "  case $value_key in\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (!arg.is_takes_value_set() || arg.is_positional() || arg.is_hide_set()) continue;
                    const auto values = completion_values(arg);
                    for (const auto& name : completion_names(arg)) {
                        out += "    " + quote(std::to_string(i) + ":" + name) + ") ";
                        if (values.empty()) out += completion_hint(arg.get_value_hint(), sh);
                        else {
                            out += "candidates=(" + completion_list(values, sh) + "); ";
                            out += zsh ? "compadd -- \"${candidates[@]}\"; return 0" : "for candidate in \"${candidates[@]}\"; do [[ $candidate == \"$cur\"* ]] && COMPREPLY+=(\"$attached_prefix$candidate\"); done; return 0";
                        }
                        out += " ;;\n";
                    }
                }
            }
            out += "  esac\n  case $state in\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                std::vector<std::string> named;
                std::vector<std::string> positional;
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (arg.is_hide_set()) continue;
                    auto names = completion_names(arg);
                    named.insert(named.end(), names.begin(), names.end());
                    if (arg.is_positional()) {
                        auto possible = completion_values(arg);
                        positional.insert(positional.end(), possible.begin(), possible.end());
                    }
                }
                for (const auto& child : contexts[i].command->get_subcommands()) {
                    if (child.is_hide_set()) continue;
                    auto names = completion_names(child);
                    named.insert(named.end(), names.begin(), names.end());
                }
                out += "    " + std::to_string(i) + ") candidates=(" + completion_list(positional, sh) + "); if ((!options_done)); then candidates+=(" + completion_list(named, sh) + "); fi ;;\n";
            }
            out += "  esac\n  if [[ $cur != -* ]] || ((options_done)); then\n    case $state in\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (arg.is_positional() && !arg.is_hide_set() && completion_values(arg).empty() &&
                        arg.get_value_hint() != value_hint::unknown && arg.get_value_hint() != value_hint::other) {
                        out += "      " + std::to_string(i) + ") " + completion_hint(arg.get_value_hint(), sh) + " ;;\n";
                        break;
                    }
                }
            }
            out += "    esac\n  fi\n";
            out += zsh ? "  compadd -- \"${candidates[@]}\"\n}\ncompdef " + symbol + " " + quote(bin) + "\n"
                       : "  for candidate in \"${candidates[@]}\"; do [[ $candidate == \"$cur\"* ]] && COMPREPLY+=(\"$attached_prefix$candidate\"); done\n  return 0\n}\ncomplete -F " + symbol + " -- " + quote(bin) + "\n";
            return out;
        }

        inline std::string completion_fish(const std::vector<completion_context>& contexts, std::string_view bin) {
            const auto symbol = completion_symbol(bin);
            const auto quote = [](std::string_view value) { return completion_quote(value, shell::fish); };
            std::string out = "# fish completion\nfunction " + symbol + "\n  set -l state 0\n  set -l pending_min 0\n  set -l pending_max 0\n  set -l pending_hyphen 0\n  set -l pending_negative 0\n  set -l options_done 0\n  set -l value_key ''\n  set -l words (commandline -opc)\n  for word in $words[2..-1]\n    if test $options_done = 1\n      continue\n    end\n    if test \"$word\" = --; and begin; test $pending_max = 0; or test $pending_hyphen = 0; end\n      set options_done 1\n      set pending_min 0\n      set pending_max 0\n      set value_key ''\n      continue\n    end\n    set -l is_subcommand 0\n    switch \"$state:$word\"\n";
            for (std::size_t i = 1; i < contexts.size(); ++i) {
                if (!contexts[contexts[i].parent].command->is_subcommand_precedence_over_arg_set()) continue;
                for (const auto& name : completion_names(*contexts[i].command))
                    out += "      case " + quote(std::to_string(contexts[i].parent) + ":" + name) + "\n        set is_subcommand 1\n";
            }
            out += "    end\n    set -l accepts_value 0\n    if not string match -q -- '-*' \"$word\"; or test \"$word\" = -; or test $pending_hyphen = 1\n      set accepts_value 1\n    else if test $pending_negative = 1; and string match -rq -- '^-[0-9]+([.][0-9]*)?([eE][0-9]+)?$' \"$word\"\n      set accepts_value 1\n    end\n    if test $pending_max != 0; and test $is_subcommand = 0; and test $accepts_value = 1\n      if test $pending_min -gt 0\n        set pending_min (math $pending_min - 1)\n      end\n      if test $pending_max -gt 0\n        set pending_max (math $pending_max - 1)\n      end\n      if test $pending_max = 0\n        set value_key ''\n      end\n      continue\n    end\n    set pending_min 0\n    set pending_max 0\n    set pending_hyphen 0\n    set pending_negative 0\n    set value_key ''\n    set -l attached 0\n    set -l token \"$word\"\n    if string match -q -- '--*=*' \"$word\"\n      set token (string split -m 1 '=' -- \"$word\")[1]\n      set attached 1\n    end\n    switch \"$state:$token\"\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (!arg.is_takes_value_set() || arg.is_positional()) continue;
                    for (const auto& name : completion_names(arg)) {
                        const auto key = quote(std::to_string(i) + ":" + name);
                        out += "      case " + key + "\n        set pending_min " + completion_arity(arg.get_min_vals()) + "\n        set pending_max " + completion_arity(arg.get_max_vals()) + "\n        set pending_hyphen " + ((arg.is_allow_hyphen_values_set() || contexts[i].command->is_allow_hyphen_values_set()) ? "1" : "0") + "\n        set pending_negative " + ((arg.is_allow_negative_numbers_set() || contexts[i].command->is_allow_negative_numbers_set()) ? "1" : "0") + "\n        set value_key " + key + "\n";
                    }
                }
            }
            for (std::size_t i = 1; i < contexts.size(); ++i) {
                for (const auto& name : completion_names(*contexts[i].command))
                    out += "      case " + quote(std::to_string(contexts[i].parent) + ":" + name) + "\n        set state " + std::to_string(i) + "\n";
            }
            out += "    end\n    if test $attached = 1\n      if test $pending_min -gt 0\n        set pending_min (math $pending_min - 1)\n      end\n      if test $pending_max -gt 0\n        set pending_max (math $pending_max - 1)\n      end\n      if test $pending_max = 0\n        set value_key ''\n      end\n    end\n  end\n  test $state = $argv[1]; or return 1\n  set -l current (commandline -ct)\n  set -l hyphen_value 0\n  if test $pending_max != 0\n    if test $pending_hyphen = 1\n      set hyphen_value 1\n    else if test $pending_negative = 1; and string match -rq -- '^-[0-9]+([.][0-9]*)?([eE][0-9]+)?$' \"$current\"\n      set hyphen_value 1\n    end\n  end\n  switch $argv[2]\n    case named\n      test $options_done = 0; or return 1\n      test $hyphen_value = 0; or return 1\n      test -z \"$value_key\"; or string match -q -- '-*' \"$current\"\n    case value\n      contains -- \"$value_key\" $argv[3..-1]\n    case positional\n      test -z \"$value_key\"\n  end\nend\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                const auto context = symbol + " " + std::to_string(i);
                const auto base = "complete -c " + quote(bin);
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (arg.is_hide_set()) continue;
                    std::string flags;
                    std::string active_names;
                    for (const auto& name : completion_names(arg)) {
                        if (name.starts_with("--")) flags += " -l " + quote(std::string_view(name).substr(2));
                        else flags += " -s " + quote(std::string_view(name).substr(1));
                        active_names += " " + quote(std::to_string(i) + ":" + name);
                    }
                    std::string suffix;
                    const auto values = completion_values(arg);
                    if (!values.empty()) suffix = " -f -a " + quote(completion_list(values, shell::fish));
                    else {
                        switch (arg.get_value_hint()) {
                        case value_hint::dir_path: suffix = " -f -a '(__fish_complete_directories)'"; break;
                        case value_hint::file_path:
                        case value_hint::any_path: suffix = " -F"; break;
                        case value_hint::executable_path:
                        case value_hint::command_name:
                        case value_hint::command_string:
                        case value_hint::command_with_arguments: suffix = " -f -a '(__fish_complete_command)'"; break;
                        case value_hint::username: suffix = " -f -a '(__fish_complete_users)'"; break;
                        case value_hint::hostname: suffix = " -f -a '(__fish_print_hostnames)'"; break;
                        case value_hint::url: suffix = " -f -a 'https:// http:// ftp://'"; break;
                        default: suffix = " -f"; break;
                        }
                    }
                    if (auto help = arg.get_help()) suffix += " -d " + quote(*help);
                    if (arg.is_positional()) out += base + " -n " + quote(context + " positional") + suffix + "\n";
                    else {
                        out += base + " -n " + quote(context + " named") + flags + (arg.is_takes_value_set() ? " -r" : "") + suffix + "\n";
                        if (arg.is_takes_value_set()) out += base + " -n " + quote(context + " value" + active_names) + suffix + "\n";
                    }
                }
                for (const auto& child : contexts[i].command->get_subcommands()) {
                    if (child.is_hide_set()) continue;
                    for (const auto& name : completion_names(child)) out += base + " -n " + quote(context + " named") + " -f -a " + quote(quote(name)) + "\n";
                }
            }
            return out;
        }

        inline std::string completion_powershell_hint(value_hint hint) {
            switch (hint) {
            case value_hint::dir_path:
                return "$values += @(Get-ChildItem -Directory -Name -ErrorAction SilentlyContinue)";
            case value_hint::any_path:
            case value_hint::file_path:
                return "$values += @(Get-ChildItem -Name -ErrorAction SilentlyContinue)";
            case value_hint::executable_path:
            case value_hint::command_name:
            case value_hint::command_string:
            case value_hint::command_with_arguments:
                return "$values += @(Get-Command -Name ($wordToComplete + '*') -ErrorAction SilentlyContinue | ForEach-Object Name)";
            case value_hint::username:
                return "if (Get-Command Get-LocalUser -ErrorAction SilentlyContinue) { $values += @(Get-LocalUser | ForEach-Object Name) }";
            case value_hint::hostname: return "$values += @([System.Net.Dns]::GetHostName())";
            case value_hint::url: return "$values += @('https://', 'http://', 'ftp://')";
            default: return "$null = $null";
            }
        }

        inline std::string completion_powershell(const std::vector<completion_context>& contexts, std::string_view bin) {
            const auto quote = [](std::string_view value) { return completion_quote(value, shell::powershell); };
            const auto array = [&](const std::vector<std::string>& names) {
                std::string result = "@(";
                bool first = true;
                for (const auto& name : names) {
                    if (!first) result += ", ";
                    result += quote(name); first = false;
                }
                return result + ")";
            };
            std::string out = "# PowerShell completion\nRegister-ArgumentCompleter -Native -CommandName " + quote(bin) + " -ScriptBlock {\n  param($wordToComplete, $commandAst, $cursorPosition)\n  $state = 0; $pendingMin = 0; $pendingMax = 0; $pendingHyphen = $false; $pendingNegative = $false; $optionsDone = $false; $valueKey = ''\n  $elements = @($commandAst.CommandElements | Select-Object -Skip 1)\n  foreach ($element in $elements) {\n    if ($element.Extent.StartOffset -ge $cursorPosition - $wordToComplete.Length) { break }\n    $word = $element.Extent.Text.Trim([char]39, [char]34)\n    if ($optionsDone) { continue }\n    if ($word -ceq '--' -and ($pendingMax -eq 0 -or -not $pendingHyphen)) { $optionsDone = $true; $pendingMin = 0; $pendingMax = 0; $valueKey = ''; continue }\n    $isSubcommand = $false\n    switch -CaseSensitive (\"${state}:$word\") {\n";
            for (std::size_t i = 1; i < contexts.size(); ++i) {
                if (!contexts[contexts[i].parent].command->is_subcommand_precedence_over_arg_set()) continue;
                for (const auto& name : completion_names(*contexts[i].command))
                    out += "      " + quote(std::to_string(contexts[i].parent) + ":" + name) + " { $isSubcommand = $true; break }\n";
            }
            out += "    }\n    if ($pendingMax -ne 0 -and -not $isSubcommand -and (-not $word.StartsWith('-') -or $word -ceq '-' -or $pendingHyphen -or ($pendingNegative -and $word -cmatch '^-[0-9]+([.][0-9]*)?([eE][0-9]+)?$'))) {\n      if ($pendingMin -gt 0) { --$pendingMin }\n      if ($pendingMax -gt 0) { --$pendingMax }\n      if ($pendingMax -eq 0) { $valueKey = '' }\n      continue\n    }\n    $pendingMin = 0; $pendingMax = 0; $pendingHyphen = $false; $pendingNegative = $false; $valueKey = ''; $attached = $false; $token = $word\n    if ($word.StartsWith('--') -and $word.Contains('=')) { $token = $word.Split('=', 2)[0]; $attached = $true }\n    switch -CaseSensitive (\"${state}:$token\") {\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (!arg.is_takes_value_set() || arg.is_positional()) continue;
                    for (const auto& name : completion_names(arg)) {
                        const auto key = quote(std::to_string(i) + ":" + name);
                        out += "      " + key + " { $pendingMin = " + completion_arity(arg.get_min_vals()) + "; $pendingMax = " + completion_arity(arg.get_max_vals()) + "; $pendingHyphen = " + ((arg.is_allow_hyphen_values_set() || contexts[i].command->is_allow_hyphen_values_set()) ? "$true" : "$false") + "; $pendingNegative = " + ((arg.is_allow_negative_numbers_set() || contexts[i].command->is_allow_negative_numbers_set()) ? "$true" : "$false") + "; $valueKey = " + key + "; break }\n";
                    }
                }
            }
            for (std::size_t i = 1; i < contexts.size(); ++i) {
                for (const auto& name : completion_names(*contexts[i].command))
                    out += "      " + quote(std::to_string(contexts[i].parent) + ":" + name) + " { $state = " + std::to_string(i) + "; break }\n";
            }
            out += "    }\n    if ($attached) {\n      if ($pendingMin -gt 0) { --$pendingMin }\n      if ($pendingMax -gt 0) { --$pendingMax }\n      if ($pendingMax -eq 0) { $valueKey = '' }\n    }\n  }\n  $completionPrefix = ''\n  if ($wordToComplete.StartsWith('-') -and -not ($pendingMax -ne 0 -and ($pendingHyphen -or ($pendingNegative -and $wordToComplete -cmatch '^-[0-9]+([.][0-9]*)?([eE][0-9]+)?$')))) { $valueKey = '' }\n  if (-not $optionsDone -and ($pendingMax -eq 0 -or -not $pendingHyphen) -and $wordToComplete.StartsWith('--') -and $wordToComplete.Contains('=')) { $parts = $wordToComplete.Split('=', 2); $valueKey = \"${state}:$($parts[0])\"; $completionPrefix = $parts[0] + '='; $wordToComplete = $parts[1] }\n  $values = @(); $valueContext = $false\n  switch -CaseSensitive ($valueKey) {\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (!arg.is_takes_value_set() || arg.is_positional() || arg.is_hide_set()) continue;
                    const auto values = completion_values(arg);
                    for (const auto& name : completion_names(arg)) {
                        out += "    " + quote(std::to_string(i) + ":" + name) + " { $valueContext = $true; $values = " + array(values) + "; ";
                        if (values.empty()) out += completion_powershell_hint(arg.get_value_hint());
                        else out += "$null = $null";
                        out += "; break }\n";
                    }
                }
            }
            out += "  }\n  if (-not $valueContext) {\n    switch ($state) {\n";
            for (std::size_t i = 0; i < contexts.size(); ++i) {
                std::vector<std::string> names;
                std::vector<std::string> positional;
                const arg_spec* positional_hint = nullptr;
                for (const auto& arg : contexts[i].command->get_arguments()) {
                    if (arg.is_hide_set()) continue;
                    auto more = completion_names(arg);
                    names.insert(names.end(), more.begin(), more.end());
                    if (arg.is_positional()) {
                        more = completion_values(arg);
                        positional.insert(positional.end(), more.begin(), more.end());
                        if (positional_hint == nullptr && more.empty() && arg.get_value_hint() != value_hint::unknown &&
                            arg.get_value_hint() != value_hint::other) positional_hint = &arg;
                    }
                }
                for (const auto& child : contexts[i].command->get_subcommands()) {
                    if (child.is_hide_set()) continue;
                    auto more = completion_names(child);
                    names.insert(names.end(), more.begin(), more.end());
                }
                out += "      " + std::to_string(i) + " { $values = " + array(positional) + "; if (-not $optionsDone) { $values += " + array(names) + " }; ";
                if (positional_hint != nullptr) out += "if ($optionsDone -or -not $wordToComplete.StartsWith('-')) { " + completion_powershell_hint(positional_hint->get_value_hint()) + " }; ";
                out += "break }\n";
            }
            out += "    }\n  }\n  $values | Where-Object { $_.StartsWith($wordToComplete, [System.StringComparison]::OrdinalIgnoreCase) } | ForEach-Object {\n    $text = $completionPrefix + $_\n    if ($text -match '[\\s\\x27\\x22`$;|&(){}]') { $text = [char]39 + $text.Replace(\"'\", \"''\") + [char]39 }\n    [System.Management.Automation.CompletionResult]::new($text, $_, 'ParameterValue', $_)\n  }\n}\n";
            return out;
        }

    }

    /** Generate an escaped, standalone completion script for the command tree. */
    [[nodiscard]] inline std::string generate_completion(const command_spec& spec, shell sh,
                                                         std::string_view bin_name = {}) {
        if (bin_name.empty()) bin_name = spec.get_bin_name().value_or(spec.get_name());
        std::vector<detail::completion_context> contexts;
        detail::completion_collect(spec, 0, contexts);
        switch (sh) {
        case shell::bash:
        case shell::zsh: return detail::completion_posix(contexts, bin_name, sh);
        case shell::fish: return detail::completion_fish(contexts, bin_name);
        case shell::powershell: return detail::completion_powershell(contexts, bin_name);
        }
        return {};
    }
}
