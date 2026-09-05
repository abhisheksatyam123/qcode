#include <iostream>

#include <qcode/core/qcode.h>

int main() {
  std::cout << "qcode - All Components Demo\n";
  std::cout << "================================\n\n";

  std::cout << "Available components:\n";
  std::cout << "- Core: YES\n";

#ifdef QCODE_HAS_OPENAI
  std::cout << "- OpenAI: YES\n";
#else
  std::cout << "- OpenAI: NO\n";
#endif

#ifdef QCODE_HAS_ANTHROPIC
  std::cout << "- Anthropic: YES\n";
#else
  std::cout << "- Anthropic: NO\n";
#endif

  std::cout << "\n";

  // Test core functionality
  std::cout << "Testing core functionality...\n";
  qcode::GenerateOptions options;
  options.model = "gpt-5.4";
  options.prompt = "Hello world";
  std::cout << "✓ Core types work fine\n\n";

  // Test OpenAI functionality
#ifdef QCODE_HAS_OPENAI
  std::cout << "Testing OpenAI functionality...\n";
  try {
    auto openai_client = qcode::openai::create_client();
    std::cout << "✓ OpenAI client created successfully\n";
    std::cout << "✓ Available models: " << qcode::openai::models::kGpt54 << ", "
              << qcode::openai::models::kGpt54Mini << "\n";
  } catch (const std::exception& e) {
    std::cout << "✗ OpenAI client failed: " << e.what() << "\n";
  }
#else
  std::cout << "OpenAI functionality not available\n";
#endif

  // Test Anthropic functionality
#ifdef QCODE_HAS_ANTHROPIC
  std::cout << "\nTesting Anthropic functionality...\n";
  try {
    auto anthropic_client = qcode::anthropic::create_client();
    std::cout << "✓ Anthropic client created successfully\n";
    std::cout << "✓ Available models: "
              << qcode::anthropic::models::kClaudeSonnet46 << ", "
              << qcode::anthropic::models::kClaudeHaiku45 << "\n";
  } catch (const std::exception& e) {
    std::cout << "✗ Anthropic client failed: " << e.what() << "\n";
  }
#else
  std::cout << "\nAnthropic functionality not available\n";
#endif

  std::cout << "\n";
  std::cout << "This example demonstrates using all components together.\n";
  std::cout
      << "OpenAI and Anthropic are available when linking qcode::engine\n";

  return 0;
}