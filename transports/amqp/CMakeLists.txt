# conduit::transport_amqp — opt-in AMQP 0.9.1 transport adapter (RabbitMQ etc).
# Only included from transports/CMakeLists.txt when CONDUIT_TRANSPORT_AMQP=ON.

include("${CMAKE_SOURCE_DIR}/cmake/AmqpDependencies.cmake")

add_library(conduit_transport_amqp STATIC
    src/transport.cpp
)
add_library(conduit::transport_amqp ALIAS conduit_transport_amqp)

target_compile_features(conduit_transport_amqp PUBLIC cxx_std_23)
target_include_directories(conduit_transport_amqp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(TARGET amqpcpp::amqpcpp)
    set(_conduit_amqp_lib amqpcpp::amqpcpp)
elseif(TARGET amqpcpp)
    set(_conduit_amqp_lib amqpcpp)
else()
    message(FATAL_ERROR "conduit: no AMQP-CPP target found")
endif()

target_link_libraries(conduit_transport_amqp
    PUBLIC  conduit::conduit
    PRIVATE ${_conduit_amqp_lib}
)

if(CONDUIT_TRANSPORT_AMQP_TLS)
    target_link_libraries(conduit_transport_amqp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(conduit_transport_amqp PRIVATE CONDUIT_TRANSPORT_AMQP_TLS=1)
endif()

conduit_set_warnings(conduit_transport_amqp PRIVATE)
conduit_enable_sanitizers(conduit_transport_amqp)
conduit_enable_clang_tidy(conduit_transport_amqp)
conduit_enable_coverage(conduit_transport_amqp)

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CONDUIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CONDUIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS conduit_transport_amqp
        EXPORT  conduit-transport-amqpTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/conduit/amqp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/conduit
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT conduit-transport-amqpTargets
        FILE        conduit-transport-amqpTargets.cmake
        NAMESPACE   conduit::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-amqp
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-amqpConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/conduit-transport-amqpConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-amqpConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-amqp
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-amqpConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-amqpConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-amqp
    )
endif()
