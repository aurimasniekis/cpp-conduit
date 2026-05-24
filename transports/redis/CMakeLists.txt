# conduit::transport_redis — opt-in Redis pub/sub transport adapter.
# Only included from transports/CMakeLists.txt when CONDUIT_TRANSPORT_REDIS=ON.

include("${CMAKE_SOURCE_DIR}/cmake/RedisDependencies.cmake")

add_library(conduit_transport_redis STATIC
    src/transport.cpp
)
add_library(conduit::transport_redis ALIAS conduit_transport_redis)

target_compile_features(conduit_transport_redis PUBLIC cxx_std_23)
target_include_directories(conduit_transport_redis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# redis-plus-plus exposes the static target as `redis++_static` when used via
# FetchContent and `redis++::redis++_static` when found via find_package.
if(TARGET redis++::redis++_static)
    set(_conduit_redis_lib redis++::redis++_static)
elseif(TARGET redis++_static)
    set(_conduit_redis_lib redis++_static)
elseif(TARGET redis++::redis++)
    set(_conduit_redis_lib redis++::redis++)
elseif(TARGET redis++)
    set(_conduit_redis_lib redis++)
else()
    message(FATAL_ERROR "conduit: no redis-plus-plus target found")
endif()

target_link_libraries(conduit_transport_redis
    PUBLIC  conduit::conduit
    PRIVATE ${_conduit_redis_lib} ${CONDUIT_REDIS_HIREDIS_TARGET}
)

conduit_set_warnings(conduit_transport_redis PRIVATE)
conduit_enable_sanitizers(conduit_transport_redis)
conduit_enable_clang_tidy(conduit_transport_redis)
conduit_enable_coverage(conduit_transport_redis)

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CONDUIT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CONDUIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS conduit_transport_redis
        EXPORT  conduit-transport-redisTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/conduit/redis
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/conduit
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT conduit-transport-redisTargets
        FILE        conduit-transport-redisTargets.cmake
        NAMESPACE   conduit::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-redis
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-redisConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/conduit-transport-redisConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-redisConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-redis
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-redisConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/conduit-transport-redisConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit-transport-redis
    )
endif()
