include_guard(GLOBAL)
include(FetchContent)

# nats.c's bundled CMakeLists targets a CMake older than CMake 4 supports.
# The policy-min injection below lets modern CMake configure the legacy script.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# TLS is an opt-in build flag — pull in OpenSSL only if requested.
if(CONDUIT_TRANSPORT_NATS_TLS)
    find_package(OpenSSL REQUIRED)
endif()

# nats.c build options. Set BEFORE FetchContent so the cache vars seed the
# subproject correctly. NATS_BUILD_LIB_STATIC / _SHARED are nats.c's own
# switches (distinct from CMake's global BUILD_SHARED_LIBS).
set(NATS_BUILD_LIB_STATIC      ON  CACHE INTERNAL "")
set(NATS_BUILD_LIB_SHARED      OFF CACHE INTERNAL "")
set(NATS_BUILD_EXAMPLES        OFF CACHE INTERNAL "")
set(NATS_BUILD_STREAMING       OFF CACHE INTERNAL "")
if(CONDUIT_TRANSPORT_NATS_TLS)
    set(NATS_BUILD_WITH_TLS    ON  CACHE INTERNAL "")
else()
    set(NATS_BUILD_WITH_TLS    OFF CACHE INTERNAL "")
endif()

# nats.c reads its own BUILD_TESTING cache var; set it after the global block
# so CTest's BUILD_TESTING (left on by enable_testing) doesn't drag in tests.
set(NATS_BUILD_TESTING         OFF CACHE INTERNAL "")

FetchContent_Declare(
    cnats
    GIT_REPOSITORY https://github.com/nats-io/nats.c.git
    GIT_TAG        v3.9.2
    FIND_PACKAGE_ARGS NAMES cnats
)

# Suppress nats.c's own enable_testing()/add_test() under FetchContent.
set(_conduit_saved_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE INTERNAL "" FORCE)
FetchContent_MakeAvailable(cnats)
if(NOT "${_conduit_saved_build_testing}" STREQUAL "")
    set(BUILD_TESTING "${_conduit_saved_build_testing}" CACHE INTERNAL "" FORCE)
endif()

# Mark nats.c include dirs as SYSTEM so consumers built with -Werror don't
# inherit warnings from third-party headers.
foreach(_nats_target nats_static nats)
    if(TARGET ${_nats_target})
        get_target_property(_nats_inc ${_nats_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_nats_inc)
            set_target_properties(${_nats_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nats_inc}")
        endif()
    endif()
endforeach()
