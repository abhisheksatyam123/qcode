#include "antigravity_client.h"

#include <qcode/logger/logger.h>
#include "providers/antigravity/antigravity_request_builder.h"
#include "providers/antigravity/antigravity_response_parser.h"
#include "providers/antigravity/antigravity_stream.h"

#include <algorithm>
#include <memory>
#include <optional>

namespace qcode {
namespace antigravity {
namespace {

constexpr const char* kCompletionsPath = ":generateContent";
constexpr const char* kStreamPath = ":streamGenerateContent?alt=sse";

providers::ProviderConfig make_config(
    const std::string& api_key, const std::string& base_url,
    const std::optional<retry::RetryConfig>& retry) {
  providers::ProviderConfig cfg;
  cfg.api_key = api_key;
  cfg.base_url = base_url;
  cfg.completions_endpoint_path = kCompletionsPath;
  cfg.embeddings_endpoint_path = "";
  cfg.auth_header_name = "Authorization";
  cfg.auth_header_prefix = "Bearer ";
  cfg.extra_headers = {};
  cfg.retry_config = retry;
  return cfg;
}

}  // namespace

AntigravityClient::AntigravityClient(const std::string& api_key,
                                     const std::string& base_url)
    : BaseProviderClient(
          make_config(api_key, base_url, std::nullopt),
          std::make_unique<AntigravityRequestBuilder>(),
          std::make_unique<AntigravityResponseParser>()) {
  LOG_DEBUG("Antigravity client initialized with base_url: {}", base_url);
}

AntigravityClient::AntigravityClient(const std::string& api_key,
                                     const std::string& base_url,
                                     const retry::RetryConfig& retry_config)
    : BaseProviderClient(
          make_config(api_key, base_url, retry_config),
          std::make_unique<AntigravityRequestBuilder>(),
          std::make_unique<AntigravityResponseParser>()) {
  LOG_DEBUG(
      "Antigravity client initialized with base_url: {} and custom retry config",
      base_url);
}

AntigravityClient::AntigravityClient(const std::string& api_key,
                                     const std::string& base_url,
                                     const std::string& project_id)
    : BaseProviderClient(
          make_config(api_key, base_url, std::nullopt),
          std::make_unique<AntigravityRequestBuilder>(project_id),
          std::make_unique<AntigravityResponseParser>()) {}

StreamResult AntigravityClient::stream_text(const StreamOptions& options) {
  LOG_DEBUG("Starting Antigravity streaming - model: {}, prompt length: {}",
            options.model, options.prompt.length());

  auto request_json = request_builder_->build_request_json(options);

  auto headers = request_builder_->build_headers(config_);
  headers.emplace("Accept", "text/event-stream");

  auto impl = std::make_unique<AntigravityStreamImpl>();
  impl->start_stream(config_.base_url + kStreamPath, headers, request_json);

  LOG_INFO("Antigravity streaming started - model: {}", options.model);
  return StreamResult(std::move(impl));
}

EmbeddingResult AntigravityClient::embeddings(const EmbeddingOptions&) {
  return EmbeddingResult("Antigravity does not support embeddings");
}

std::string AntigravityClient::provider_name() const { return "antigravity"; }

std::vector<std::string> AntigravityClient::supported_models() const {
  return {"gemini-2.5-flash", "gemini-2.5-flash-lite", "gemini-2.5-pro",
          "gemini-3-flash", "gemini-3-flash-agent", "gemini-3-pro-low",
          "gemini-3-pro-high", "claude-sonnet-4-6",
          "claude-opus-4-6-thinking"};
}

bool AntigravityClient::supports_model(const std::string& model_name) const {
  // Antigravity serves Gemini/Claude models; accept any non-empty model name
  // rather than maintaining a brittle allow-list.
  return !model_name.empty();
}

std::string AntigravityClient::config_info() const {
  return "Antigravity (base_url: " + config_.base_url + ")";
}

std::string AntigravityClient::default_model() const {
  return "gemini-3-flash";
}

}  // namespace antigravity
}  // namespace qcode
