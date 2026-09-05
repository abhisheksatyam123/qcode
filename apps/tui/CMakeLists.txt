# qcode-tui — terminal UI front-end.
# Links qcode::ui (which pulls in qcode::engine and FTXUI).

add_executable(qcode-tui
    src/main.cpp
    src/tui_overlays.cpp
    src/views.cpp
    src/views_pickers.cpp
    src/views_toast.cpp
    src/file_diff_preview.cpp
)

target_link_libraries(qcode-tui PRIVATE
    qcode::ui
)

target_include_directories(qcode-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
