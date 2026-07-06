add_executable(qcode-tui main.cpp)

target_link_libraries(qcode-tui PRIVATE
    ai::sdk
    ftxui::screen
    ftxui::dom
    ftxui::component
)
