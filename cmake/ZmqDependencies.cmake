include_guard(GLOBAL)
include(FetchContent)

# libzmq and cppzmq target CMake older than CMake 4 supports. The policy-min
# injection below lets modern CMake configure the legacy scripts.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# CurveZMQ pulls libsodium; gate behind CONDUIT_TRANSPORT_ZMQ_CURVE so the
# default ZMQ build stays light.
if(CONDUIT_TRANSPORT_ZMQ_CURVE)
    set(_conduit_zmq_curve ON)
else()
    set(_conduit_zmq_curve OFF)
endif()

# libzmq build options. Force shared OFF or libzmq.so leaks into the build
# tree alongside the static archive. BUILD_TESTS=OFF avoids libzmq's own
# unittest tree (some of it fails under -Werror on certain toolchains).
set(BUILD_TESTS                OFF CACHE INTERNAL "")
set(BUILD_SHARED               OFF CACHE INTERNAL "")
set(BUILD_STATIC               ON  CACHE INTERNAL "")
set(WITH_PERF_TOOL             OFF CACHE INTERNAL "")
set(ENABLE_CPACK               OFF CACHE INTERNAL "")
set(ENABLE_DRAFTS              OFF CACHE INTERNAL "")
set(ENABLE_CURVE               ${_conduit_zmq_curve} CACHE INTERNAL "")
set(WITH_LIBSODIUM             ${_conduit_zmq_curve} CACHE INTERNAL "")
set(ZMQ_BUILD_TESTS            OFF CACHE INTERNAL "")

FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG        v4.3.5
    FIND_PACKAGE_ARGS NAMES ZeroMQ
)
FetchContent_MakeAvailable(libzmq)

# cppzmq's `find_package(ZeroMQ)` expects ZeroMQ::ZeroMQ (or ZeroMQ::libzmq-
# static) to exist. When libzmq was built via FetchContent, only the raw
# `libzmq-static` / `libzmq` targets show up — bridge them with an ALIAS
# *before* pulling cppzmq.
if(TARGET libzmq-static AND NOT TARGET ZeroMQ::libzmq-static)
    add_library(ZeroMQ::libzmq-static ALIAS libzmq-static)
endif()
if(TARGET libzmq AND NOT TARGET ZeroMQ::libzmq)
    add_library(ZeroMQ::libzmq ALIAS libzmq)
endif()

set(CPPZMQ_BUILD_TESTS         OFF CACHE INTERNAL "")
# If libzmq came from FetchContent, force cppzmq to FetchContent as well.
# Mixing system cppzmq with a fetched libzmq breaks linking: system cppzmq's
# CMake config links against `libzmq` as a bare name, which no longer
# resolves to a target and leaks through to the linker as `-llibzmq`.
if(DEFINED libzmq_SOURCE_DIR)
    FetchContent_Declare(
        cppzmq
        GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
        GIT_TAG        v4.10.0
    )
else()
    FetchContent_Declare(
        cppzmq
        GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
        GIT_TAG        v4.10.0
        FIND_PACKAGE_ARGS NAMES cppzmq
    )
endif()
FetchContent_MakeAvailable(cppzmq)

# Resolve a libzmq static target name regardless of consumption path.
set(_conduit_libzmq_target "")
foreach(_t libzmq-static libzmq ZeroMQ::libzmq-static ZeroMQ::libzmq)
    if(TARGET ${_t})
        set(_conduit_libzmq_target ${_t})
        break()
    endif()
endforeach()
if(_conduit_libzmq_target STREQUAL "")
    message(FATAL_ERROR "conduit: no libzmq target found")
endif()
set(CONDUIT_ZMQ_LIBZMQ_TARGET "${_conduit_libzmq_target}" CACHE INTERNAL "")

# Prefer cppzmq-static: cppzmq's CMakeLists declares BOTH `cppzmq` and
# `cppzmq-static`. `cppzmq` interface-links to the shared `libzmq` target,
# which doesn't exist when we built libzmq with BUILD_SHARED=OFF — picking
# it leaks `-llibzmq` to the linker. `cppzmq-static` links to the matching
# `libzmq-static` target we actually built.
set(_conduit_cppzmq_target "")
foreach(_t cppzmq-static cppzmq)
    if(TARGET ${_t})
        set(_conduit_cppzmq_target ${_t})
        break()
    endif()
endforeach()
if(_conduit_cppzmq_target STREQUAL "")
    message(FATAL_ERROR "conduit: no cppzmq target found")
endif()
set(CONDUIT_ZMQ_CPPZMQ_TARGET "${_conduit_cppzmq_target}" CACHE INTERNAL "")

# Mark third-party include dirs as SYSTEM so consumers built with -Werror
# don't inherit warnings from libzmq / cppzmq headers.
foreach(_zmq_target libzmq libzmq-static cppzmq cppzmq-static)
    if(TARGET ${_zmq_target})
        get_target_property(_zmq_inc ${_zmq_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_zmq_inc)
            set_target_properties(${_zmq_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_zmq_inc}")
        endif()
    endif()
endforeach()
