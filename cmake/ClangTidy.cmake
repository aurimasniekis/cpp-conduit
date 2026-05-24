include_guard(GLOBAL)

# conduit_enable_clang_tidy(<target>)
#
# Applies clang-tidy to <target> when CONDUIT_ENABLE_CLANG_TIDY is ON.
#
# Why per-target rather than the global CMAKE_CXX_CLANG_TIDY?  Setting the
# variable globally causes every FetchContent'd third-party dependency
# (libzmq, paho, nats.c, AMQP-CPP, redis-plus-plus, ...) to inherit the
# clang-tidy command, and a great many of them fail under our `.clang-tidy`
# config — they're not our code to fix. Scoping the property to our own
# targets keeps the linter laser-focused on conduit code.

if(CONDUIT_ENABLE_CLANG_TIDY)
    find_program(CONDUIT_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(CONDUIT_CLANG_TIDY_COMMAND
        "${CONDUIT_CLANG_TIDY_EXE}"
        "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-search-dirs
            OUTPUT_VARIABLE _conduit_gcc_search_dirs
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_conduit_gcc_search_dirs MATCHES "install: ([^\n]+)")
            list(APPEND CONDUIT_CLANG_TIDY_COMMAND
                "--extra-arg-before=--gcc-install-dir=${CMAKE_MATCH_1}")
            message(STATUS
                "conduit: pinning clang-tidy to gcc install dir ${CMAKE_MATCH_1}")
        endif()
    endif()
    message(STATUS "conduit: clang-tidy enabled (${CONDUIT_CLANG_TIDY_EXE})")
endif()

function(conduit_enable_clang_tidy target)
    if(NOT CONDUIT_ENABLE_CLANG_TIDY)
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_CLANG_TIDY "${CONDUIT_CLANG_TIDY_COMMAND}"
    )
endfunction()
