# Examples for qcode

function(add_qcode_example name source_file)
    add_executable(${name} ${source_file})
    target_link_libraries(${name} PRIVATE qcode-core)

    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
endfunction()

add_qcode_example(basic_chat basic_chat.cpp)
add_qcode_example(streaming_chat streaming_chat.cpp)
add_qcode_example(multi_provider multi_provider.cpp)
add_qcode_example(error_handling error_handling.cpp)
add_qcode_example(retry_config_example retry_config_example.cpp)
add_qcode_example(test_openai test_openai.cpp)
add_qcode_example(test_anthropic test_anthropic.cpp)
add_qcode_example(openrouter_example openrouter_example.cpp)
add_qcode_example(tool_calling_basic tool_calling_basic.cpp)
add_qcode_example(tool_calling_multistep tool_calling_multistep.cpp)
add_qcode_example(tool_calling_async tool_calling_async.cpp)
add_qcode_example(test_tool_integration test_tool_integration.cpp)
add_qcode_example(embeddings_example embeddings_example.cpp)
add_qcode_example(langfuse_tracing langfuse_tracing.cpp)

add_subdirectory(components/openai)
add_subdirectory(components/anthropic)
add_subdirectory(components/all)

message(STATUS "Examples will be built in: ${CMAKE_BINARY_DIR}/examples/")
