add_library(clapp_test_support INTERFACE)
add_library(clapp::test_support ALIAS clapp_test_support)
target_include_directories(clapp_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if (CLAPP_USE_CATCH2)
    find_package(Catch2 3 QUIET)
    if (Catch2_FOUND)
        target_link_libraries(clapp_test_support INTERFACE Catch2::Catch2WithMain)
        target_compile_definitions(clapp_test_support INTERFACE CLAPP_USE_CATCH2=1)
    else ()
        message(WARNING "clapp: CLAPP_USE_CATCH2=ON but Catch2 3.x was not found; falling back to the built-in harness")
    endif ()
endif ()

clapp_add_unit_test(meta annotations)
clapp_add_unit_test(meta deduce)
clapp_add_unit_test(meta from_matches)
clapp_add_unit_test(meta parse)

clapp_add_unit_test(lex os_str)
clapp_add_unit_test(lex parsed_arg)
clapp_add_unit_test(lex short_flags)
clapp_add_unit_test(lex raw_args)

# GCC 16.1 emits a false-positive -Warray-bounds when -O3 inlines the third
# next_os() call in raw_args_test.cpp, even though peek_os() checks the index.
# Keep the warning visible while preventing this one compiler bug from failing
# the Release build; all other diagnostics remain errors.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16.1
        AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.2)
    target_compile_options(clapp_units_lex_raw_args PRIVATE
            "$<$<CONFIG:Release>:-Wno-error=array-bounds>")
endif ()

clapp_add_unit_test(util flat_map)
clapp_add_unit_test(util flat_set)
clapp_add_unit_test(util graph)
clapp_add_unit_test(util id)
clapp_add_unit_test(util str)
clapp_add_unit_test(util any_value)

clapp_add_unit_test(builder action)
clapp_add_unit_test(builder value_range)
clapp_add_unit_test(builder value_hint)
clapp_add_unit_test(builder possible_value)
clapp_add_unit_test(builder value_parser)
clapp_add_unit_test(builder styling)
clapp_add_unit_test(builder arg)
clapp_add_unit_test(builder arg_group)
clapp_add_unit_test(builder command)

clapp_add_unit_test(builder conformance_build_contracts)

clapp_add_unit_test(output styled_str)
clapp_add_unit_test(output textwrap)
clapp_add_unit_test(output render)
clapp_add_unit_test(output usage)
clapp_add_unit_test(output help)

clapp_add_unit_test(error error_kind)
clapp_add_unit_test(error context)
clapp_add_unit_test(error error)

clapp_add_unit_test(parser value_source)
clapp_add_unit_test(parser matched_arg)
clapp_add_unit_test(parser arg_matches)
clapp_add_unit_test(parser arg_matcher)
clapp_add_unit_test(parser parse)
clapp_add_unit_test(parser validator)

clapp_add_unit_test(parser conformance_action)
clapp_add_unit_test(parser conformance_app_settings)
clapp_add_unit_test(parser conformance_arg_aliases)
clapp_add_unit_test(parser conformance_arg_aliases_short)
clapp_add_unit_test(parser conformance_arg_matches)
clapp_add_unit_test(parser conformance_conflicts)
clapp_add_unit_test(parser conformance_default_missing_vals)
clapp_add_unit_test(parser conformance_default_vals)
clapp_add_unit_test(parser conformance_delimiters)
clapp_add_unit_test(parser conformance_derive_order)
clapp_add_unit_test(parser conformance_display_order)
clapp_add_unit_test(parser conformance_double_require)
clapp_add_unit_test(parser conformance_empty_values)
clapp_add_unit_test(parser conformance_env)
clapp_add_unit_test(parser conformance_error)
clapp_add_unit_test(parser conformance_flag_subcommands)
clapp_add_unit_test(parser conformance_flags)
clapp_add_unit_test(parser conformance_global_args)
clapp_add_unit_test(parser conformance_groups)
clapp_add_unit_test(parser conformance_help)

