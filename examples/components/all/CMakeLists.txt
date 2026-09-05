add_executable(all_components_demo main.cpp)
target_link_libraries(all_components_demo PRIVATE qcode::engine)

set_target_properties(all_components_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/components/all
)
