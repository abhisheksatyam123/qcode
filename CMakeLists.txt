cmake_minimum_required(VERSION 3.16)

project(qcode
    VERSION 0.1.0
    DESCRIPTION "qcode — C++ toolkit for AI-powered coding agents"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_EXAMPLES "Build example applications" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(QCODE_BUILD_TUI "Build TUI application" ON)
option(QCODE_BUILD_SERVER "Build headless server" ON)
option(QCODE_BUILD_CLI "Build CLI client" ON)

set(HTTPLIB_COMPILE_DEFS
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    CPPHTTPLIB_THREAD_POOL_COUNT=8
)

add_subdirectory(third_party)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# FTXUI is required by libqcode render sources (markdown/message/tool UI),
# not only by the TUI app binary.
include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

add_subdirectory(libqcode)

if(QCODE_BUILD_TUI)
    add_subdirectory(apps/tui)
endif()

if(QCODE_BUILD_SERVER)
    add_subdirectory(apps/server)
endif()

if(QCODE_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# === Installation ===
include(GNUInstallDirs)

set(QCODE_INSTALL_TARGETS qcode-core)
if(QCODE_BUILD_TUI)
    list(APPEND QCODE_INSTALL_TARGETS qcode-tui)
endif()
if(QCODE_BUILD_SERVER)
    list(APPEND QCODE_INSTALL_TARGETS qcode-server)
endif()
if(QCODE_BUILD_CLI)
    list(APPEND QCODE_INSTALL_TARGETS qcode-cli)
endif()

install(TARGETS ${QCODE_INSTALL_TARGETS}
    EXPORT qcode-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY libqcode/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY include/ai
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT qcode-targets
    FILE qcode-targets.cmake
    NAMESPACE qcode::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcode
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qcode-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/qcode-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcode
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/qcode-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/qcode-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/qcode-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcode
)

message(STATUS "")
message(STATUS "qcode ${PROJECT_VERSION} Configuration Summary")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build TUI: ${QCODE_BUILD_TUI}")
message(STATUS "  Build Server: ${QCODE_BUILD_SERVER}")
message(STATUS "  Build CLI: ${QCODE_BUILD_CLI}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