clapp_add_unit_test(parser conformance_help_complex)
clapp_add_unit_test(parser conformance_help_env)
clapp_add_unit_test(parser conformance_help_flatten)
clapp_add_unit_test(parser conformance_help_global_args)
clapp_add_unit_test(parser conformance_help_help_flag)
clapp_add_unit_test(parser conformance_help_help_required)
clapp_add_unit_test(parser conformance_help_last_arg)
clapp_add_unit_test(parser conformance_help_misc)
clapp_add_unit_test(parser conformance_help_override)
clapp_add_unit_test(parser conformance_help_parent_cmd_req)
clapp_add_unit_test(parser conformance_help_positional)
clapp_add_unit_test(parser conformance_help_subcommand)
clapp_add_unit_test(parser conformance_help_try_help)
clapp_add_unit_test(parser conformance_help_wrapping)
clapp_add_unit_test(parser conformance_hidden_args)
clapp_add_unit_test(parser conformance_ignore_errors)
clapp_add_unit_test(parser conformance_indices)
clapp_add_unit_test(parser conformance_multiple_occurrences)
clapp_add_unit_test(parser conformance_multiple_values)
clapp_add_unit_test(parser conformance_occurrences)
clapp_add_unit_test(parser conformance_opts)
clapp_add_unit_test(parser conformance_positionals)
clapp_add_unit_test(parser conformance_posix_compatible)
clapp_add_unit_test(parser conformance_possible_values)
clapp_add_unit_test(parser conformance_propagate_globals)
clapp_add_unit_test(parser conformance_require)
clapp_add_unit_test(parser conformance_subcommands)
clapp_add_unit_test(parser conformance_template_help)
clapp_add_unit_test(parser conformance_tests)
clapp_add_unit_test(parser conformance_unicode)
clapp_add_unit_test(parser conformance_utf8)
clapp_add_unit_test(parser conformance_version)

clapp_add_multi_tu_test(umbrella umbrella_test.cpp umbrella_second_tu.cpp)

clapp_add_compile_fail_test(meta no_value_parser
        PATTERN "field 'payload' of type '[^']*' has no value_parser")

clapp_add_compile_fail_test(meta duplicate_long_option
        PATTERN "the fields 'dry_run' and 'mode' both derive the long option 'dry-run'")

clapp_add_compile_fail_test(meta duplicate_short_option
        PATTERN "the fields 'name' and 'number' both derive the short option 'n'")

clapp_add_compile_fail_test(meta flatten_on_non_aggregate
        PATTERN "field 'not_a_struct' of type 'int' carries .*clapp::flatten")

# No field name available on either compiler; see the snippet's header comment.
clapp_add_compile_fail_test(meta subcommand_on_non_variant
        PATTERN "clapp::subcommand.* needs a std::variant")

clapp_add_compile_fail_test(meta group_names_unknown_arg
        PATTERN_GNU "the group 'mode' lists 'nonexistent', which is not an argument"
        PATTERN_CLANG "lists '.*, which is not an argument of this command")

clapp_add_compile_fail_test(meta positional_index_gap
        PATTERN_GNU "'source' at 1, 'destination' at 3.*the indices must be 1..n without gaps"
        PATTERN_CLANG "the indices must be 1..n without gaps")

clapp_add_compile_fail_test(meta positional_index_duplicate
        PATTERN_GNU "the positionals 'source' and 'destination' claim index 1"
        PATTERN_CLANG "the positionals '.* claim index ")

clapp_add_compile_fail_test(meta monostate_alternative
        PATTERN_GNU "std::monostate is not supported as a std::variant alternative"
        PATTERN_CLANG "std::monostate is not supported as a std::variant")

clapp_add_compile_fail_test(meta external_subcommand_alternative
        PATTERN_GNU "clapp::external_subcommand.* on a std::variant alternative is not implemented"
        PATTERN_CLANG "clapp::external_subcommand.* on a std::variant")

clapp_add_compile_fail_test(meta negative_control EXPECT compiles)

clapp_add_compile_fail_test(builder action_num_args_conflict
        PATTERN_GNU "'mammal' has action 'set-true', which is incompatible with the num_args it was given"
        PATTERN_CLANG "which is incompatible with the num_args it was given")

clapp_add_compile_fail_test(builder conflicts_with_unknown_id
        PATTERN_GNU "'conflicts_with' on 'config' names 'extra', which is neither an argument nor a group"
        PATTERN_CLANG "require_id.*conflicts_with.,")

