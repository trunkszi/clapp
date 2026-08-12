cmake_minimum_required(VERSION 4.3)

foreach(_required IN ITEMS CLAPP_CF_NAME CLAPP_CF_SOURCE CLAPP_CF_COMPILER CLAPP_CF_EXPECT)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR
            "clapp compile-fail driver: ${_required} was not passed.\n"
            "  This script is meant to be invoked by clapp_add_compile_fail_test(), "
            "not by hand.")
    endif()
endforeach()

if(NOT EXISTS "${CLAPP_CF_SOURCE}")
    message(FATAL_ERROR
        "clapp compile-fail [${CLAPP_CF_NAME}]: the snippet does not exist:\n"
        "  ${CLAPP_CF_SOURCE}")
endif()

string(REPLACE "|" ";" _flags "${CLAPP_CF_FLAGS}")

execute_process(
    COMMAND "${CLAPP_CF_COMPILER}" ${_flags} "${CLAPP_CF_SOURCE}"
    RESULT_VARIABLE _status
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    TIMEOUT         300)

set(_diagnostic "${_stdout}${_stderr}")

string(REPLACE "\\'" "'" _diagnostic "${_diagnostic}")
string(REPLACE "\\\"" "\"" _diagnostic "${_diagnostic}")

if(NOT _status MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR
        "clapp compile-fail [${CLAPP_CF_NAME}]: the compiler could not be run at all.\n"
        "  compiler: ${CLAPP_CF_COMPILER}\n"
        "  reason  : ${_status}\n"
        "  This is a harness fault, not a result. Nothing was proved about the contract.")
endif()

if(CLAPP_CF_EXPECT STREQUAL "compiles")
    if(NOT _status EQUAL 0)
        message(FATAL_ERROR
            "clapp compile-fail [${CLAPP_CF_NAME}]: NEGATIVE CONTROL FAILED.\n"
            "  This snippet must COMPILE. It is the control that proves the other\n"
            "  compile_fail.* tests are testing something: a harness that reported\n"
            "  'rejected as expected' unconditionally would leave every one of them\n"
            "  green, and only this test red.\n"
            "  snippet : ${CLAPP_CF_SOURCE}\n"
            "  exit    : ${_status}\n"
            "  --- compiler output ---\n${_diagnostic}")
    endif()
    message(STATUS "clapp compile-fail [${CLAPP_CF_NAME}]: negative control compiled, as required")
    return()
endif()

if(NOT CLAPP_CF_EXPECT STREQUAL "rejected")
    message(FATAL_ERROR
        "clapp compile-fail [${CLAPP_CF_NAME}]: unknown expectation '${CLAPP_CF_EXPECT}' "
        "(use 'rejected' or 'compiles').")
endif()

if(NOT DEFINED CLAPP_CF_PATTERN OR CLAPP_CF_PATTERN STREQUAL "")
    message(FATAL_ERROR
        "clapp compile-fail [${CLAPP_CF_NAME}]: no pattern was given.\n"
        "  A compile-fail test without a pattern passes for ANY compiler error, "
        "including a typo in the snippet.")
endif()

if(_status EQUAL 0)
    message(FATAL_ERROR
        "clapp compile-fail [${CLAPP_CF_NAME}]: the snippet COMPILED.\n"
        "  This contract's correct behaviour is to reject compilation, so a clean build\n"
        "  here means the guard is gone — not that the code got better.\n"
        "  snippet : ${CLAPP_CF_SOURCE}\n"
        "  expected: a diagnostic matching /${CLAPP_CF_PATTERN}/")
endif()

set(_matched_contract FALSE)
set(_matched_no_exceptions_contract FALSE)
if(_diagnostic MATCHES "${CLAPP_CF_PATTERN}")
    set(_matched_contract TRUE)
elseif(CLAPP_CF_NO_EXCEPTIONS
       AND _diagnostic MATCHES "abort"
       AND _diagnostic MATCHES "constant expression")
    set(_matched_contract TRUE)
    set(_matched_no_exceptions_contract TRUE)
endif()

if(NOT _matched_contract)
    message(FATAL_ERROR
        "clapp compile-fail [${CLAPP_CF_NAME}]: rejected, but for the wrong reason.\n"
        "  The compiler refused the snippet, which is only half the contract. Its\n"
        "  diagnostic matched neither the requested pattern nor the no-exceptions\n"
        "  consteval abort marker.\n"
        "  snippet : ${CLAPP_CF_SOURCE}\n"
        "  required: /${CLAPP_CF_PATTERN}/\n"
        "  --- compiler output ---\n${_diagnostic}")
endif()

if(_matched_no_exceptions_contract)
    message(STATUS
        "clapp compile-fail [${CLAPP_CF_NAME}]: rejected at the no-exceptions consteval guard")
else()
    message(STATUS
        "clapp compile-fail [${CLAPP_CF_NAME}]: rejected, and the diagnostic matched the contract")
endif()
