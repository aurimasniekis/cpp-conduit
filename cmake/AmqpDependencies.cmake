include_guard(GLOBAL)
include(FetchContent)

# AMQP-CPP's bundled CMakeLists targets a CMake older than CMake 4 supports.
# The policy-min injection below lets modern CMake configure the legacy script.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# AMQP-CPP from Copernica. We deliberately do not use any of the bundled
# event-loop integrations (LibBoostAsio / LibEv etc.) — the conduit AMQP
# transport implements its own minimal TcpHandler over poll() so we avoid
# pulling in a transitive event-loop dependency.
set(AMQP-CPP_BUILD_SHARED      OFF CACHE INTERNAL "")
set(AMQP-CPP_LINUX_TCP         ON  CACHE INTERNAL "")
set(AMQP-CPP_BUILD_EXAMPLES    OFF CACHE INTERNAL "")
set(AMQP-CPP_BUILD_TESTING     OFF CACHE INTERNAL "")

FetchContent_Declare(
    amqpcpp
    GIT_REPOSITORY https://github.com/CopernicaMarketingSoftware/AMQP-CPP.git
    GIT_TAG        v4.3.27
    FIND_PACKAGE_ARGS NAMES amqpcpp
)
FetchContent_MakeAvailable(amqpcpp)

# Mark amqpcpp's include dirs as SYSTEM on each known target so consumers
# built with -Werror don't inherit warnings from third-party headers.
foreach(_amqpcpp_target amqpcpp amqpcpp::amqpcpp)
    if(TARGET ${_amqpcpp_target})
        get_target_property(_amqpcpp_inc ${_amqpcpp_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_amqpcpp_inc)
            set_target_properties(${_amqpcpp_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_amqpcpp_inc}")
        endif()
    endif()
endforeach()

# AMQP-CPP's Linux TCP headers unconditionally `#include <openssl/ssl.h>` and
# the static library it builds links libssl/libcrypto. OpenSSL is therefore a
# hard requirement whenever AMQP-CPP is enabled, not just when TLS is on.
find_package(OpenSSL REQUIRED)
if(TARGET amqpcpp)
    set_property(TARGET amqpcpp APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
    set_property(TARGET amqpcpp APPEND PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
endif()
