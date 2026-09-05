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
    src/core/embedding_options.cpp
    src/core/generate_options.cpp
    src/core/random.cpp
    src/core/uuid.cpp
    src/core/in_process_bus.cpp
    src/core/http_request_handler.cpp
    src/core/ssl_config.cpp

    # config
    src/config/config.cpp

    # providers
    src/providers/base_provider_client.cpp
    src/providers/registry.cpp
    src/providers/zen_route.cpp
    src/providers/provider_profile.cpp
    src/providers/authenticated_providers.cpp
    src/providers/langfuse_tracer.cpp
    src/providers/openai/openai_client.cpp
    src/providers/openai/openai_request_builder.cpp
    src/providers/openai/openai_response_parser.cpp
    src/providers/openai/openai_stream.cpp
    src/providers/openai/openai_factory.cpp
    src/providers/anthropic/anthropic_client.cpp
    src/providers/anthropic/anthropic_request_builder.cpp
    src/providers/anthropic/anthropic_response_parser.cpp
    src/providers/anthropic/anthropic_stream.cpp
    src/providers/anthropic/anthropic_factory.cpp
    src/providers/antigravity/antigravity_client.cpp
    src/providers/antigravity/antigravity_request_builder.cpp
    src/providers/antigravity/antigravity_response_parser.cpp
    src/providers/antigravity/antigravity_stream.cpp
    src/providers/antigravity/antigravity_factory.cpp
    src/providers/cursor/cursor_client.cpp
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
    src/transform/transform.cpp
    src/transform/gemini_transform.cpp

    # generation
    src/generation/generation_controller.cpp
    src/generation/generation_service.cpp

    # session
    src/session/session_store.cpp
    src/session/study_store.cpp
    src/session/study_assistant.cpp
    src/session/system_prompt.cpp
    src/session/git_workspace.cpp
    src/session/clipboard.cpp
    src/session/token_budget.cpp

    # tools
    src/tools/tool_executor.cpp
    src/tools/bash_tool.cpp
    src/tools/task_tool.cpp
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

# Feature gates mirrored by the header #error guards (qcode/providers/*.h).
target_compile_definitions(qcode-engine PUBLIC
    ${HTTPLIB_COMPILE_DEFS}
    QCODE_HAS_OPENAI=1
    QCODE_HAS_ANTHROPIC=1
    QCODE_HAS_ANTIGRAVITY=1
    QCODE_HAS_CURSOR=1
    QCODE_HAS_LANGFUSE=1
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
    target_compile_options(qcode-engine PRIVATE -Wall -Wextra)
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
    target_compile_options(qcode-ui PRIVATE -Wall -Wextra)
endif()
