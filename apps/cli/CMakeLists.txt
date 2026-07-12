cmake_minimum_required(VERSION 3.16)
project(qcode-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(qcode-cli
    main.cpp
)

target_link_libraries(qcode-cli PRIVATE
    qcode-core
)

install(TARGETS qcode-cli
    RUNTIME DESTINATION bin
)
