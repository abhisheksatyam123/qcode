#include "antigravity_client.h"

#include "ai/logger.h"
#include "providers/antigravity/antigravity_request_builder.h"
#include "providers/openai/openai_response_parser.h"
#include "providers/openai/openai_stream.h"

#include <algorithm>
#include <memory>
#include <optional>

namespace ai {
namespace antigravity {
namespace {

constexpr const char* kDefaultBaseUrl =
    "https://daily-cloudcode-pa.googleapis.com/v1internal";
constexpr const char* kCompletionsPath = ":generateContent";
constexpr const char* kStreamPath = ":streamGenerateContent?alt=sse";

providers::ProviderConfig make_config(
    const std::string& api_key, const std::string& base_url,
    const std::optional<retry::RetryConfig>& retry) {
  providers::ProviderConfig cfg;
  cfg.api_key = api_key;
  cfg.base_url = base_url;
  cfg.completions_endpoint_path = kCompletionsPath;
  cfg.embeddings_endpoint_path = "/v1/embeddings";
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
          std::make_unique<openai::OpenAIResponseParser>()) {
  LOG_DEBUG("Antigravity client initialized with base_url: {}", base_url);
}

AntigravityClient::AntigravityClient(const std::string& api_key,
                                     const std::string& base_url,
                                     const retry::RetryConfig& retry_config)
    : BaseProviderClient(
          make_config(api_key, base_url, retry_config),
          std::make_unique<AntigravityRequestBuilder>(),
          std::make_unique<openai::OpenAIResponseParser>()) {
  LOG_DEBUG(
      "Antigravity client initialized with base_url: {} and custom retry config",
      base_url);
}

StreamResult AntigravityClient::stream_text(const StreamOptions& options) {
  LOG_DEBUG("Starting Antigravity streaming - model: {}, prompt length: {}",
            options.model, options.prompt.length());

  auto request_json = request_builder_->build_request_json(options);
  request_json["stream"] = true;

  auto headers = request_builder_->build_headers(config_);
  headers.emplace("Accept", "text/event-stream");

  auto impl = std::make_unique<openai::OpenAIStreamImpl>();
  impl->start_stream(config_.base_url + kStreamPath, headers, request_json);

  LOG_INFO("Antigravity streaming started - model: {}", options.model);
  return StreamResult(std::move(impl));
}

std::string AntigravityClient::provider_name() const { return "antigravity"; }

std::vector<std::string> AntigravityClient::supported_models() const {
  return {"gemini-3-flash", "gemini-3-flash-agent", "gemini-3-pro",
          "gemini-2.5-pro"};
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
}  // namespace ai
