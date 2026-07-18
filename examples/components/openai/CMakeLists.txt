add_executable(openai_component_demo main.cpp)
target_link_libraries(openai_component_demo PRIVATE qcode-core)

set_target_properties(openai_component_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/components/openai
)
