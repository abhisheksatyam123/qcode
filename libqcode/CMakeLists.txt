# qcode-core — shared library (providers, session, generation, render, tools).

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

add_library(qcode-core)
add_library(qcode::core ALIAS qcode-core)

target_include_directories(qcode-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/md4c>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/httplib-header-only>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(qcode-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(qcode-core PRIVATE
    # Types
    src/types/message.cpp
    src/types/stream_options.cpp
    src/types/stream_result.cpp
    src/types/embedding_options.cpp
    src/types/generate_options.cpp

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

    # Tools
    src/tools/tool_executor.cpp
    src/tools/bash_tool.cpp
    src/tools/task_tool.cpp
    src/tools/multi_step_coordinator.cpp
    src/tools/tool_catalog.cpp

    # HTTP
    src/http/http_request_handler.cpp

    # Utils
    src/utils/random.cpp
    src/utils/uuid.cpp

    # Transform
    src/transform/transform.cpp
    src/transform/gemini_transform.cpp

    # Generation
    src/generation/generation_controller.cpp
    src/generation/generation_service.cpp

    # Context
    src/context/token_budget.cpp

    # Config
    src/config/config.cpp

    # Workspace
    src/workspace/git_workspace.cpp
    src/workspace/clipboard.cpp

    # Session persistence
    src/session/session_store.cpp

    # Bus
    src/bus/in_process_bus.cpp

    # System prompt
    src/system_prompt/system_prompt.cpp

    # Render
    src/render/message_render.cpp
    src/render/markdown.cpp
    src/render/tool_renderers.cpp
    ${CMAKE_SOURCE_DIR}/third_party/md4c/md4c.c

    # Store
    src/store/app_store.cpp

    # Commands
    src/commands/commands.cpp

    # Langfuse
    src/langfuse/tracer.cpp
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/third_party/md4c/md4c.c
    PROPERTIES LANGUAGE C COMPILE_FLAGS "-std=c99"
)

# Keep third-party deps as BUILD_INTERFACE so install(EXPORT) does not require
# them to be in an export set.
target_link_libraries(qcode-core PUBLIC
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    CURL::libcurl
    ftxui::screen
    ftxui::dom
    ftxui::component
    $<BUILD_INTERFACE:httplib::httplib>
    $<BUILD_INTERFACE:sqlite3>
    $<BUILD_INTERFACE:stduuid::stduuid>
    $<BUILD_INTERFACE:concurrentqueue::concurrentqueue>
)

target_compile_options(qcode-core PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

target_compile_definitions(qcode-core PUBLIC
    ${HTTPLIB_COMPILE_DEFS}
    AI_SDK_HAS_OPENAI=1
    AI_SDK_HAS_ANTHROPIC=1
    AI_SDK_HAS_ANTIGRAVITY=1
    AI_SDK_HAS_CURSOR=1
    AI_SDK_HAS_LANGFUSE=1
    AI_SDK_HAS_REGISTRY=1
)
