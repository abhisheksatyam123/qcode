#include "anthropic_client.h"

#include <qcode/providers/anthropic.h>
#include <qcode/core/logger.h>
#include "anthropic_request_builder.h"
#include "anthropic_response_parser.h"
#include "anthropic_stream.h"

#include <httplib.h>

#include <algorithm>
#include <map>
#include <memory>

namespace qcode {
namespace anthropic {
namespace {

httplib::Headers extra_headers(
    const std::map<std::string, std::string>& headers) {
  httplib::Headers extra{{"anthropic-version", "2023-06-01"}};
  for (const auto& [name, value] : headers) extra.emplace(name, value);
  return extra;
}

std::string messages_path(const std::string& base_url,
                          const std::string& override_path) {
  if (!override_path.empty()) {
    return override_path.front() == '/' ? override_path : "/" + override_path;
  }
  return (base_url.ends_with("/v1") || base_url.ends_with("/v1/"))
             ? "/messages"
             : "/v1/messages";
}

}  // namespace

AnthropicClient::AnthropicClient(const std::string& api_key,
                                 const std::string& base_url)
    : AnthropicClient(api_key, CompatibleOptions{.base_url = base_url}) {}

AnthropicClient::AnthropicClient(const std::string& api_key,
                                 const std::string& base_url,
                                 const retry::RetryConfig& retry_config)
    : AnthropicClient(api_key, CompatibleOptions{.base_url = base_url,
                                                 .retry_config = retry_config}) {}

AnthropicClient::AnthropicClient(const std::string& api_key,
                                 const CompatibleOptions& options)
    : BaseProviderClient(
          providers::ProviderConfig{
              .api_key = api_key,
              .base_url = options.base_url,
              .completions_endpoint_path =
                  messages_path(options.base_url, options.completions_path),
              .embeddings_endpoint_path = "/v1/embeddings",
              .auth_header_name =
                  options.bearer_auth ? "Authorization" : "x-api-key",
              .auth_header_prefix = options.bearer_auth ? "Bearer " : "",
              .extra_headers = extra_headers(options.headers),
              .retry_config = options.retry_config},
          std::make_unique<AnthropicRequestBuilder>(),
          std::make_unique<AnthropicResponseParser>()) {
  LOG_DEBUG("Anthropic client initialized with base_url: {} bearer={}",
            options.base_url, options.bearer_auth);
}

StreamResult AnthropicClient::stream_text(const StreamOptions& options) {
  LOG_DEBUG(
      "Starting text streaming - model: {}, prompt length: {}", options.model,
      options.prompt.length());

  // Build request with stream: true
  auto request_json = request_builder_->build_request_json(options);
  request_json["stream"] = true;
  LOG_DEBUG("Stream request JSON built with stream=true");

  // Create headers
  auto headers = request_builder_->build_headers(config_);
  headers.emplace("Accept", "text/event-stream");

  // Create stream implementation
  auto impl = std::make_unique<AnthropicStreamImpl>();
  impl->start_stream(config_.base_url + config_.completions_endpoint_path,
                     headers, request_json);

  LOG_INFO("Text streaming started - model: {}", options.model);

  // Return StreamResult with implementation
  return StreamResult(std::move(impl));
}

std::string AnthropicClient::provider_name() const {
  return "anthropic";
}

std::vector<std::string> AnthropicClient::supported_models() const {
  // Both the friendly aliases (e.g. "claude-sonnet-4-5") and their dated
  // snapshot variants (e.g. "claude-sonnet-4-5-20250929") are accepted by the
  // Anthropic API; list both so identifiers in `qcode::anthropic::models::*`
  // resolve via `supports_model()`.
  return {"claude-opus-4-7", "claude-sonnet-4-6", "claude-opus-4-6",
          "claude-haiku-4-5", "claude-haiku-4-5-20251001", "claude-opus-4-5",
          "claude-opus-4-5-20251101", "claude-sonnet-4-5",
          "claude-sonnet-4-5-20250929", "claude-opus-4-1",
          "claude-opus-4-1-20250805",
          // Deprecated, retire 2026-06-15:
          "claude-sonnet-4-0", "claude-sonnet-4-20250514", "claude-opus-4-0",
          "claude-opus-4-20250514"};
}

bool AnthropicClient::supports_model(const std::string& model_name) const {
  auto models = supported_models();
  return std::find(models.begin(), models.end(), model_name) != models.end();
}

std::string AnthropicClient::config_info() const {
  return "Anthropic API (base_url: " + config_.base_url + ")";
}

std::string AnthropicClient::default_model() const {
  return models::kDefaultModel;
}

}  // namespace anthropic
}  // namespace qcode
