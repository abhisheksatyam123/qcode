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

# Named modules (`import qcode.config`) on host Clang + Ninja with CMake 3.28+
# and clang-scan-deps. Android, GCC, and Unix Makefiles keep the header surface.
find_program(QCODE_CLANG_SCAN_DEPS NAMES clang-scan-deps clang-scan-deps-18)
if(NOT ANDROID
   AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.28"
   AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   AND QCODE_CLANG_SCAN_DEPS
   AND CMAKE_GENERATOR MATCHES "Ninja")
    set(QCODE_CXX_MODULES ON)
    set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${QCODE_CLANG_SCAN_DEPS}")
else()
    set(QCODE_CXX_MODULES OFF)
endif()

# Enable ccache if available. Skip it when scanning C++20 named modules;
# BMI + ccache is still unreliable on GCC 13 / Clang 18.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT QCODE_CXX_MODULES)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Build options
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(QCODE_BUILD_APP "Build the unified qcode application" ON)
option(QCODE_USE_FAST_LINKER "Prefer mold/lld for faster host debug links" ON)
option(QCODE_DEBUG_OPTIMIZE "Keep environment optimization flags in Debug builds" OFF)

# Developer tooling options
option(QCODE_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(QCODE_ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)

if(QCODE_ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    endif()
endif()

if(QCODE_ENABLE_SANITIZERS)
    if(NOT MSVC)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Conda and distro shells often export CFLAGS/CXXFLAGS with -O2/-O3. Those leak
# into Debug builds, making compiles and links several times slower. Debug is
# unoptimized by default; opt in with QCODE_DEBUG_OPTIMIZE if needed.
if(NOT MSVC AND NOT QCODE_DEBUG_OPTIMIZE)
    add_compile_options("$<$<CONFIG:Debug>:-O0>")
endif()

# Release-style builds: -O2 + NDEBUG (CMake's default Release uses -O3).
# RelWithDebInfo already defaults to "-O2 -g -DNDEBUG" and is left as-is.
if(NOT MSVC)
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

# Per-function / per-data sections and dead-section removal, non-Debug only.
if(NOT MSVC)
    add_compile_options("$<$<NOT:$<CONFIG:Debug>>:-ffunction-sections>")
    add_compile_options("$<$<NOT:$<CONFIG:Debug>>:-fdata-sections>")
endif()
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
    add_link_options("$<$<NOT:$<CONFIG:Debug>>:LINKER:--gc-sections>")
endif()

# Link-time optimisation for Release only, when the toolchain supports it.
include(CheckIPOSupported)
check_ipo_supported(RESULT QCODE_IPO_SUPPORTED OUTPUT QCODE_IPO_MSG LANGUAGES C CXX)
if(QCODE_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "IPO/LTO unavailable: ${QCODE_IPO_MSG}")
endif()

# GCC's LTO plugin is not compatible with every alternative linker. Keep the
# fast linker for non-LTO configurations, where it mainly speeds large links.
set(QCODE_ACTIVE_LINKER "system")
if(QCODE_USE_FAST_LINKER
   AND NOT MSVC
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND NOT CMAKE_BUILD_TYPE MATCHES "Release")
    find_program(QCODE_MOLD_EXECUTABLE NAMES mold)
    if(QCODE_MOLD_EXECUTABLE)
        add_link_options(-fuse-ld=mold)
        set(QCODE_ACTIVE_LINKER "mold")
        message(STATUS "Fast linker: mold")
    else()
        find_program(QCODE_LLD_EXECUTABLE NAMES ld.lld lld)
        if(QCODE_LLD_EXECUTABLE)
            add_link_options(-fuse-ld=lld)
            set(QCODE_ACTIVE_LINKER "lld")
            message(STATUS "Fast linker: lld")
        endif()
    endif()
endif()

set(HTTPLIB_COMPILE_DEFS
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_ZLIB_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    CPPHTTPLIB_THREAD_POOL_COUNT=64
)

add_subdirectory(third_party)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# FTXUI is required by libqcode src/ui/ (markdown/message/tool UI),
# not only by the TUI app binary.
include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

add_subdirectory(libqcode)

if(QCODE_BUILD_APP)
    add_subdirectory(apps/qcode)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# === Installation ===
include(GNUInstallDirs)

set(QCODE_INSTALL_TARGETS qcode-engine qcode-ui)
if(QCODE_BUILD_APP)
    list(APPEND QCODE_INSTALL_TARGETS qcode)
endif()

if(QCODE_CXX_MODULES)
    list(REMOVE_ITEM QCODE_INSTALL_TARGETS qcode-engine)
    install(TARGETS qcode-engine
        EXPORT qcode-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/cxx/qcode
    )
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

if(QCODE_BUILD_APP)
    install(DIRECTORY apps/webui/src/
        DESTINATION ${CMAKE_INSTALL_BINDIR}/webui
    )
endif()

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
message(STATUS "  C++ named modules: ${QCODE_CXX_MODULES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build app: ${QCODE_BUILD_APP} (qcode: tui | serve | prompt)")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Strict warnings: ${QCODE_ENABLE_WARNINGS}")
message(STATUS "  Sanitizers: ${QCODE_ENABLE_SANITIZERS}")
message(STATUS "  Fast linker: ${QCODE_ACTIVE_LINKER}")
message(STATUS "")
