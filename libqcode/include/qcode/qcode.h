#pragma once

/// Main convenience header for qcode
/// Include this header to get access to all public APIs

// Provider-specific clients (conditionally included)
#ifdef QCODE_HAS_OPENAI
#include <qcode/providers/openai.h>
#endif

#ifdef QCODE_HAS_ANTHROPIC
#include <qcode/providers/anthropic.h>
#endif

// Type definitions
#include <qcode/types/client.h>
#include <qcode/types/enums.h>
#include <qcode/types/generate_options.h>
#include <qcode/types/message.h>
#include <qcode/types/model.h>
#include <qcode/types/stream_event.h>
#include <qcode/types/stream_options.h>
#include <qcode/types/stream_result.h>
#include <qcode/types/tool.h>
#include <qcode/types/usage.h>

// Tool functionality
#include <qcode/tools/tool_executor.h>
#include <qcode/tools/tool_factory.h>
#include <qcode/tools/multi_step_coordinator.h>

// Error handling
#include <qcode/errors/errors.h>

/// qcode - C++ toolkit for AI-powered coding agents
///
/// Usage Examples:
///
/// OpenAI Integration:
/// ```cpp
/// #include <qcode/qcode.h>
/// #include <iostream>
///
/// // Ensure OPENAI_API_KEY environment variable is set
/// auto client = qcode::openai::create_client();
///
/// auto result = client.generate_text({
///     .model = qcode::openai::models::kGpt54,
///     .system = "You are a friendly assistant!",
///     .prompt = "Why is the sky blue?"
/// });
///
/// if (result) {
///     std::cout << result->text << std::endl;
/// }
/// ```
///
/// Streaming text generation:
/// ```cpp
/// auto client = qcode::openai::create_client();
///
/// auto stream = client.stream_text({
///     .model = qcode::openai::models::kGpt54,
///     .system = "You are a helpful assistant.",
///     .prompt = "Write a short story about a robot."
/// });
///
/// for (const auto& chunk : stream) {
///     if (chunk.text) {
///         std::cout << chunk.text.value() << std::flush;
///     }
/// }
/// ```
///
/// Anthropic Integration:
/// ```cpp
/// auto client = qcode::anthropic::create_client();
/// auto result = client.generate_text({
///     .model = qcode::anthropic::models::kClaudeSonnet46,
///     .system = "You are a helpful assistant.",
///     .prompt = "Explain quantum computing in simple terms."
/// });
///
/// if (result) {
///     std::cout << result->text << std::endl;
/// }
/// ```
namespace qcode {}
