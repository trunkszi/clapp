/**
 * \file
 * \brief clapp's sole public umbrella header — include this and use; no linking
 *        required.
 */

#pragma once

#include <clapp/config.hpp>
#include <clapp/meta/annotations.hpp>

#include <clapp/lex/os_str.hpp>
#include <clapp/lex/parsed_arg.hpp>
#include <clapp/lex/raw_args.hpp>
#include <clapp/lex/short_flags.hpp>

#include <clapp/util/any_value.hpp>
#include <clapp/util/flat_map.hpp>
#include <clapp/util/flat_set.hpp>
#include <clapp/util/graph.hpp>
#include <clapp/util/id.hpp>
#include <clapp/util/str.hpp>

#include <clapp/builder/action.hpp>
#include <clapp/builder/arg.hpp>
#include <clapp/builder/arg_group.hpp>
#include <clapp/builder/command.hpp>
#include <clapp/builder/possible_value.hpp>
#include <clapp/builder/styling.hpp>
#include <clapp/builder/value_hint.hpp>
#include <clapp/builder/value_parser.hpp>
#include <clapp/builder/value_range.hpp>

#include <clapp/output/styled_str.hpp>

#include <clapp/output/help.hpp>
#include <clapp/output/render.hpp>
#include <clapp/output/textwrap.hpp>
#include <clapp/output/usage.hpp>


#include <clapp/error/context.hpp>
#include <clapp/error/error.hpp>
#include <clapp/error/error_kind.hpp>

#include <clapp/parser/arg_matcher.hpp>
#include <clapp/parser/arg_matches.hpp>
#include <clapp/parser/matched_arg.hpp>
#include <clapp/parser/parse.hpp>
#include <clapp/parser/validator.hpp>
#include <clapp/parser/value_source.hpp>

#include <clapp/meta/deduce.hpp>
#include <clapp/meta/from_matches.hpp>
#include <clapp/meta/parse.hpp>
