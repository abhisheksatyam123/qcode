cmake_minimum_required(VERSION 3.22.1)

project(qcode_jni VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QCODE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." CACHE PATH "qcode repository root")
get_filename_component(QCODE_ROOT "${QCODE_ROOT}" ABSOLUTE)

set(QCODE_BUILD_TUI OFF CACHE BOOL "" FORCE)
set(QCODE_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(QCODE_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Prefer the Android sysroot for OpenSSL/CURL when present.
# Host-side NDK sysroot unpack tree lives under the unified build root.
# Legacy sibling dir (build-android-deps/) is kept as a fallback.
set(ANDROID_DEPS_SYSROOT "${QCODE_ROOT}/build/android-deps/sysroot-arm64-v8a")
if(NOT EXISTS "${ANDROID_DEPS_SYSROOT}")
  set(ANDROID_DEPS_SYSROOT "${QCODE_ROOT}/build-android-deps/sysroot-arm64-v8a")
endif()
if(EXISTS "${ANDROID_DEPS_SYSROOT}")
  list(PREPEND CMAKE_PREFIX_PATH "${ANDROID_DEPS_SYSROOT}")
  list(PREPEND CMAKE_FIND_ROOT_PATH "${ANDROID_DEPS_SYSROOT}")
  set(OPENSSL_ROOT_DIR "${ANDROID_DEPS_SYSROOT}" CACHE PATH "" FORCE)
  set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
  if(EXISTS "${ANDROID_DEPS_SYSROOT}/include/openssl/ssl.h")
    set(OPENSSL_INCLUDE_DIR "${ANDROID_DEPS_SYSROOT}/include" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${ANDROID_DEPS_SYSROOT}/lib/libssl.a")
    set(OPENSSL_SSL_LIBRARY "${ANDROID_DEPS_SYSROOT}/lib/libssl.a" CACHE FILEPATH "" FORCE)
  endif()
  if(EXISTS "${ANDROID_DEPS_SYSROOT}/lib/libcrypto.a")
    set(OPENSSL_CRYPTO_LIBRARY "${ANDROID_DEPS_SYSROOT}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
  endif()
  set(CURL_DIR "${ANDROID_DEPS_SYSROOT}/lib/cmake/CURL" CACHE PATH "" FORCE)
endif()

set(HTTPLIB_COMPILE_DEFS
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    CPPHTTPLIB_ZLIB_SUPPORT=1
    CPPHTTPLIB_THREAD_POOL_COUNT=8
)

add_subdirectory("${QCODE_ROOT}" qcode_build)

add_library(qcode_jni SHARED
    qcode_jni.cpp
    "${QCODE_ROOT}/apps/server/src/server_routes.cpp"
    "${QCODE_ROOT}/apps/server/src/json_codec.cpp"
    "${QCODE_ROOT}/apps/server/src/http_utils.cpp"
    "${QCODE_ROOT}/apps/server/src/workspace_fs.cpp"
    "${QCODE_ROOT}/apps/server/src/terminal_pty.cpp"
)

target_include_directories(qcode_jni PRIVATE
    "${QCODE_ROOT}/apps/server/include"
    "${QCODE_ROOT}/apps/server/src"
)

target_compile_definitions(qcode_jni PRIVATE
    ${HTTPLIB_COMPILE_DEFS}
    QCODE_SERVER_WEBUI_DIR=""
)

find_library(ANDROID_LOG_LIB log)

target_link_libraries(qcode_jni PRIVATE
    qcode::engine
    ${ANDROID_LOG_LIB}
    android
)

# Static OpenSSL/CURL on Android need libdl.
target_link_libraries(qcode_jni PRIVATE dl)
