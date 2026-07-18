cmake_minimum_required(VERSION 3.16)

# Project definition
project(ai-sdk-cpp 
    VERSION 0.1.0
    DESCRIPTION "A unified C++ SDK for multiple AI/LLM providers"
    LANGUAGES C CXX
)

# C++20 requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clangd / LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(QCODE_BUILD_TUI "Build TUI application" ON)
option(QCODE_BUILD_SERVER "Build headless server" ON)
option(QCODE_BUILD_CLI "Build CLI client" ON)

# Common compile definitions
set(HTTPLIB_COMPILE_DEFS
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    CPPHTTPLIB_THREAD_POOL_COUNT=8
)

# Add third party directory with submodules
add_subdirectory(third_party)

# Find system OpenSSL / CURL
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# FTXUI is needed by libqcode render sources and the TUI app.
if(QCODE_BUILD_TUI)
    include(FetchContent)
    FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
        GIT_TAG v5.0.0
    )
    FetchContent_MakeAvailable(ftxui)
endif()

# === libqcode (core library) ===
add_subdirectory(libqcode)

# === TUI Application ===
if(QCODE_BUILD_TUI)
    add_subdirectory(apps/tui)
endif()

# === Server Application ===
if(QCODE_BUILD_SERVER)
    add_subdirectory(apps/server)
endif()

# === CLI Application ===
if(QCODE_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()

# === Examples ===
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# === Tests ===
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# === Installation ===
include(GNUInstallDirs)

# Install targets
set(ALL_TARGETS qcode-core)
if(QCODE_BUILD_TUI)
    list(APPEND ALL_TARGETS qcode-tui)
endif()
if(QCODE_BUILD_SERVER)
    list(APPEND ALL_TARGETS qcode-server qcode-cli)
endif()

install(TARGETS ${ALL_TARGETS}
    EXPORT ai-sdk-cpp-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install canonical headers (<qcode/...>) and legacy shims (<ai/...>).
install(DIRECTORY libqcode/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY include/ai
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Export targets
install(EXPORT ai-sdk-cpp-targets
    FILE ai-sdk-cpp-targets.cmake
    NAMESPACE ai::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ai-sdk-cpp
)

# Create and install config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ai-sdk-cpp-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ai-sdk-cpp-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ai-sdk-cpp
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ai-sdk-cpp-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ai-sdk-cpp-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ai-sdk-cpp-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ai-sdk-cpp
)

# Print configuration summary
message(STATUS "")
message(STATUS "ai-sdk-cpp ${PROJECT_VERSION} Configuration Summary")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build TUI: ${QCODE_BUILD_TUI}")
message(STATUS "  Build Server: ${QCODE_BUILD_SERVER}")
message(STATUS "  Build CLI: ${QCODE_BUILD_CLI}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
