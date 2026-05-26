include_guard(GLOBAL)
include(FetchContent)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 3.12.0 NAMES nlohmann_json
)
FetchContent_MakeAvailable(nlohmann_json)

set(ULID_BUILD_TESTS      OFF CACHE INTERNAL "")
set(ULID_BUILD_BENCHMARKS OFF CACHE INTERNAL "")
set(ULID_BUILD_DOCS       OFF CACHE INTERNAL "")
FetchContent_Declare(
    ulid
    GIT_REPOSITORY https://github.com/aurimasniekis/cpp-ulid.git
    GIT_TAG        v1.0.0
    FIND_PACKAGE_ARGS 1.0.0 NAMES ulid
)
FetchContent_MakeAvailable(ulid)

set(PARCEL_BUILD_TESTS    OFF CACHE INTERNAL "")
set(PARCEL_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(PARCEL_BUILD_DOCS     OFF CACHE INTERNAL "")
FetchContent_Declare(
    parcel
    GIT_REPOSITORY https://github.com/aurimasniekis/cpp-parcel.git
    GIT_TAG        v0.1.0
    FIND_PACKAGE_ARGS 0.1.0 NAMES parcel
)
FetchContent_MakeAvailable(parcel)

set(METADATA_BUILD_TESTS    OFF CACHE INTERNAL "")
set(METADATA_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(METADATA_BUILD_DOCS     OFF CACHE INTERNAL "")
FetchContent_Declare(
    metadata
    GIT_REPOSITORY https://github.com/aurimasniekis/cpp-metadata.git
    GIT_TAG        v0.1.0
    FIND_PACKAGE_ARGS 0.1.0 NAMES metadata
)
FetchContent_MakeAvailable(metadata)

set(COMMONS_BUILD_TESTS    OFF CACHE INTERNAL "")
set(COMMONS_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(COMMONS_BUILD_DOCS     OFF CACHE INTERNAL "")
FetchContent_Declare(
    commons
    GIT_REPOSITORY https://github.com/aurimasniekis/cpp-commons.git
    GIT_TAG        v0.1.1
    FIND_PACKAGE_ARGS 0.1.1 NAMES commons
)
FetchContent_MakeAvailable(commons)

if(CONDUIT_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK   OFF CACHE INTERNAL "")
    FetchContent_Declare(
        googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES GTest
    )
    FetchContent_MakeAvailable(googletest)
endif()
