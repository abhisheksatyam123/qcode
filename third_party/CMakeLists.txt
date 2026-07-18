# Bundled third-party dependencies for qcode.

set(QCODE_THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Third party directory root")

add_subdirectory(zlib-cmake)
add_subdirectory(brotli-cmake)
add_subdirectory(httplib-cmake)
add_subdirectory(nlohmann_json-cmake)
add_subdirectory(concurrentqueue-cmake)
add_subdirectory(stduuid-cmake)

if(BUILD_TESTS)
    add_subdirectory(googletest-cmake)
    if(BUILD_CLICKHOUSE_TESTS)
        add_subdirectory(clickhouse-cmake)
    endif()
endif()

add_subdirectory(sqlite)
