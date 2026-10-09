# Unit / integration tests. Link qcode::ui because markdown/TUI tests need FTXUI.

add_executable(qcode_tests
    # Unit tests
    unit/openai_client_test.cpp
    unit/markdown_test.cpp
    unit/task_notes_test.cpp
    unit/tool_timeout_test.cpp
    unit/openai_responses_test.cpp
    unit/anthropic_client_test.cpp
    unit/anthropic_oauth_test.cpp
    unit/anthropic_request_builder_test.cpp
    unit/types_test.cpp
    unit/openai_stream_test.cpp
    unit/stream_result_test.cpp
    unit/stream_step_test.cpp
    unit/anthropic_stream_test.cpp
    unit/openai_embeddings_test.cpp
    unit/registry_test.cpp
    unit/gemini_transform_test.cpp
    unit/provider_transform_test.cpp
    unit/antigravity_client_test.cpp
    unit/cursor_provider_test.cpp
    unit/bash_tool_test.cpp
    unit/image_tool_test.cpp
    unit/tui_config_test.cpp
    unit/tui_bus_test.cpp
    unit/retry_policy_test.cpp
    unit/model_performance_telemetry_test.cpp
    unit/session_stats_test.cpp
    unit/queued_prompt_store_test.cpp
    unit/queue_injection_test.cpp
    unit/session_reliability_test.cpp
    unit/dedupe_user_prompt_test.cpp
    unit/task_tool_test.cpp
    unit/task_target_test.cpp
    unit/subagent_router_test.cpp
    unit/subagent_stats_test.cpp
    unit/generation_continue_test.cpp
    unit/bus_json_codec_test.cpp
    unit/study_store_test.cpp
    unit/git_workspace_test.cpp
    unit/session_store_test.cpp
    unit/session_file_logger_test.cpp
    unit/token_budget_test.cpp
    unit/tool_catalog_test.cpp
    unit/workspace_fs_test.cpp
    unit/study_assistant_test.cpp
    unit/commands_test.cpp
    unit/tui_scroll_test.cpp
    unit/prompt_wrap_test.cpp
    unit/compaction_cache_test.cpp
    unit/session_title_test.cpp
    unit/image_attachment_test.cpp
    unit/transform_matrix_test.cpp
    ${CMAKE_SOURCE_DIR}/apps/tui/src/prompt_wrap.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/bus_json_codec.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/workspace_fs.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/http_utils.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/terminal_pty.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/server_routes.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_session.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_session_ops.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/session_runtime.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_terminal.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_fs.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_study.cpp
    ${CMAKE_SOURCE_DIR}/apps/server/src/routes/route_vision.cpp
    unit/server_routes_test.cpp
    unit/app_store_latency_test.cpp
    unit/anthropic_sse_test.cpp

    # Integration tests
    integration/openai_integration_test.cpp
    integration/anthropic_integration_test.cpp
    integration/tool_calling_integration_test.cpp
    integration/openai_embeddings_integration_test.cpp
    integration/multi_step_duplicate_execution_test.cpp

    # Test helpers
    utils/mock_openai_client.cpp
    utils/mock_anthropic_client.cpp
    utils/test_fixtures.cpp
)

target_link_libraries(qcode_tests
    PRIVATE
        qcode::ui
        httplib::httplib
        concurrentqueue
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_include_directories(qcode_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_SOURCE_DIR}/third_party/httplib-header-only
        ${CMAKE_SOURCE_DIR}/libqcode/src
        ${CMAKE_SOURCE_DIR}/libqcode/include
        ${CMAKE_SOURCE_DIR}/apps/server/include
        ${CMAKE_SOURCE_DIR}/apps/tui/include
        ${CMAKE_SOURCE_DIR}/apps/server/src
)

set_target_properties(qcode_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(QCODE_CXX_MODULES)
    target_sources(qcode_tests PRIVATE unit/cxx_modules_test.cpp)
    set_target_properties(qcode_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)
endif()

if(MSVC)
    target_compile_options(qcode_tests PRIVATE /W3)
else()
    target_compile_options(qcode_tests PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

target_compile_definitions(qcode_tests PRIVATE
    QCODE_TESTING=1
    QCODE_HAS_REGISTRY=1
    QCODE_HAS_ANTIGRAVITY=1
    QCODE_HAS_OPENAI=1
    QCODE_HAS_ANTHROPIC=1
    QCODE_HAS_CURSOR=1
    QCODE_HAS_LANGFUSE=1
    $<$<CONFIG:Debug>:QCODE_DEBUG_TESTS=1>
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    QCODE_SERVER_WEBUI_DIR="${CMAKE_SOURCE_DIR}/apps/webui/src"
    QCODE_VERSION="${PROJECT_VERSION}" 
)

include(GoogleTest)
gtest_discover_tests(qcode_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
        TIMEOUT 30
)

# ClickHouse integration tests are opt-in.
if(BUILD_CLICKHOUSE_TESTS)
    target_sources(qcode_tests PRIVATE
        integration/clickhouse_integration_test.cpp
    )
    target_link_libraries(qcode_tests PRIVATE
        ClickHouse::Client
    )
endif()

# qcode-tui must not hang when invoked without a TTY (help / unknown / no args).
if(TARGET qcode-tui)
    add_test(NAME TuiHelpExits COMMAND qcode-tui --help)
    set_tests_properties(TuiHelpExits PROPERTIES
        TIMEOUT 5
        PASS_REGULAR_EXPRESSION "Usage:"
    )
    add_test(NAME TuiUnknownArgExits COMMAND qcode-tui --not-a-flag)
    set_tests_properties(TuiUnknownArgExits PROPERTIES
        TIMEOUT 5
        WILL_FAIL TRUE
    )
    add_test(NAME TuiNoTtyExits COMMAND qcode-tui)
    set_tests_properties(TuiNoTtyExits PROPERTIES
        TIMEOUT 5
        WILL_FAIL TRUE
    )
endif()
