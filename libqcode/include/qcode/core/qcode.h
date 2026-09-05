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

#ifdef QCODE_HAS_CURSOR
#include <qcode/providers/cursor.h>
#endif

#ifdef QCODE_HAS_ANTIGRAVITY
#include <qcode/providers/antigravity.h>
#endif

// Type definitions
#include <qcode/core/client.h>
#include <qcode/core/enums.h>
#include <qcode/core/generate_options.h>
#include <qcode/core/message.h>
#include <qcode/core/model.h>
#include <qcode/core/stream_event.h>
#include <qcode/core/stream_options.h>
#include <qcode/core/stream_result.h>
#include <qcode/core/tool.h>
#include <qcode/core/usage.h>

// Tool functionality
#include <qcode/tools/tool_executor.h>
#include <qcode/tools/tool_factory.h>
#include <qcode/tools/multi_step_coordinator.h>

// Error handling
#include <qcode/core/errors.h>

/// qcode - C++ toolkit for AI-powered coding agents
///
/// Usage Examples:
///
/// OpenAI Integration:
/// ```cpp
/// #include <qcode/core/qcode.h>
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
