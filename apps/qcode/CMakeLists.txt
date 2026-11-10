# Unified qcode application.
#
# One executable exposes three runtime modes:
#   qcode             default TUI
#   qcode tui         TUI
#   qcode serve       HTTP API + WebUI
#   qcode prompt      one-shot HTTP client
#
# Each frontend remains a separate static library so the mode implementations
# stay isolated while users install and run a single binary.

set(QCODE_WEBUI_SRC "${CMAKE_SOURCE_DIR}/apps/webui/src")

# ── TUI mode ────────────────────────────────────────────────────────────────
add_library(qcode-frontend-tui STATIC
    tui/src/tui.cpp
    tui/src/overlays.cpp
    tui/src/git_monitor.cpp
    tui/src/views.cpp
    tui/src/prompt_wrap.cpp
    tui/src/clipboard.cpp
    tui/src/pickers.cpp
    tui/src/views_toast.cpp
    tui/src/file_diff_preview.cpp
)
target_include_directories(qcode-frontend-tui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tui/include
)
target_link_libraries(qcode-frontend-tui PUBLIC qcode::ui)
target_precompile_headers(qcode-frontend-tui PRIVATE
    <functional>
    <memory>
    <string>
    <vector>
    <nlohmann/json.hpp>
    <ftxui/component/component.hpp>
    <ftxui/component/screen_interactive.hpp>
    <ftxui/dom/elements.hpp>
)

# ── Server mode ─────────────────────────────────────────────────────────────
add_library(qcode-frontend-server STATIC
    server/src/server.cpp
    server/src/server_routes.cpp
    server/src/routes/route_system.cpp
    server/src/routes/route_session.cpp
    server/src/routes/route_session_ops.cpp
    server/src/routes/session_runtime.cpp
    server/src/routes/route_fs.cpp
    server/src/bus_json_codec.cpp
    server/src/http_utils.cpp
    server/src/workspace_fs.cpp
)
target_include_directories(qcode-frontend-server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server/src
)
target_compile_definitions(qcode-frontend-server PRIVATE
    QCODE_SERVER_WEBUI_DIR="${QCODE_WEBUI_SRC}"
    QCODE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(qcode-frontend-server PUBLIC qcode::engine)
target_precompile_headers(qcode-frontend-server PRIVATE
    <functional>
    <memory>
    <string>
    <unordered_map>
    <vector>
    <httplib.h>
    <nlohmann/json.hpp>
)

# ── Prompt mode ─────────────────────────────────────────────────────────────
add_library(qcode-frontend-cli STATIC
    cli/src/cli.cpp
)
target_include_directories(qcode-frontend-cli
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libqcode/src
)
target_link_libraries(qcode-frontend-cli PUBLIC qcode::engine)

# ── Single executable ───────────────────────────────────────────────────────
add_executable(qcode main.cpp)
target_include_directories(qcode PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(qcode PRIVATE
    QCODE_VERSION="${PROJECT_VERSION}"
    QCODE_GIT_DESCRIBE="${QCODE_GIT_DESCRIBE}"
    QCODE_GIT_COMMIT="${QCODE_GIT_COMMIT}"
)
target_link_libraries(qcode PRIVATE
    qcode-frontend-tui
    qcode-frontend-server
    qcode-frontend-cli
)

# The server mode resolves ./webui next to the executable before falling back
# to the source tree, so keep the assets beside the unified binary.
add_custom_command(TARGET qcode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${QCODE_WEBUI_SRC}"
        "$<TARGET_FILE_DIR:qcode>/webui"
    # Pre-compressed copies (*.gz) of the text assets, served with
    # Content-Encoding: gzip. Only the installed copy gets them.
    COMMAND ${CMAKE_COMMAND}
        -DWEBUI_DIR="$<TARGET_FILE_DIR:qcode>/webui"
        -P "${CMAKE_SOURCE_DIR}/cmake/compress_webui.cmake"
    COMMENT "Copying WebUI next to qcode and pre-compressing it"
)
