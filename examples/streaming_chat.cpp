/**
 * Streaming Chat Example - qcode
 *
 * This example demonstrates real-time streaming text generation.
 * It shows how to:
 * - Use the stream_text API for real-time responses
 * - Handle stream events with iterators
 * - Use callbacks for event handling
 * - Display text as it's generated
 *
 * Usage:
 *   export OPENAI_API_KEY=your_key_here
 *   ./streaming_chat
 */

#include <iostream>
#include <string>

#include <qcode/core/qcode.h>

int main() {
  std::cout << "qcode - Streaming Chat Example\n";
  std::cout << "====================================\n\n";

  // Example 1: Basic streaming with iterator
  std::cout << "1. Basic streaming (iterator approach):\n";
  std::cout
      << "Prompt: Write a short story about a robot learning to paint.\n\n";
  std::cout << "Response: ";

  auto client = qcode::openai::create_client();
  qcode::GenerateOptions gen_options1;
  gen_options1.model = qcode::openai::models::kGpt54;
  gen_options1.prompt =
      "Write a short story about a robot learning "
      "to paint. Keep it under 200 words.";
  qcode::StreamOptions options1(std::move(gen_options1));
  auto stream1 = client.stream_text(options1);

  for (const auto& event : stream1) {
    if (event.is_text_delta()) {
      std::cout << event.text_delta << std::flush;
    } else if (event.is_error()) {
      std::cout << "\nError: " << event.error.value_or("Unknown error") << "\n";
      break;
    } else if (event.is_finish()) {
      std::cout << "\n\n[Stream finished]\n\n";
      break;
    }
  }

  // Example 2: Streaming with options and callbacks
  std::cout << "2. Streaming with callbacks:\n";
  std::cout << "Prompt: Explain quantum computing in simple terms.\n\n";

  // Set up callbacks
  std::string accumulated_text;
  int chunk_count = 0;

  auto text_callback = [&](const std::string& chunk) {
    std::cout << chunk << std::flush;
    accumulated_text += chunk;
    chunk_count++;
  };

  auto complete_callback = [&](const qcode::GenerateResult& result) {
    std::cout << "\n\n[Stream completed]\n";
    std::cout << "Total chunks received: " << chunk_count << "\n";
    std::cout << "Final text length: " << accumulated_text.length()
              << " characters\n";
    std::cout << "Token usage: " << result.usage.total_tokens << " tokens\n";
    std::cout << "Finish reason: " << result.finishReasonToString() << "\n\n";
  };

  auto error_callback = [](const std::string& error) {
    std::cout << "\nError in stream: " << error << "\n\n";
  };

  qcode::GenerateOptions gen_options2;
  gen_options2.model = qcode::openai::models::kGpt54Mini;
  gen_options2.prompt =
      "Explain quantum computing in simple terms that a "
      "high school student could understand.";
  qcode::StreamOptions options(std::move(gen_options2), text_callback,
                            complete_callback, error_callback);

  std::cout << "Response: ";
  auto stream2 = client.stream_text(options);

  // Process the stream (callbacks will handle the events)
  stream2.collect_all();  // This will block until completion

  // Example 3: Streaming conversation
  std::cout << "3. Streaming conversation:\n";

  qcode::Messages conversation = {
      qcode::Message::system("You are a creative writing assistant."),
      qcode::Message::user("I need help writing a poem about the ocean."),
      qcode::Message::assistant("I'd be happy to help! What specific aspect of "
                             "the ocean interests you most?"),
      qcode::Message::user("The way waves crash against rocks during a storm.")};

  std::cout << "Response: ";
  qcode::GenerateOptions gen_options3;
  gen_options3.model = qcode::openai::models::kGpt54;
  gen_options3.messages = conversation;
  qcode::StreamOptions options3(std::move(gen_options3));
  auto stream3 = client.stream_text(options3);

  // Manual event processing
  bool finished = false;
  for (const auto& event : stream3) {
    switch (event.type) {
      case qcode::kStreamEventTypeTextDelta:
        std::cout << event.text_delta;
        std::cout.flush();
        break;
      case qcode::kStreamEventTypeFinish:
        std::cout << "\n\nGeneration finished.\n";
        finished = true;
        break;
      case qcode::kStreamEventTypeError:
        std::cerr << "\nError in stream: "
                  << event.error.value_or("Unknown error") << "\n";
        finished = true;
        break;
      default:
        // Handle other event types (ToolCall, ToolResult, etc.)
        break;
    }
    if (finished)
      break;
  }

next_example:

  // Example 4: Streaming with temperature control
  std::cout << "4. Creative streaming (high temperature):\n";
  std::cout << "Prompt: Write 3 unusual ice cream flavors.\n\n";

  qcode::GenerateOptions gen_options4;
  gen_options4.model = qcode::openai::models::kGpt54;
  gen_options4.prompt =
      "Invent 3 unusual but delicious ice cream flavors with creative names.";
  gen_options4.temperature = 1.2;  // High creativity
  gen_options4.max_tokens = 100;
  qcode::StreamOptions creative_options(std::move(gen_options4));

  std::cout << "Response: ";
  auto stream4 = client.stream_text(creative_options);

  std::string creative_result = stream4.collect_all();
  std::cout << creative_result << "\n\n";

  // Example 5: Error handling demonstration
  std::cout << "5. Error handling:\n";
  std::cout << "Testing with invalid model...\n\n";

  qcode::GenerateOptions gen_options5;
  gen_options5.model = "invalid-model-name";
  gen_options5.prompt = "Hello, world!";
  qcode::StreamOptions options5(std::move(gen_options5));
  auto stream5 = client.stream_text(options5);

  if (stream5.has_error()) {
    std::cout << "Expected error: " << stream5.error_message() << "\n\n";
  } else {
    std::cout << "Unexpected: No error with invalid model\n\n";
  }

  std::cout << "Streaming examples completed!\n";
  std::cout << "\nKey benefits of streaming:\n";
  std::cout << "  - Real-time response display\n";
  std::cout << "  - Better user experience for long responses\n";
  std::cout << "  - Ability to cancel or modify based on partial content\n";
  std::cout << "  - Lower perceived latency\n";

  return 0;
}