# qcode-cli — command-line client.

add_executable(qcode-cli
    main.cpp
)

target_link_libraries(qcode-cli PRIVATE
    qcode-core
)
