#include "base_provider_client.h"

#include "ai/logger.h"
#include "ai/tools.h"

namespace ai {
namespace providers {

BaseProviderClient::BaseProviderClient(
    const ProviderConfig& config,
    std::unique_ptr<RequestBuilder> request_builder,
    std::unique_ptr<ResponseParser> response_parser)
    : config_(config),
      request_builder_(std::move(request_builder)),
      response_parser_(std::move(response_parser)) {
  // Initialize HTTP handler with parsed config
  auto http_config = http::HttpRequestHandler::parse_base_url(config.base_url);

  // Apply custom retry config if provided
  if (config.retry_config.has_value()) {
    http_config.retry_config = config.retry_config.value();
  }

  http_handler_ = std::make_unique<http::HttpRequestHandler>(http_config);

  LOG_DEBUG(
      R"(BaseProviderClient initialized - base_url: {},
     completions_endpoint: {}, embeddings_endpoint: {})",
      config.base_url, config.completions_endpoint_path,
      config.embeddings_endpoint_path);
}

GenerateResult BaseProviderClient::generate_text(
    const GenerateOptions& options) {
  LOG_DEBUG(
      "Starting text generation - model: {}, prompt length: {}, tools: {}, "
      "max_steps: {}",
      options.model, options.prompt.length(), options.tools.size(),
      options.max_steps);

  // Check if multi-step tool calling is enabled
  if (options.has_tools() && options.is_multi_step()) {
    LOG_DEBUG("Using multi-step tool calling with {} tools",
                          options.tools.size());

    // Use MultiStepCoordinator for complex workflows
    return MultiStepCoordinator::execute_multi_step(
        options, [this](const GenerateOptions& step_options) {
          return this->generate_text_single_step(step_options);
        });
  } else {
    // Single step generation
    return generate_text_single_step(options);
  }
}

GenerateResult BaseProviderClient::generate_text_single_step(
    const GenerateOptions& options) {
  try {
    // Build request JSON using the provider-specific builder
    auto request_json = request_builder_->build_request_json(options);
    std::string json_body = request_json.dump();
    LOG_DEBUG("Request JSON built: {}", json_body);

    // Build headers
    auto headers = request_builder_->build_headers(config_);

    // Make the request
    auto result = http_handler_->post(config_.completions_endpoint_path,
                                      headers, json_body);

    if (!result.is_success()) {
      // Parse error response using provider-specific parser
      LOG_ERROR("text_generation_single_step: HTTP request failed - finish_reason={} error=\"{}\" text_len={} provider_metadata=\"{}\"",
              result.finishReasonToString(), result.error_message(),
              result.text.size(), result.provider_metadata.value_or(""));
      if (result.provider_metadata.has_value()) {
        int status_code = std::stoi(result.provider_metadata.value());
        return response_parser_->parse_error_completion_response(
            status_code, result.error.value_or(""));
      }
      return result;
    }

    // Parse the response JSON from result.text
    nlohmann::json json_response;
    try {
      json_response = nlohmann::json::parse(result.text);
    } catch (const nlohmann::json::exception& e) {
      LOG_ERROR("Failed to parse response JSON: {}", e.what());
      LOG_DEBUG("Raw response text: {}", result.text);
      return GenerateResult("Failed to parse response: " +
                            std::string(e.what()));
    }

    LOG_INFO(
        "Text generation successful - model: {}, response_id: {}",
        options.model, json_response.value("id", "unknown"));

    // Parse using provider-specific parser
    auto parsed_result =
        response_parser_->parse_success_completion_response(json_response);

    if (!parsed_result.is_success()) {
      LOG_ERROR("text_generation_single_step: parser returned error - finish_reason={} error=\"{}\" metadata=\"{}\"",
                parsed_result.finishReasonToString(),
                parsed_result.error_message(),
                parsed_result.provider_metadata.value_or(""));
      return parsed_result;
    }

    if (parsed_result.has_tool_calls()) {
      LOG_DEBUG("Model made {} tool calls",
                            parsed_result.tool_calls.size());
    }

    // Execute tools if the model made tool calls
    if (parsed_result.has_tool_calls() && options.has_tools()) {
      LOG_DEBUG("Model made {} tool calls, executing them",
                            parsed_result.tool_calls.size());

      auto tool_results = ToolExecutor::execute_tools_with_options(
          parsed_result.tool_calls, options, true);

      parsed_result.tool_results = tool_results;
      LOG_DEBUG("Executed {} tools", tool_results.size());

      // Check if any tool execution failed
      int failed_count = 0;
      for (const auto& result : tool_results) {
        if (!result.is_success()) {
          failed_count++;
          LOG_WARN("Tool '{}' execution failed: {}",
                               result.tool_name, result.error_message());
        }
      }

      if (failed_count > 0) {
        LOG_INFO(
            "Some tools failed ({}/{}), but overall result is still successful",
            failed_count, tool_results.size());
      }
    }

    return parsed_result;

  } catch (const std::exception& e) {
    LOG_ERROR("Exception during text generation: {}", e.what());
    return GenerateResult(std::string("Exception: ") + e.what());
  }
}

StreamResult BaseProviderClient::stream_text(const StreamOptions& options) {
  // This needs to be implemented with provider-specific stream implementations
  // For now, return an error
  LOG_ERROR("Streaming not yet implemented in BaseProviderClient");
  return StreamResult();
}

EmbeddingResult BaseProviderClient::embeddings(
    const EmbeddingOptions& options) {
  try {
    // Build request JSON using the provider-specific builder
    auto request_json = request_builder_->build_request_json(options);
    std::string json_body = request_json.dump();
    LOG_DEBUG("Request JSON built: {}", json_body);

    // Build headers
    auto headers = request_builder_->build_headers(config_);

    // Make the requests
    auto result = http_handler_->post(config_.embeddings_endpoint_path, headers,
                                      json_body);

    if (!result.is_success()) {
      // Parse error response using provider-specific parser
      if (result.provider_metadata.has_value()) {
        int status_code = std::stoi(result.provider_metadata.value());
        return response_parser_->parse_error_embedding_response(
            status_code, result.error.value_or(""));
      }
      return EmbeddingResult(result.error);
    }

    // Parse the response JSON from result.text
    nlohmann::json json_response;
    try {
      json_response = nlohmann::json::parse(result.text);
    } catch (const nlohmann::json::exception& e) {
      LOG_ERROR("Failed to parse response JSON: {}", e.what());
      LOG_DEBUG("Raw response text: {}", result.text);
      return EmbeddingResult("Failed to parse response: " +
                             std::string(e.what()));
    }

    LOG_INFO("Embeddings successful - model: {}, response_id: {}",
                         options.model, json_response.value("id", "unknown"));

    // Parse using provider-specific parser
    auto parsed_result =
        response_parser_->parse_success_embedding_response(json_response);
    return parsed_result;

  } catch (const std::exception& e) {
    LOG_ERROR("Exception during embeddings: {}", e.what());
    return EmbeddingResult(std::string("Exception: ") + e.what());
  }
}

}  // namespace providers
}  // namespace ai