cmake_minimum_required(VERSION 3.16)
project(qcode-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common compile definitions
set(HTTPLIB_COMPILE_DEFS
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    CPPHTTPLIB_THREAD_POOL_COUNT=8
)

# Find dependencies
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# Core library
add_library(qcode-core)

# Include directories.
# Canonical public API: <qcode/...> via libqcode/include.
# Legacy <ai/...> shims live under ${CMAKE_SOURCE_DIR}/include.
target_include_directories(qcode-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/md4c>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/httplib-header-only>
    $<INSTALL_INTERFACE:include>
)

# Private headers live next to sources (e.g. http/http_request_handler.h).
target_include_directories(qcode-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Source files
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
    src/providers/registry_init.cpp
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
    src/tools/tool_descriptors.cpp

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

    # Context
    src/context/context_manager.cpp

    # Config
    src/config/config.cpp

    # DB
    src/db/db.cpp

    # Bus
    src/bus/impl.cpp

    # System prompt
    src/system_prompt/system_prompt.cpp

    # Render
    src/render/message_render.cpp
    src/render/markdown.cpp
    src/render/tool_renderers.cpp
    ${CMAKE_SOURCE_DIR}/third_party/md4c/md4c.c

    # Store
    src/store/store.cpp

    # Chat
    src/chat/chat_bus.cpp

    # Commands
    src/commands/commands.cpp

    # Langfuse
    src/langfuse/tracer.cpp
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/third_party/md4c/md4c.c
    PROPERTIES LANGUAGE C COMPILE_FLAGS "-std=c99"
)

# Dependencies
# Keep third-party deps as BUILD_INTERFACE so install(EXPORT) does not require
# them to be in an export set (matches the pre-migration SDK layout).
target_link_libraries(qcode-core PUBLIC
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

if(TARGET ftxui::component)
    target_link_libraries(qcode-core PUBLIC
        $<BUILD_INTERFACE:ftxui::screen>
        $<BUILD_INTERFACE:ftxui::dom>
        $<BUILD_INTERFACE:ftxui::component>
    )
endif()

# Compile options
target_compile_options(qcode-core PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Compile definitions
# Feature macros required by the legacy include/ai/*.h availability guards.
target_compile_definitions(qcode-core PUBLIC
    ${HTTPLIB_COMPILE_DEFS}
    AI_SDK_HAS_OPENAI=1
    AI_SDK_HAS_ANTHROPIC=1
    AI_SDK_HAS_ANTIGRAVITY=1
    AI_SDK_HAS_CURSOR=1
    AI_SDK_HAS_LANGFUSE=1
    AI_SDK_HAS_REGISTRY=1
)

# Install
install(TARGETS qcode-core
    EXPORT qcode-core-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT qcode-core-targets
    FILE qcode-core-targets.cmake
    NAMESPACE qcode::
    DESTINATION lib/cmake/qcode-core
)
