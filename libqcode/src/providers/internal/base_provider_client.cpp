#include "providers/internal/base_provider_client.h"

#include <qcode/core/logger.h>

namespace qcode {
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

  // Direct single-turn execution: BaseProviderClient is a wire client.
  // Multi-step agent loops are driven exclusively by the orchestration layer.
  return generate_text_single_step(options);
}

GenerateResult BaseProviderClient::generate_text_single_step(
    const GenerateOptions& options) {
  try {
    // Build request JSON using the provider-specific builder
    auto request_json = request_builder_->build_request_json(options);
    std::string json_body = request_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    // Never dump full request bodies — they can be megabytes (tool history) and
    // previously filled /tmp/qcode.log to hundreds of MB in a single session.
    LOG_DEBUG("Request JSON built: {} bytes", json_body.size());

    // Build headers
    auto headers = request_builder_->build_headers(config_);

    // Make the request — abort_flag rides along so Esc cancels retries
    auto result = http_handler_->post(config_.completions_endpoint_path,
                                      headers, json_body, "application/json",
                                      options.on_retry ? *options.on_retry : nullptr,
                                      options.abort_flag);

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
      LOG_INFO("Raw response text: {}", result.text);
      return GenerateResult("Failed to parse response: " +
                            std::string(e.what()));
    }

    LOG_INFO("Raw response text: {}", result.text);

    LOG_INFO(
        "Text generation successful - model: {}, response_id: {}",
        options.model, json_response.value("id", "unknown"));

    // Parse using provider-specific parser
    auto parsed_result =
        response_parser_->parse_success_completion_response(json_response);

    if (!parsed_result.is_retryable.has_value()) {
      parsed_result.is_retryable = result.is_retryable;
    }
    return parsed_result;

  } catch (const std::exception& e) {
    LOG_ERROR("Exception in generate_text_single_step: {}", e.what());
    GenerateResult error_result("Exception: " + std::string(e.what()));
    error_result.is_retryable = true;
    return error_result;
  }
}

StreamResult BaseProviderClient::stream_text(const StreamOptions& options) {
  (void)options;
  LOG_ERROR("Streaming not implemented directly in BaseProviderClient");
  return StreamResult();
}

EmbeddingResult BaseProviderClient::embeddings(
    const EmbeddingOptions& options) {
  LOG_DEBUG(
      "Starting embeddings generation - model: {}",
      options.model);

  try {
    // Build request JSON using the provider-specific builder
    auto request_json = request_builder_->build_request_json(options);
    std::string json_body = request_json.dump();

    // Build headers
    auto headers = request_builder_->build_headers(config_);

    // Make the request
    auto result = http_handler_->post(config_.embeddings_endpoint_path, headers,
                                      json_body);

    if (!result.is_success()) {
      LOG_ERROR("HTTP request for embeddings failed: {}",
                            result.error_message());
      if (result.provider_metadata.has_value()) {
        int status_code = std::stoi(result.provider_metadata.value());
        return response_parser_->parse_error_embedding_response(
            status_code, result.error.value_or(""));
      }
      return EmbeddingResult(result.error_message());
    }

    // Parse response
    nlohmann::json json_response;
    try {
      json_response = nlohmann::json::parse(result.text);
    } catch (const nlohmann::json::exception& e) {
      LOG_ERROR("Failed to parse embedding response JSON: {}",
                            e.what());
      return EmbeddingResult("Failed to parse response: " +
                             std::string(e.what()));
    }

    return response_parser_->parse_success_embedding_response(json_response);

  } catch (const std::exception& e) {
    LOG_ERROR("Exception in generate_embeddings: {}", e.what());
    return EmbeddingResult("Exception: " + std::string(e.what()));
  }
}

}  // namespace providers
}  // namespace qcode
