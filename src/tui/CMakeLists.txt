add_executable(qcode-tui
    main.cpp
    chat.cpp
    chat_bus.cpp
    commands.cpp
    config.cpp
    db.cpp
    system_prompt.cpp
    tools.cpp
    views.cpp
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
)

target_include_directories(qcode-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
