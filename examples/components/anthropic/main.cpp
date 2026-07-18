#include <iostream>

#include <qcode/qcode.h>

int main() {
  std::cout << "qcode - Anthropic Component Demo\n";
  std::cout << "====================================\n\n";

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
  options.model = "claude-sonnet-4-6";
  options.prompt = "Hello world";
  std::cout << "✓ Core types work fine\n\n";

  // Test Anthropic functionality
#ifdef QCODE_HAS_ANTHROPIC
  std::cout << "Testing Anthropic functionality...\n";
  try {
    auto client = qcode::anthropic::create_client();
    std::cout << "✓ Anthropic client created successfully\n";
    std::cout << "✓ Available models: "
              << qcode::anthropic::models::kClaudeSonnet46 << ", "
              << qcode::anthropic::models::kClaudeHaiku45 << "\n";
  } catch (const std::exception& e) {
    std::cout << "✗ Anthropic client failed: " << e.what() << "\n";
  }
#else
  std::cout << "Anthropic functionality not available\n";
#endif

  // Demonstrate that OpenAI is NOT available
#ifdef QCODE_HAS_OPENAI
  std::cout << "\nTesting OpenAI functionality...\n";
  // This would work if OpenAI was linked
  auto openai_client = qcode::openai::create_client();
  std::cout << "✓ OpenAI client created\n";
#else
  std::cout
      << "\nOpenAI functionality intentionally not available in this build\n";
  std::cout << "This example only links qcode::anthropic component\n";

  // Uncommenting the next line would cause a compile error:
  // auto openai_client = qcode::openai::create_client(); // ERROR!
#endif

  return 0;
}