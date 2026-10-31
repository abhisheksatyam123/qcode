# libqcode CMake configuration: split into headless qcode::engine and UI qcode::ui targets.

find_package(OpenSSL REQUIRED)
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json 3.10 REQUIRED)
endif()
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# ── 1. Headless Engine Library (qcode-engine / qcode::engine) ──
add_library(qcode-engine)
add_library(qcode::engine ALIAS qcode-engine)

get_filename_component(QCODE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

target_include_directories(qcode-engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${QCODE_REPO_ROOT}/third_party/httplib-header-only>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(qcode-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(qcode-engine PRIVATE
    # core
    src/core/message.cpp
    src/core/stream_options.cpp
    src/core/stream_result.cpp
    src/core/generate_options.cpp
    src/core/random.cpp
    src/core/in_process_bus.cpp
    src/core/http_request_handler.cpp
    src/core/http_client.cpp
    src/core/zen_http_client.cpp
    src/core/zen_boringssl_transport.cpp
    src/core/ssl_config.cpp


    # config
    src/config/config.cpp

    # providers/internal — private shared client (not in include/)
    src/providers/internal/base_provider_client.cpp
    # providers — public module impl (mirrors include/qcode/providers)
    src/providers/registry.cpp
    src/providers/zen_route.cpp
    src/providers/provider_profile.cpp
    src/providers/authenticated_providers.cpp
    src/providers/openai/openai_client.cpp
    src/providers/openai/openai_request_builder.cpp
    src/providers/openai/openai_response_parser.cpp
    src/providers/openai/openai_stream.cpp
    src/providers/openai/openai_sse.cpp
    src/providers/openai/openai_cache.cpp
    src/providers/openai/openai_responses.cpp
    src/providers/openai/openai_factory.cpp
    src/providers/anthropic/anthropic_oauth.cpp
    src/providers/anthropic/anthropic_client.cpp
    src/providers/anthropic/anthropic_request_builder.cpp
    src/providers/anthropic/anthropic_thinking.cpp
    src/providers/anthropic/anthropic_response_parser.cpp
    src/providers/anthropic/anthropic_stream.cpp
    src/providers/anthropic/anthropic_factory.cpp
    src/providers/antigravity/antigravity_client.cpp
    src/providers/antigravity/antigravity_request_builder.cpp
    src/providers/antigravity/antigravity_response_parser.cpp
    src/providers/antigravity/antigravity_factory.cpp
    src/providers/cursor/cursor_client.cpp
    src/providers/cursor/cursor_agent_session.cpp
    src/providers/cursor/cursor_request_builder.cpp
    src/providers/cursor/cursor_response_parser.cpp
    src/providers/cursor/cursor_stream.cpp
    src/providers/cursor/cursor_factory.cpp
    src/providers/cursor/cursor_http2.cpp
    src/providers/cursor/cursor_bidi.cpp
    src/providers/cursor/cursor_proto.cpp
    src/providers/cursor/cursor_exec.cpp
    src/providers/cursor/cursor_kv.cpp

    # transform
    src/transform/provider_transform.cpp
    src/transform/gemini_transform.cpp

    # generation
    src/generation/generation_controller.cpp
    src/generation/generation_service.cpp
    src/generation/stream_step.cpp
    src/generation/turn_prefix.cpp
    src/generation/call_usage.cpp
    src/generation/session_title.cpp

    # session
    src/compaction/compaction_request.cpp
    src/session/session_store.cpp
    src/session/session_telemetry.cpp
    src/session/usage_stats.cpp
    src/session/system_prompt.cpp
    src/session/task_notes.cpp
    src/session/git_workspace.cpp
    src/session/token_budget.cpp

    # tools
    src/tools/tool_executor.cpp
    src/tools/bash_tool.cpp
    src/tools/task_tool.cpp
    src/tools/task_target.cpp
    src/tools/image_tool.cpp
    src/tools/multi_step_coordinator.cpp
    src/tools/tool_catalog.cpp
)

# Keep third-party deps as BUILD_INTERFACE so install(EXPORT) does not require
# them to be in an export set.
target_link_libraries(qcode-engine PUBLIC
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    CURL::libcurl
    $<BUILD_INTERFACE:httplib::httplib>
    $<BUILD_INTERFACE:sqlite3>
    $<BUILD_INTERFACE:stduuid::stduuid>
    $<BUILD_INTERFACE:concurrentqueue::concurrentqueue>
)

# BoringSSL-backed transport for OpenCode Zen. Its ClientHello is accepted by
# Zen's free-tier gate where system OpenSSL's is not. Build the bundled
# submodule when available; otherwise fall back to the prebuilt prefix tree.
if(EXISTS "${QCODE_REPO_ROOT}/third_party/boringssl/CMakeLists.txt")
  set(BORINGSSL_PREFIX "qcode_bssl" CACHE STRING
      "Symbol prefix for the bundled BoringSSL build")
  set(BUILD_TESTING OFF CACHE BOOL "Build BoringSSL tests" FORCE)
  set(_qcode_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
  set(_qcode_saved_c_flags "${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "")
  set(CMAKE_C_FLAGS "")
  add_subdirectory(${QCODE_REPO_ROOT}/third_party/boringssl ${CMAKE_BINARY_DIR}/third_party/boringssl EXCLUDE_FROM_ALL)
  set(CMAKE_CXX_FLAGS "${_qcode_saved_cxx_flags}")
  set(CMAKE_C_FLAGS "${_qcode_saved_c_flags}")

  function(qcode_disable_warnings_in_dir dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t ${targets})
      if(TARGET ${t})
        target_compile_options(${t} PRIVATE -w)
      endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(sd ${subdirs})
      qcode_disable_warnings_in_dir("${sd}")
    endforeach()
  endfunction()
  qcode_disable_warnings_in_dir("${CMAKE_BINARY_DIR}/third_party/boringssl")
  set(QCODE_BORINGSSL_INCLUDE_DIR
      "${QCODE_REPO_ROOT}/third_party/boringssl/include")
  set(QCODE_BORINGSSL_LIBRARIES ssl crypto)
else()
  set(QCODE_BORINGSSL_INCLUDE_DIR "/tmp/opencode/boringssl/include" CACHE PATH
      "BoringSSL include directory")
  set(QCODE_BORINGSSL_SSL_LIB "/tmp/opencode/boringssl/build_prefix/libssl.a"
      CACHE FILEPATH "BoringSSL ssl library")
  set(QCODE_BORINGSSL_CRYPTO_LIB "/tmp/opencode/boringssl/build_prefix/libcrypto.a"
      CACHE FILEPATH "BoringSSL crypto library")
  add_library(qcode-boringssl-ssl STATIC IMPORTED)
  set_target_properties(qcode-boringssl-ssl PROPERTIES
      IMPORTED_LOCATION "${QCODE_BORINGSSL_SSL_LIB}")
  add_library(qcode-boringssl-crypto STATIC IMPORTED)
  set_target_properties(qcode-boringssl-crypto PROPERTIES
      IMPORTED_LOCATION "${QCODE_BORINGSSL_CRYPTO_LIB}")
  set(QCODE_BORINGSSL_LIBRARIES
      qcode-boringssl-ssl qcode-boringssl-crypto)
endif()

# Keep BoringSSL's include path on this source only; linking the BoringSSL
# targets directly would leak BoringSSL's headers into every qcode source that
# includes <openssl/*.h> via httplib.
set_source_files_properties(
    src/core/zen_boringssl_transport.cpp
    PROPERTIES
      INCLUDE_DIRECTORIES "${QCODE_BORINGSSL_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_SOURCE_DIR}/src;${QCODE_REPO_ROOT}/third_party/httplib-header-only"
      COMPILE_DEFINITIONS "BORINGSSL_PREFIX=qcode_bssl;CPPHTTPLIB_OPENSSL_SUPPORT=1"
      SKIP_PRECOMPILE_HEADERS ON
)

if(TARGET ssl)
  set(QCODE_BORINGSSL_SSL_LIB "$<TARGET_FILE:ssl>")
  set(QCODE_BORINGSSL_CRYPTO_LIB "$<TARGET_FILE:crypto>")
endif()

target_link_libraries(qcode-engine PRIVATE
    ${QCODE_BORINGSSL_SSL_LIB}
    ${QCODE_BORINGSSL_CRYPTO_LIB}
    pthread dl)

# Feature gates mirrored by the header #error guards (qcode/providers/*.h).
target_compile_definitions(qcode-engine PUBLIC
    ${HTTPLIB_COMPILE_DEFS}
    QCODE_HAS_OPENAI=1
    QCODE_HAS_ANTHROPIC=1
    QCODE_HAS_ANTIGRAVITY=1
    QCODE_HAS_CURSOR=1
    QCODE_HAS_REGISTRY=1
)

set_target_properties(qcode-engine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(QCODE_CXX_MODULES)
    target_sources(qcode-engine PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/qcode.config.cppm"
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/qcode.transform.cppm"
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/qcode.generation.cppm"
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/qcode.cppm"
    )
    set_target_properties(qcode-engine PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_compile_definitions(qcode-engine PUBLIC QCODE_HAS_CXX_MODULES=1)
else()
    # PCH and C++20 named modules do not mix on GCC/Clang.
    target_precompile_headers(qcode-engine PRIVATE
        <algorithm>
        <chrono>
        <functional>
        <map>
        <memory>
        <string>
        <vector>
        <nlohmann/json.hpp>
        <httplib.h>
    )
endif()


if(MSVC)
    target_compile_options(qcode-engine PRIVATE /W3)
else()
    target_compile_options(qcode-engine PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()



# ── 2. UI & Rendering Library (qcode-ui / qcode::ui) ──
add_library(qcode-ui)
add_library(qcode::ui ALIAS qcode-ui)

target_include_directories(qcode-ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${QCODE_REPO_ROOT}/third_party/md4c>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(qcode-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(qcode-ui PRIVATE
    src/ui/message_render.cpp
    src/ui/markdown.cpp
    src/ui/tool_renderers.cpp
    src/ui/app_store.cpp
    src/ui/commands.cpp
    ${QCODE_REPO_ROOT}/third_party/md4c/md4c.c
)

set_source_files_properties(
    ${QCODE_REPO_ROOT}/third_party/md4c/md4c.c
    PROPERTIES LANGUAGE C COMPILE_FLAGS "-std=c99"
)

target_link_libraries(qcode-ui PUBLIC
    qcode-engine
    ftxui::screen
    ftxui::dom
    ftxui::component
)

set_target_properties(qcode-ui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Backward-compatibility alias targets
add_library(qcode-core ALIAS qcode-ui)
add_library(qcode::core ALIAS qcode-ui)

if(MSVC)
    target_compile_options(qcode-ui PRIVATE /W3)
else()
    target_compile_options(qcode-ui PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
