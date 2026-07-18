#pragma once

/// Core AI SDK C++ header - Base functionality only
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
#include <qcode/tools/tools.h>

// Error handling
#include <qcode/providers/errors.h>

/// AI SDK C++ Core - Base functionality
///
/// This header provides access to core types and functionality
/// without pulling in specific provider implementations.
///
/// Usage:
/// ```cpp
/// #include <qcode/core.h>
///
/// // Use core types like ai::GenerateOptions, ai::Message, etc.
/// ai::GenerateOptions options{
///     .model = "some-model",
///     .prompt = "Hello world"
/// };
/// ```
namespace ai {}