add_executable(qcode-tui
    main.cpp
    chat.cpp
    commands.cpp
    config.cpp
    system_prompt.cpp
    tools.cpp
    views.cpp
)

target_link_libraries(qcode-tui PRIVATE
    ai::sdk
    ftxui::screen
    ftxui::dom
    ftxui::component
)

target_include_directories(qcode-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
