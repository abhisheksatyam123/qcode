# Unit / integration tests for qcode::ui (and qcode::engine).

add_executable(qcode_tests
    # Unit tests
    unit/openai_client_test.cpp
    unit/markdown_test.cpp
    unit/tool_timeout_test.cpp
    unit/openai_responses_test.cpp
    unit/anthropic_client_test.cpp
    unit/anthropic_request_builder_test.cpp
    unit/types_test.cpp
    unit/openai_stream_test.cpp
    unit/stream_result_test.cpp
    unit/anthropic_stream_test.cpp
    unit/openai_embeddings_test.cpp
    unit/registry_test.cpp
    unit/gemini_transform_test.cpp
    unit/provider_transform_test.cpp
    unit/antigravity_client_test.cpp
    unit/cursor_provider_test.cpp
    unit/bash_tool_test.cpp
    unit/tui_config_test.cpp
    unit/tui_bus_test.cpp
    unit/retry_policy_test.cpp
    unit/model_performance_telemetry_test.cpp
    unit/session_stats_test.cpp
    unit/queued_prompt_store_test.cpp
    unit/session_reliability_test.cpp
    unit/dedupe_user_prompt_test.cpp
    unit/task_tool_test.cpp
    unit/generation_continue_test.cpp

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
    target_compile_options(qcode_tests PRIVATE -Wall -Wextra)
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
    CPPHTTPLIB_THREAD_POOL_COUNT=8
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