clapp_add_compile_fail_test(builder duplicate_arg_id
        PATTERN_GNU "command 'some': the id 'arg1' is claimed twice by the arguments"
        PATTERN_CLANG "the id '., clash->first.token, .' is claimed .*arguments and groups share one id namespace")

clapp_add_compile_fail_test(builder duplicate_group_id
        PATTERN_GNU "command 'group': the id 'req' is claimed twice by the groups"
        PATTERN_CLANG "the id '., clash->first.token, .' is claimed .*arguments and groups share one id namespace")

clapp_add_compile_fail_test(builder duplicate_long_option
        PATTERN_GNU "command 'some': '--long' is claimed by both the argument 'arg1' and the argument 'arg2'"
        PATTERN_CLANG "': '--., clash->first.token, .' is claimed by both")

clapp_add_compile_fail_test(builder duplicate_short_option
        PATTERN_GNU "command 'some': '-a' is claimed by both the argument 'arg1' and the argument 'arg2'"
        PATTERN_CLANG "': '-., std::string_view.*' is claimed by both")

clapp_add_compile_fail_test(builder duplicate_subcommand
        PATTERN_GNU "the subcommand name 'repeat' is declared twice"
        PATTERN_CLANG "the subcommand name .*is declared twice")

clapp_add_compile_fail_test(builder duplicate_subcommand_alias
        PATTERN_GNU "the subcommand 'unique' has an alias 'repeat' that is already the name of another subcommand"
        PATTERN_CLANG "that is already the name of another subcommand")

clapp_add_compile_fail_test(builder duplicate_subcommand_long_flag
        PATTERN_GNU "'--flag' is claimed by both the subcommand 'some' and the subcommand 'result'"
        PATTERN_CLANG "': '--., clash->first.token, .' is claimed by both")

clapp_add_compile_fail_test(builder duplicate_subcommand_short_flag
        PATTERN_GNU "'-f' is claimed by both the subcommand 'some' and the subcommand 'result'"
        PATTERN_CLANG "': '-., std::string_view.*' is claimed by both")

clapp_add_compile_fail_test(builder group_id_conflicts_with_arg
        PATTERN_GNU "command 'group': the id 'a' is claimed by both the argument and the group"
        PATTERN_CLANG "the id '., clash->first.token, .' is claimed .*arguments and groups share one id namespace")

clapp_add_compile_fail_test(builder help_expected_without_help
        PATTERN_GNU "command 'myapp'.*is set but the argument 'foo' has neither help"
        PATTERN_CLANG "has neither help.. nor long_help")

clapp_add_compile_fail_test(builder help_expected_subcommand_without_help
        PATTERN_GNU "command 'bar'.*is set but the argument 'delete' has neither help"
        PATTERN_CLANG "has neither help.. nor long_help")

clapp_add_compile_fail_test(builder mut_arg_undefined_id
        PATTERN_GNU "mut_arg: command 'myapp': argument 'typo' is undefined"
        PATTERN_CLANG "mut_arg: command .*argument .*is undefined")

clapp_add_compile_fail_test(builder mut_group_undefined_id
        PATTERN_GNU "mut_group: command 'myapp': group 'typo' is undefined"
        PATTERN_CLANG "mut_group: command .*group .*is undefined")

clapp_add_compile_fail_test(builder mut_subcommand_undefined_id
        PATTERN_GNU "mut_subcommand: command 'myapp': subcommand 'typo' is undefined"
        PATTERN_CLANG "mut_subcommand: command .*subcommand .*is undefined")

clapp_add_compile_fail_test(builder help_flag_id_conflict
        PATTERN_GNU "command 'conflict': the id 'help' is claimed twice by the arguments.*call disable_help_flag"
        PATTERN_CLANG "the id '., clash->first.token, .' is claimed .*arguments and groups share one id namespace")

clapp_add_compile_fail_test(builder help_flag_long_conflict
        PATTERN_GNU "'--help' is claimed by both the argument 'custom-help' and the argument 'help'.*call disable_help_flag"
        PATTERN_CLANG "': '--., clash->first.token, .' is claimed by both")

