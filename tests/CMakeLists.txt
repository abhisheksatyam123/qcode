# Unit / integration tests. Link qcode::ui because markdown/TUI tests need FTXUI.

add_executable(qcode_tests
    # Unit tests
    unit/openai_client_test.cpp
    unit/anthropic_client_test.cpp
    unit/registry_test.cpp
    unit/mock_provider_test.cpp
    unit/provider_factory_test.cpp
    unit/tui_bus_test.cpp
    unit/agent_mode_test.cpp
    unit/task_tool_contract_test.cpp
    unit/bus_json_codec_test.cpp
    unit/session_store_test.cpp
    unit/tool_catalog_test.cpp
    unit/commands_test.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/tui/src/prompt_wrap.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/tui/src/clipboard.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/bus_json_codec.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/workspace_fs.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/http_utils.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/server_routes.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/routes/route_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/routes/route_session.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/routes/route_session_ops.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/routes/session_runtime.cpp
    ${CMAKE_SOURCE_DIR}/apps/qcode/server/src/routes/route_fs.cpp
    unit/server_routes_test.cpp

    # Integration tests
    integration/openai_integration_test.cpp
    integration/anthropic_integration_test.cpp
    integration/tool_calling_integration_test.cpp

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
        ${CMAKE_SOURCE_DIR}/apps/qcode/server/include
        ${CMAKE_SOURCE_DIR}/apps/qcode/tui/include
        ${CMAKE_SOURCE_DIR}/apps/qcode/server/src
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

if(NOT QCODE_CXX_MODULES)
    target_precompile_headers(qcode_tests PRIVATE
        <algorithm>
        <chrono>
        <filesystem>
        <functional>
        <memory>
        <optional>
        <string>
        <string_view>
        <thread>
        <unordered_map>
        <utility>
        <vector>
        <gtest/gtest.h>
        <gmock/gmock.h>
        <nlohmann/json.hpp>
        <httplib.h>
    )
endif()

target_compile_definitions(qcode_tests PRIVATE
    QCODE_TESTING=1
    QCODE_HAS_REGISTRY=1
    QCODE_HAS_ANTIGRAVITY=1
    QCODE_HAS_OPENAI=1
    QCODE_HAS_ANTHROPIC=1
    QCODE_HAS_CURSOR=1
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

# Unified qcode dispatcher smoke tests: every mode must be reachable and exit
# cleanly without hanging when no server/TTY is available.
if(TARGET qcode)
    add_test(NAME QcodeHelpExits COMMAND qcode --help)
    set_tests_properties(QcodeHelpExits PROPERTIES
        TIMEOUT 5
        PASS_REGULAR_EXPRESSION "Usage: qcode"
    )

    add_test(NAME QcodeVersionExits COMMAND qcode --version)
    set_tests_properties(QcodeVersionExits PROPERTIES
        TIMEOUT 5
        PASS_REGULAR_EXPRESSION "qcode [0-9]+"
    )

    add_test(NAME QcodeTuiHelpExits COMMAND qcode tui --help)
    set_tests_properties(QcodeTuiHelpExits PROPERTIES
        TIMEOUT 5
        PASS_REGULAR_EXPRESSION "Usage: qcode"
    )

    add_test(NAME QcodeServeHelpExits COMMAND qcode serve --help)
    set_tests_properties(QcodeServeHelpExits PROPERTIES
        TIMEOUT 5
        PASS_REGULAR_EXPRESSION "Usage: qcode serve"
    )

    add_test(NAME QcodePromptHelpExits COMMAND qcode prompt --help)
    set_tests_properties(QcodePromptHelpExits PROPERTIES
        TIMEOUT 5
        PASS_REGULAR_EXPRESSION "Usage: qcode prompt"
    )

    add_test(NAME QcodeUnknownModeExits COMMAND qcode --not-a-mode)
    set_tests_properties(QcodeUnknownModeExits PROPERTIES
        TIMEOUT 5
        WILL_FAIL TRUE
    )

    add_test(NAME QcodeNoTtyExits COMMAND qcode)
    set_tests_properties(QcodeNoTtyExits PROPERTIES
        TIMEOUT 5
        WILL_FAIL TRUE
    )

    # Headless perf guard: startup latency and RSS of `qcode serve` against
    # scripts/perf_baseline.json. Linux only (reads /proc). Filter with -L perf.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME PerfGuardServe
            COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/perf_guard.py
                --binary $<TARGET_FILE:qcode>
                --baseline ${CMAKE_SOURCE_DIR}/scripts/perf_baseline.json
                --turns 50)
        set_tests_properties(PerfGuardServe PROPERTIES
            LABELS perf
            TIMEOUT 120
        )
    endif()
endif()

# Headless TUI frame benchmark (not a test): qcode_tui_bench [messages] [frames]
if(TARGET qcode-frontend-tui)
    add_executable(qcode_tui_bench bench/tui_render_bench.cpp)
    target_link_libraries(qcode_tui_bench PRIVATE qcode-frontend-tui)
    set_target_properties(qcode_tui_bench PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
endif()
