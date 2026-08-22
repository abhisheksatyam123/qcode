#pragma once

/// Core qcode API — types, tools, and errors (no provider clients)
/// Use this when you only need core types and tools without specific providers

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

/// qcode core API
///
/// This header provides access to core types and functionality
/// without pulling in specific provider implementations.
///
/// Usage:
/// ```cpp
/// #include <qcode/core/core_api.h>
///
/// // Use core types like qcode::GenerateOptions, qcode::Message, etc.
/// qcode::GenerateOptions options{
///     .model = "some-model",
///     .prompt = "Hello world"
/// };
/// ```
namespace qcode {}