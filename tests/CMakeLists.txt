# Test executable for AI SDK C++
add_executable(ai_tests
    # Unit tests
    unit/openai_client_test.cpp
    unit/openai_responses_test.cpp
    unit/anthropic_client_test.cpp
    unit/types_test.cpp
    unit/openai_stream_test.cpp
    unit/anthropic_stream_test.cpp
    unit/openai_embeddings_test.cpp
    unit/registry_test.cpp
    unit/gemini_transform_test.cpp
    unit/antigravity_client_test.cpp
    unit/cursor_provider_test.cpp
    unit/bash_tool_test.cpp
    unit/tui_config_test.cpp
    unit/tui_bus_test.cpp
    ../libqcode/src/config/config.cpp
    ../libqcode/src/bus/impl.cpp

    # Integration tests
    integration/openai_integration_test.cpp
    integration/anthropic_integration_test.cpp
    integration/tool_calling_integration_test.cpp
    integration/openai_embeddings_integration_test.cpp
    integration/multi_step_duplicate_execution_test.cpp

    # Utility classes
    utils/mock_openai_client.cpp
    utils/mock_anthropic_client.cpp
    utils/test_fixtures.cpp
)

# Link against the main library and test dependencies
target_link_libraries(ai_tests
    PRIVATE
        qcode-core
        httplib::httplib  # Need direct access to httplib for internal headers
        concurrentqueue   # Need direct access to concurrentqueue
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Include directories for tests
target_include_directories(ai_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_SOURCE_DIR}/src  # Access to private headers
        ${CMAKE_SOURCE_DIR}/third_party/httplib-header-only
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/libqcode/src
        ${CMAKE_SOURCE_DIR}/libqcode/include
)

# Set test properties
set_target_properties(ai_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Compiler options for tests (less strict than main library)
if(MSVC)
    target_compile_options(ai_tests PRIVATE /W3)
else()
    target_compile_options(ai_tests PRIVATE -Wall -Wextra)
endif()

# Define preprocessor macros for tests
target_compile_definitions(ai_tests PRIVATE
    AI_SDK_TESTING=1
    AI_SDK_HAS_REGISTRY=1
    AI_SDK_HAS_ANTIGRAVITY=1
    AI_SDK_HAS_OPENAI=1
    AI_SDK_HAS_ANTHROPIC=1
    AI_SDK_HAS_CURSOR=1
    AI_SDK_HAS_LANGFUSE=1
    $<$<CONFIG:Debug>:AI_SDK_DEBUG_TESTS=1>
    # httplib configuration (must match main library)
    CPPHTTPLIB_OPENSSL_SUPPORT=1
    CPPHTTPLIB_BROTLI_SUPPORT=1
    CPPHTTPLIB_THREAD_POOL_COUNT=8
)

# Discover tests for CTest
include(GoogleTest)
gtest_discover_tests(ai_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES 
        TIMEOUT 30
)

# ClickHouse integration tests are opt-in: they require the clickhouse-cpp submodule
# (with its contrib/) and a live ClickHouse server, so they are excluded from the
# default test build.
if(BUILD_CLICKHOUSE_TESTS)
    target_sources(ai_tests PRIVATE
        integration/clickhouse_integration_test.cpp
    )
    target_link_libraries(ai_tests PRIVATE
        ClickHouse::Client
    )
endif()

