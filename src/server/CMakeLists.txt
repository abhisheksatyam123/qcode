# ── QCode Headless Server ──────────────────────────────────────────────
# Exposes LLM generation over HTTP (NDJSON streaming).
# No FTXUI dependencies.
add_executable(qcode-server
    server_main.cpp
    json_codec.cpp
    uuid.cpp
)

target_include_directories(qcode-server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/httplib-header-only
)

target_compile_definitions(qcode-server PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT=1)

target_link_libraries(qcode-server PRIVATE
    ai::sdk
    httplib::httplib
    nlohmann_json::nlohmann_json
    sqlite3
    stduuid::stduuid
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Shared TUI sources (no FTXUI deps) compiled into server
target_sources(qcode-server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/tui/bus/impl.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/store.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/chat_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/tools.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/config.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/db.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/system_prompt.cpp
    ${CMAKE_SOURCE_DIR}/src/tui/provider_registry_init.cpp
)

target_include_directories(qcode-server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/tui
)