clapp_add_compile_fail_test(builder help_flag_short_conflict
        PATTERN_GNU "'-h' is claimed by both the argument 'home' and the argument 'help'.*call disable_help_flag"
        PATTERN_CLANG "': '-., std::string_view.*' is claimed by both")

clapp_add_compile_fail_test(builder long_option_leading_dash
        PATTERN_GNU "the long option of 'filename' is '--filename'.*write it without the leading"
        PATTERN_CLANG "the long option of .*write it without the leading")

clapp_add_compile_fail_test(builder low_index_positional_not_required
        PATTERN_GNU "'files' takes several values but is not the last positional.*make 'target' required"
        PATTERN_CLANG "takes several values but is not the last positional")

clapp_add_compile_fail_test(builder low_index_positional_too_far_back
        PATTERN_GNU "'files' takes several values but is neither the last positional nor the one before it.*unless 'target2' has last"
        PATTERN_CLANG "takes several values but is neither the last positional nor the one before it")

clapp_add_compile_fail_test(builder multicall_with_args
        PATTERN_GNU "command 'repl': a multicall command dispatches on argv.*but 'oh-no' was added"
        PATTERN_CLANG "a multicall command dispatches on argv.0. and cannot have arguments")

clapp_add_compile_fail_test(builder optional_before_required_positional
        PATTERN_GNU "the positional 'FILE1' is optional but comes before the required positional 'FILE2'"
        PATTERN_CLANG "is optional but comes before the required positional")

clapp_add_compile_fail_test(builder overrides_with_unknown_id
        PATTERN_GNU "'overrides_with' on 'config' names 'extra', which is neither an argument nor a group"
        PATTERN_CLANG "require_id.*overrides_with.,")

clapp_add_compile_fail_test(builder positional_with_long
        PATTERN_GNU "command 'test': 'arg' has index.*a positional cannot also have a short or long option"
        PATTERN_CLANG "has index.., so it is a positional, and a positional cannot also have")

clapp_add_compile_fail_test(builder positional_without_values
        PATTERN_GNU "'pos' is a positional and must take a value, but its action 'set-true' takes none"
        PATTERN_CLANG "is a positional and must take a value, but its action")

clapp_add_compile_fail_test(builder propagate_version_without_version
        PATTERN_GNU "command 'foo'.*has nothing to propagate.*call version"
        PATTERN_CLANG "has nothing to propagate.*call version")

clapp_add_compile_fail_test(builder required_if_eq_unknown_id
        PATTERN_GNU "'required_if_eq_any' on 'config' names 'extra', which is neither an argument nor a group"
        PATTERN_CLANG "require_id.*required_if_eq_any.,")

clapp_add_compile_fail_test(builder required_unless_unknown_id
        PATTERN_GNU "'required_unless_present_any' on 'config' names 'extra', which is neither an argument nor a group"
        PATTERN_CLANG "require_id.*required_unless_present_any.,")

clapp_add_compile_fail_test(builder requires_self
        PATTERN_GNU "command 'flag_required': 'flag' requires itself"
        PATTERN_CLANG ".' requires itself")

clapp_add_compile_fail_test(builder requires_unknown_id
        PATTERN_GNU "'requires' on 'config' names 'extra', which is neither an argument nor a group"
        PATTERN_CLANG "require_id.*requires.,")

clapp_add_compile_fail_test(builder self_conflicting_arg
        PATTERN_GNU "command 'prog': 'config' conflicts with itself"
        PATTERN_CLANG ".' conflicts with itself")

clapp_add_compile_fail_test(builder subcommand_short_flag_conflicts_with_arg
        PATTERN_GNU "'-f' is claimed by both the argument 'test' and the subcommand 'some'"
        PATTERN_CLANG "': '-., std::string_view.*' is claimed by both")

clapp_add_compile_fail_test(builder too_many_value_names
        PATTERN_GNU "'foo' has 2 value_name.*can never fill more than 1"
        PATTERN_CLANG "value_name..s but a num_args that can never fill more than")

clapp_add_compile_fail_test(builder two_unbounded_positionals
        PATTERN_GNU "command 'lip': 2 positionals have an unbounded num_args"
        PATTERN_CLANG "positionals have an unbounded num_args")

