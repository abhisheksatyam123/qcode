# qcode-server — headless HTTP API + WebUI.

set(QCODE_WEBUI_SRC "${CMAKE_SOURCE_DIR}/apps/webui/src")

add_executable(qcode-server
    src/server_main.cpp
    src/json_codec.cpp
    src/http_utils.cpp
    src/workspace_fs.cpp
    src/terminal_pty.cpp
)

target_include_directories(qcode-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(qcode-server PRIVATE
    QCODE_SERVER_WEBUI_DIR="${QCODE_WEBUI_SRC}"
)

target_link_libraries(qcode-server PRIVATE qcode-core)

add_custom_command(TARGET qcode-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${QCODE_WEBUI_SRC}"
        "$<TARGET_FILE_DIR:qcode-server>/webui"
    COMMENT "Copying webui next to qcode-server"
)
