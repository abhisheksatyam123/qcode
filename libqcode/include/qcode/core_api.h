#pragma once

/// Core qcode API — types, tools, and errors (no provider clients)
/// Use this when you only need core types and tools without specific providers

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

// Error handling
#include <qcode/providers/errors.h>

/// qcode core API
///
/// This header provides access to core types and functionality
/// without pulling in specific provider implementations.
///
/// Usage:
/// ```cpp
/// #include <qcode/core_api.h>
///
/// // Use core types like ai::GenerateOptions, ai::Message, etc.
/// ai::GenerateOptions options{
///     .model = "some-model",
///     .prompt = "Hello world"
/// };
/// ```
namespace ai {}