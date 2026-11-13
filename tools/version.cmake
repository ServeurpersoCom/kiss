# Generate version.h with the current git commit hash and date.
# Rewrites the file only when its content changes, so nothing else rebuilds.
# Usage: cmake -DSRC_DIR=... -DOUTPUT=... -P version.cmake

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
)
if(NOT GIT_RESULT EQUAL 0)
    set(GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git show -s --format=%cs HEAD
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE GIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE DATE_RESULT
)
if(NOT DATE_RESULT EQUAL 0)
    set(GIT_DATE "unknown")
endif()

set(CONTENT "#pragma once\n#define KISS_VERSION \"${GIT_HASH} (${GIT_DATE})\"\n")

if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" EXISTING)
    if("${EXISTING}" STREQUAL "${CONTENT}")
        return()
    endif()
endif()

file(WRITE "${OUTPUT}" "${CONTENT}")
