cmake_minimum_required(VERSION 3.25)

project(conduit
    VERSION     0.5.0
    DESCRIPTION "Modern C++23 header-only event-dispatching / event-transport library"
    LANGUAGES   CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(CONDUIT_BUILD_TESTS         "Build conduit tests"                  ${PROJECT_IS_TOP_LEVEL})
option(CONDUIT_BUILD_EXAMPLES      "Build conduit examples"               ${PROJECT_IS_TOP_LEVEL})
option(CONDUIT_BUILD_DOCS          "Build Doxygen API documentation"      OFF)
option(CONDUIT_ENABLE_CLANG_TIDY   "Run clang-tidy during build"          OFF)
option(CONDUIT_ENABLE_SANITIZERS   "Build with ASan + UBSan (Debug)"      OFF)
option(CONDUIT_ENABLE_COVERAGE     "Build with Clang source-based coverage" OFF)
option(CONDUIT_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"    ${PROJECT_IS_TOP_LEVEL})
option(CONDUIT_INSTALL             "Generate install rules for conduit"   ${PROJECT_IS_TOP_LEVEL})
option(CONDUIT_TRANSPORT_MQTT       "Build conduit::transport_mqtt (Paho MQTT C++)"      OFF)
option(CONDUIT_TRANSPORT_AMQP       "Build conduit::transport_amqp (AMQP-CPP)"           OFF)
option(CONDUIT_TRANSPORT_AMQP_TLS   "Enable TLS/OpenSSL inside conduit::transport_amqp"  OFF)
option(CONDUIT_TRANSPORT_REDIS      "Build conduit::transport_redis (redis-plus-plus)"   OFF)
option(CONDUIT_TRANSPORT_REDIS_TLS  "Enable TLS inside conduit::transport_redis"         OFF)
option(CONDUIT_TRANSPORT_NATS       "Build conduit::transport_nats (nats.c)"             OFF)
option(CONDUIT_TRANSPORT_NATS_TLS   "Enable TLS/OpenSSL inside conduit::transport_nats"  OFF)
option(CONDUIT_TRANSPORT_ZMQ        "Build conduit::transport_zmq (libzmq + cppzmq)"     OFF)
option(CONDUIT_TRANSPORT_ZMQ_CURVE  "Enable CurveZMQ (libsodium) in conduit::transport_zmq" OFF)

include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(ClangTidy)
include(Dependencies)

find_package(Threads REQUIRED)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/conduit/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/conduit/version.hpp"
    @ONLY
)

add_library(conduit INTERFACE)
add_library(conduit::conduit ALIAS conduit)

target_include_directories(conduit
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(conduit INTERFACE cxx_std_23)

target_link_libraries(conduit INTERFACE
    nlohmann_json::nlohmann_json
    ulid::ulid
    parcel::parcel
    metadata::metadata
    commons::commons
    threadman::threadman
    prom::prom
    Threads::Threads
)

if(CONDUIT_INSTALL AND (DEFINED nlohmann_json_SOURCE_DIR OR DEFINED ulid_SOURCE_DIR OR DEFINED parcel_SOURCE_DIR OR DEFINED metadata_SOURCE_DIR OR DEFINED commons_SOURCE_DIR OR DEFINED threadman_SOURCE_DIR OR DEFINED prom_SOURCE_DIR OR DEFINED logman_SOURCE_DIR OR DEFINED dimval_SOURCE_DIR OR DEFINED spdlog_SOURCE_DIR))
    message(STATUS
        "conduit: CONDUIT_INSTALL disabled — a fetched dependency cannot be re-exported. "
        "Install nlohmann_json / ulid / parcel / metadata / commons / threadman / prom / logman / dimval / spdlog via system package or find_package to re-enable install rules."
    )
    set(CONDUIT_INSTALL OFF)
endif()

if(CONDUIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CONDUIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

add_subdirectory(transports)

if(CONDUIT_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(CONDUIT_DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(conduit_docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating Doxygen documentation"
        VERBATIM
    )
endif()

if(CONDUIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS conduit
        EXPORT  conduitTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/conduit
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/include/conduit/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/conduit
    )

    install(EXPORT conduitTargets
        FILE        conduitTargets.cmake
        NAMESPACE   conduit::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/conduitConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/conduitConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/conduitConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/conduitConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/conduitConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
    )
endif()
