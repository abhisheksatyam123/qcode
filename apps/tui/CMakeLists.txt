# qcode-tui — terminal UI front-end.
# Core logic lives in qcode-core; this target owns the FTXUI entrypoint/views.

add_executable(qcode-tui
    src/main.cpp
    src/views.cpp
)

target_link_libraries(qcode-tui PRIVATE
    qcode-core
    ftxui::screen
    ftxui::dom
    ftxui::component
)

target_include_directories(qcode-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

install(TARGETS qcode-tui
    RUNTIME DESTINATION bin
)
