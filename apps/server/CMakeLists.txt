cmake_minimum_required(VERSION 3.16)
project(qcode-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# WebUI static files live in apps/webui/src; copy next to the binary and
# bake in a fallback absolute path for development runs.
set(QCODE_WEBUI_SRC "${CMAKE_SOURCE_DIR}/apps/webui/src")

add_executable(qcode-server
    src/server_main.cpp
    src/json_codec.cpp
    src/uuid.cpp
)

target_include_directories(qcode-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(qcode-server PRIVATE
    AI_SERVER_WEBUI_DIR="${QCODE_WEBUI_SRC}"
)

target_link_libraries(qcode-server PRIVATE qcode-core)

add_custom_command(TARGET qcode-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${QCODE_WEBUI_SRC}"
        "$<TARGET_FILE_DIR:qcode-server>/webui"
    COMMENT "Copying webui next to qcode-server"
)

# Install
install(TARGETS qcode-server
    RUNTIME DESTINATION bin
)