clapp_add_compile_fail_test(builder version_action_without_version
        PATTERN_GNU "the argument 'ver' uses arg_action::version, but the command has neither version"
        PATTERN_CLANG "uses arg_action::version, but the command has neither")

clapp_add_compile_fail_test(builder version_flag_long_conflict
        PATTERN_GNU "'--version' is claimed by both the argument 'ver' and the argument 'version'.*call disable_version_flag"
        PATTERN_CLANG "': '--., clash->first.token, .' is claimed by both")

clapp_add_compile_fail_test(builder version_flag_short_conflict
        PATTERN_GNU "'-V' is claimed by both the argument 'ver' and the argument 'version'.*call disable_version_flag"
        PATTERN_CLANG "': '-., std::string_view.*' is claimed by both")

clapp_add_compile_fail_test(builder negative_control EXPECT compiles)

clapp_add_e2e(scenarios demo/main DRIVES scenarios/demo/main)
clapp_add_e2e(scenarios git/main DRIVES scenarios/git/main)
clapp_add_e2e(scenarios typed/main DRIVES scenarios/typed/main)
clapp_add_e2e(scenarios escaped_positional/main DRIVES scenarios/escaped_positional/main)
clapp_add_e2e(scenarios multicall_busybox/main DRIVES scenarios/multicall_busybox/main)
clapp_add_e2e(scenarios multicall_hostname/main DRIVES scenarios/multicall_hostname/main)
clapp_add_e2e(scenarios repl/main DRIVES scenarios/repl/main)
clapp_add_e2e(scenarios pacman/main DRIVES scenarios/pacman/main)
clapp_add_e2e(scenarios find/main DRIVES scenarios/find/main)

function(_clapp_tutorial_peer edition peer step)
    set(driver "clapp_e2e_tutorial_${edition}_${step}")
    set(peer_binary "clapp_example_tutorial_${peer}_${step}")
    if (NOT TARGET ${driver} OR NOT TARGET ${peer_binary})
        return()
    endif ()
    add_dependencies(${driver} ${peer_binary})
    target_compile_definitions(${driver} PRIVATE
            CLAPP_E2E_PEER_BINARY="$<TARGET_FILE:${peer_binary}>")
endfunction()

function(clapp_add_tutorial_pair step)
    clapp_add_e2e(tutorial builder/${step} DRIVES tutorial/builder/${step})
    clapp_add_e2e(tutorial derive/${step} DRIVES tutorial/derive/${step})
    _clapp_tutorial_peer(builder derive ${step})
    _clapp_tutorial_peer(derive builder ${step})
endfunction()

clapp_add_tutorial_pair(01_quick)
clapp_add_tutorial_pair(02_apps)
clapp_add_tutorial_pair(03_args)
clapp_add_tutorial_pair(04_subcommands)
clapp_add_tutorial_pair(05_validation)

if (CLAPP_BUILD_INSTALL_TEST)
    add_test(NAME "install.add_subdirectory"
            COMMAND "${CMAKE_COMMAND}"
            "-DCLAPP_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DCLAPP_SUBPROJECT_WORK_DIR=${PROJECT_BINARY_DIR}/tests/subproject-work/add-subdirectory"
            "-DCLAPP_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCLAPP_GENERATOR=${CMAKE_GENERATOR}"
            -P "${PROJECT_SOURCE_DIR}/tests/install/run_subproject_check.cmake")
    set_tests_properties("install.add_subdirectory" PROPERTIES
            LABELS "gate;install;subproject" RUN_SERIAL TRUE TIMEOUT 900)

    add_test(NAME "install.find_package"
            COMMAND "${CMAKE_COMMAND}"
            "-DCLAPP_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DCLAPP_WORK_DIR=${PROJECT_BINARY_DIR}/tests/install-work"
            "-DCLAPP_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCLAPP_GENERATOR=${CMAKE_GENERATOR}"
            -P "${PROJECT_SOURCE_DIR}/tests/install/run_install_check.cmake")
    set_tests_properties("install.find_package" PROPERTIES
            LABELS "install" RUN_SERIAL TRUE TIMEOUT 900)
endif ()
