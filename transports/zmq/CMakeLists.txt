# conduit::transport_zmq — opt-in ZeroMQ transport adapter.
# Only included from transports/CMakeLists.txt when CONDUIT_TRANSPORT_ZMQ=ON.

include("${CMAKE_SOURCE_DIR}/cmake/ZmqDependencies.cmake")

add_library(conduit_transport_zmq STATIC
    src/transport.cpp
)
add_library(conduit::transport_zmq ALIAS conduit_transport_zmq)

target_compile_features(conduit_transport_zmq PUBLIC cxx_std_23)
target_include_directories(conduit_transport_zmq
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(conduit_transport_zmq
    PUBLIC  conduit::conduit
    PRIVATE ${CONDUIT_ZMQ_CPPZMQ_TARGET} ${CONDUIT_ZMQ_LIBZMQ_TARGET}
)

if(CONDUIT_TRANSPORT_ZMQ_CURVE)
    target_compile_definitions(conduit_transport_zmq PRIVATE CONDUIT_ZMQ_HAS_CURVE=1)
endif()

conduit_set_warnings(conduit_transport_zmq PRIVATE)
conduit_enable_sanitizers(conduit_transport_zmq)
conduit_enable_clang_tidy(conduit_transport_zmq)
conduit_enable_coverage(conduit_transport_zmq)

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CONDUIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CONDUIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS conduit_transport_zmq
        EXPORT  conduit-transport-zmqTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/conduit/zmq
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/conduit
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT conduit-transport-zmqTargets
        FILE        conduit-transport-zmqTargets.cmake
        NAMESPACE   conduit::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-zmq
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-zmqConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/conduit-transport-zmqConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-zmqConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-zmq
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-zmqConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-zmqConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-zmq
    )
endif()
