#include "openai_client.h"

#include <qcode/core/logger.h>
#include <qcode/providers/openai.h>
#include "openai_request_builder.h"
#include "openai_response_parser.h"
#include "openai_stream.h"

#include <algorithm>
#include <memory>

namespace qcode {

namespace {
bool has_version_suffix(const std::string& base_url) {
  return base_url.ends_with("/v1") || base_url.ends_with("/v1/");
}

std::string compute_completions_path(const std::string& base_url,
                                     bool use_responses = false) {
  if (base_url.find("qualcomm.com") != std::string::npos) {
    return "/responses";
  }
  const std::string resource =
      use_responses ? "/responses" : "/chat/completions";
  return has_version_suffix(base_url) ? resource : "/v1" + resource;
}

std::string get_embeddings_path(const std::string& base_url) {
  return has_version_suffix(base_url) ? "/embeddings" : "/v1/embeddings";
}

httplib::Headers make_headers(
    const std::map<std::string, std::string>& headers) {
  httplib::Headers result;
  for (const auto& [name, value] : headers) result.emplace(name, value);
  return result;
}
}

namespace openai {

OpenAIClient::OpenAIClient(const std::string& api_key,
                           const std::string& base_url)
    : BaseProviderClient(
          providers::ProviderConfig{
              .api_key = api_key,
              .base_url = base_url,
              .completions_endpoint_path = compute_completions_path(base_url),
              .embeddings_endpoint_path = get_embeddings_path(base_url),
              .auth_header_name = "Authorization",
              .auth_header_prefix = "Bearer ",
              .extra_headers = {}},
          std::make_unique<OpenAIRequestBuilder>(),
          std::make_unique<OpenAIResponseParser>()) {
  LOG_DEBUG("OpenAI client initialized with base_url: {}",
                        base_url);
}

OpenAIClient::OpenAIClient(const std::string& api_key,
                           const std::string& base_url,
                           const retry::RetryConfig& retry_config)
    : BaseProviderClient(
          providers::ProviderConfig{
              .api_key = api_key,
              .base_url = base_url,
              .completions_endpoint_path = compute_completions_path(base_url),
              .embeddings_endpoint_path = get_embeddings_path(base_url),
              .auth_header_name = "Authorization",
              .auth_header_prefix = "Bearer ",
              .extra_headers = {},
              .retry_config = retry_config},
          std::make_unique<OpenAIRequestBuilder>(),
          std::make_unique<OpenAIResponseParser>()) {
  LOG_DEBUG(
      "OpenAI client initialized with base_url: {} and custom retry config",
      base_url);
}

OpenAIClient::OpenAIClient(
    const std::string& api_key, const std::string& base_url,
    bool use_responses, const std::map<std::string, std::string>& headers)
    : BaseProviderClient(
          providers::ProviderConfig{
              .api_key = api_key,
              .base_url = base_url,
              .completions_endpoint_path =
                  compute_completions_path(base_url, use_responses),
              .embeddings_endpoint_path = get_embeddings_path(base_url),
              .auth_header_name = "Authorization",
              .auth_header_prefix = "Bearer ",
              .extra_headers = make_headers(headers)},
          std::make_unique<OpenAIRequestBuilder>(use_responses),
          std::make_unique<OpenAIResponseParser>()) {}

StreamResult OpenAIClient::stream_text(const StreamOptions& options) {
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
  auto impl = std::make_unique<OpenAIStreamImpl>();
  std::string stream_path = config_.completions_endpoint_path;
  impl->start_stream(config_.base_url + stream_path,
                     headers, request_json);

  LOG_INFO("Text streaming started - model: {}", options.model);

  // Return StreamResult with implementation
  return StreamResult(std::move(impl));
}

std::string OpenAIClient::provider_name() const {
  return "openai";
}

std::vector<std::string> OpenAIClient::supported_models() const {
  return {// Current GPT-5 series
          models::kGpt54, models::kGpt54Pro, models::kGpt54Mini,
          models::kGpt54Nano, models::kGpt5Mini, models::kGpt5Nano,
          // Current GPT-4.1 series
          models::kGpt41, models::kGpt41Mini,
          // Legacy / deprecated (still functional via API)
          models::kGpt4o, models::kGpt4oMini, models::kGpt4Turbo, models::kGpt4,
          models::kGpt35Turbo};
}

bool OpenAIClient::supports_model(const std::string& model_name) const {
  auto models = supported_models();
  return std::find(models.begin(), models.end(), model_name) != models.end();
}

std::string OpenAIClient::config_info() const {
  return "OpenAI API (base_url: " + config_.base_url + ")";
}

std::string OpenAIClient::default_model() const {
  return models::kDefaultModel;
}

}  // namespace openai
}  // namespace qcode
