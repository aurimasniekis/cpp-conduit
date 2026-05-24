include_guard(GLOBAL)
include(FetchContent)

# hiredis and redis-plus-plus both target a CMake older than CMake 4 supports.
# The policy-min injection below lets modern CMake configure the legacy scripts.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# hiredis (C client) — pinned to v1.2.0; required by redis-plus-plus.
set(DISABLE_TESTS              ON  CACHE INTERNAL "")
set(ENABLE_SSL                 ${CONDUIT_TRANSPORT_REDIS_TLS} CACHE INTERNAL "")
set(BUILD_SHARED_LIBS          OFF CACHE INTERNAL "")
set(ENABLE_EXAMPLES            OFF CACHE INTERNAL "")

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.2.0
    FIND_PACKAGE_ARGS NAMES hiredis
)
FetchContent_MakeAvailable(hiredis)

# hiredis headers live at the root of the source tree (hiredis.h, async.h, …).
# Once installed, they end up under `include/hiredis/`, which is the form
# downstream code uses (`#include <hiredis/hiredis.h>`). Stage a shim include
# directory that mirrors that layout so the FetchContent build is consumable
# the same way.
set(_conduit_hiredis_shim "${CMAKE_BINARY_DIR}/conduit-hiredis-shim/hiredis")
file(MAKE_DIRECTORY "${_conduit_hiredis_shim}")
file(GLOB _conduit_hiredis_headers "${hiredis_SOURCE_DIR}/*.h")
foreach(_h ${_conduit_hiredis_headers})
    get_filename_component(_hn "${_h}" NAME)
    file(CREATE_LINK "${_h}" "${_conduit_hiredis_shim}/${_hn}" COPY_ON_ERROR SYMBOLIC)
endforeach()
foreach(_ht hiredis hiredis_static)
    if(TARGET ${_ht})
        target_include_directories(${_ht} INTERFACE
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/conduit-hiredis-shim>)
    endif()
endforeach()

# hiredis only installs the `hiredis::hiredis` alias when consumed via
# find_package() after install. redis-plus-plus links against that exact
# alias, so define it ourselves when we built hiredis via add_subdirectory.
if(NOT TARGET hiredis::hiredis AND TARGET hiredis)
    add_library(hiredis::hiredis ALIAS hiredis)
endif()
if(NOT TARGET hiredis::hiredis_static AND TARGET hiredis_static)
    add_library(hiredis::hiredis_static ALIAS hiredis_static)
endif()
if(NOT TARGET hiredis::hiredis_ssl AND TARGET hiredis_ssl)
    add_library(hiredis::hiredis_ssl ALIAS hiredis_ssl)
endif()

# Resolve a usable hiredis static target name regardless of whether hiredis
# was found via find_package() or fetched by FetchContent.
set(_conduit_redis_hiredis_lib "")
foreach(_h hiredis::hiredis_static hiredis::hiredis hiredis_static hiredis)
    if(TARGET ${_h})
        set(_conduit_redis_hiredis_lib ${_h})
        break()
    endif()
endforeach()
if(_conduit_redis_hiredis_lib STREQUAL "")
    message(FATAL_ERROR "conduit: no hiredis target found")
endif()
set(CONDUIT_REDIS_HIREDIS_TARGET "${_conduit_redis_hiredis_lib}" CACHE INTERNAL "")

# redis-plus-plus's CMakeLists does a check_symbol_exists() against hiredis at
# configure time. That check uses try_compile() which can't see ALIAS targets
# from this project, so pre-populate the cache var (hiredis v1.2.0 ships the
# function) to skip the check entirely.
set(REDIS_PLUS_PLUS_HAS_redisEnableKeepAliveWithInterval 1 CACHE INTERNAL "")

# redis-plus-plus — C++ wrapper around hiredis. Use latest 1.3.x tag.
set(REDIS_PLUS_PLUS_BUILD_TEST   OFF CACHE INTERNAL "")
set(REDIS_PLUS_PLUS_BUILD_STATIC ON  CACHE INTERNAL "")
set(REDIS_PLUS_PLUS_BUILD_SHARED OFF CACHE INTERNAL "")
set(REDIS_PLUS_PLUS_CXX_STANDARD 17  CACHE INTERNAL "")
if(CONDUIT_TRANSPORT_REDIS_TLS)
    set(REDIS_PLUS_PLUS_USE_TLS ON  CACHE INTERNAL "")
else()
    set(REDIS_PLUS_PLUS_USE_TLS OFF CACHE INTERNAL "")
endif()

FetchContent_Declare(
    redis_plus_plus
    GIT_REPOSITORY https://github.com/sewenew/redis-plus-plus.git
    GIT_TAG        1.3.13
    FIND_PACKAGE_ARGS NAMES redis++
)
FetchContent_MakeAvailable(redis_plus_plus)

# redis-plus-plus generates `sw/redis++/hiredis_features.h` into its binary
# tree and includes it from public headers (e.g. `connection.h`), but exposes
# the generated dir only as a PRIVATE include on its own targets. Promote it
# to PUBLIC so downstream consumers (this transport) can pull in the same
# public headers without a missing-header build failure.
foreach(_rpp_target redis++_static redis++)
    if(TARGET ${_rpp_target})
        target_include_directories(${_rpp_target} PUBLIC
            $<BUILD_INTERFACE:${redis_plus_plus_BINARY_DIR}/src>)
    endif()
endforeach()

# Mark third-party include dirs as SYSTEM so -Werror consumers don't inherit
# warnings from hiredis / redis++ headers. ALIAS targets must be skipped —
# set_target_properties can't be used on them, so resolve to the real target.
foreach(_redis_target
    hiredis hiredis_static
    redis++_static redis++)
    if(TARGET ${_redis_target})
        get_target_property(_redis_inc ${_redis_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_redis_inc)
            set_target_properties(${_redis_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_redis_inc}")
        endif()
    endif()
endforeach()
