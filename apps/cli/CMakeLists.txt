# qcode-cli — command-line client.

add_executable(qcode-cli
    main.cpp
)

# Headless CLI links qcode::engine (no FTXUI dependency)
target_link_libraries(qcode-cli PRIVATE
    qcode::engine
)
