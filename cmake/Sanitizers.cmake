include_guard(GLOBAL)

set(CONDUIT_SANITIZER_FLAGS
    -fsanitize=address
    -fsanitize=undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all
)

# Apply sanitizer flags at directory scope so they propagate to every target
# declared after this module is included — including FetchContent-built
# dependencies (redis-plus-plus, hiredis, etc.). Mixed instrumentation causes
# libc++ container annotations to get out of sync between conduit's code and
# uninstrumented deps, producing spurious container-overflow reports.
if(CONDUIT_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(${CONDUIT_SANITIZER_FLAGS})
    add_link_options   (${CONDUIT_SANITIZER_FLAGS})
elseif(CONDUIT_ENABLE_SANITIZERS AND MSVC)
    message(STATUS "conduit: sanitizers requested but skipped on MSVC")
endif()

# conduit_enable_sanitizers(<target>)
#
# Adds AddressSanitizer + UndefinedBehaviorSanitizer flags to <target> when
# CONDUIT_ENABLE_SANITIZERS is ON and the toolchain is GCC or Clang. The
# directory-scope add_compile_options above already covers all targets in this
# project; this function is kept for backward compatibility and as a no-op
# safety net for targets declared in unusual scopes.
function(conduit_enable_sanitizers target)
    if(NOT CONDUIT_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        return()
    endif()

    target_compile_options(${target} PRIVATE ${CONDUIT_SANITIZER_FLAGS})
    target_link_options   (${target} PRIVATE ${CONDUIT_SANITIZER_FLAGS})
endfunction()
