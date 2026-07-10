add_executable(qcode-tui
    main.cpp
    chat_bus.cpp
    provider_registry_init.cpp
    commands.cpp
    config.cpp
    db.cpp
    system_prompt.cpp
    tools.cpp
    views.cpp
    markdown.cpp
    ../../third_party/md4c/md4c.c
    message_render.cpp
    tool_renderers.cpp
    store.cpp
    bus/impl.cpp
)

target_link_libraries(qcode-tui PRIVATE
    ai::sdk
    ftxui::screen
    ftxui::dom
    ftxui::component
    sqlite3
    stduuid::stduuid
)

target_include_directories(qcode-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/md4c
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/md4c/md4c.c
    PROPERTIES LANGUAGE C COMPILE_FLAGS "-std=c99")

