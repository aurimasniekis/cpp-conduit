# conduit::transport_nats — opt-in NATS transport adapter.
# Only included from transports/CMakeLists.txt when CONDUIT_TRANSPORT_NATS=ON.

include("${CMAKE_SOURCE_DIR}/cmake/NatsDependencies.cmake")

add_library(conduit_transport_nats STATIC
    src/transport.cpp
)
add_library(conduit::transport_nats ALIAS conduit_transport_nats)

target_compile_features(conduit_transport_nats PUBLIC cxx_std_23)
target_include_directories(conduit_transport_nats
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# nats.c exposes its static library under several target names depending on
# how it was consumed (FetchContent vs find_package, version-dependent names).
if(TARGET cnats::nats_static)
    set(_conduit_nats_lib cnats::nats_static)
elseif(TARGET nats_static)
    set(_conduit_nats_lib nats_static)
elseif(TARGET cnats::nats)
    set(_conduit_nats_lib cnats::nats)
elseif(TARGET nats)
    set(_conduit_nats_lib nats)
else()
    message(FATAL_ERROR "conduit: no nats.c target found")
endif()

target_link_libraries(conduit_transport_nats
    PUBLIC  conduit::conduit
    PRIVATE ${_conduit_nats_lib}
)

if(CONDUIT_TRANSPORT_NATS_TLS)
    target_compile_definitions(conduit_transport_nats PRIVATE NATS_HAS_TLS=1)
endif()

conduit_set_warnings(conduit_transport_nats PRIVATE)
conduit_enable_sanitizers(conduit_transport_nats)
conduit_enable_clang_tidy(conduit_transport_nats)
conduit_enable_coverage(conduit_transport_nats)

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CONDUIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CONDUIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS conduit_transport_nats
        EXPORT  conduit-transport-natsTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/conduit/nats
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/conduit
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT conduit-transport-natsTargets
        FILE        conduit-transport-natsTargets.cmake
        NAMESPACE   conduit::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-nats
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-natsConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/conduit-transport-natsConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-natsConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-nats
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-natsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-natsConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-nats
    )
endif()
