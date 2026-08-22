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
    # Core & Types
    src/types/message.cpp
    src/types/stream_options.cpp
    src/types/stream_result.cpp
    src/types/embedding_options.cpp
    src/types/generate_options.cpp
    src/utils/random.cpp
    src/utils/uuid.cpp
    src/bus/in_process_bus.cpp
    src/config/config.cpp
    src/context/token_budget.cpp

    # Providers
    src/providers/base_provider_client.cpp
    src/providers/registry.cpp
    src/providers/authenticated_providers.cpp
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
    src/providers/cursor/cursor_proto.cpp
    src/transform/transform.cpp
    src/transform/gemini_transform.cpp
    src/langfuse/tracer.cpp

    # Session, Workspace & Tools
    src/session/session_store.cpp
    src/study/study_store.cpp
    src/study/study_assistant.cpp
    src/generation/generation_controller.cpp
    src/generation/generation_service.cpp
    src/tools/tool_executor.cpp
    src/tools/bash_tool.cpp
    src/tools/task_tool.cpp
    src/tools/multi_step_coordinator.cpp
    src/tools/tool_catalog.cpp
    src/workspace/git_workspace.cpp
    src/workspace/clipboard.cpp
    src/system_prompt/system_prompt.cpp

    # HTTP
    src/http/http_request_handler.cpp
    src/http/ssl_config.cpp
)

target_link_libraries(qcode-engine PUBLIC
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    CURL::libcurl
)

set_target_properties(qcode-engine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

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
    src/render/message_render.cpp
    src/render/markdown.cpp
    src/render/tool_renderers.cpp
    src/store/app_store.cpp
    src/commands/commands.cpp
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
