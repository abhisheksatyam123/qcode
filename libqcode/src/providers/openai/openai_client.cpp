#include "openai_client.h"

#include <qcode/core/logger.h>
#include <qcode/providers/openai.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/providers/zen_route.h>
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
                                     bool use_responses = false,
                                     const std::string& override_path = {}) {
  if (!override_path.empty()) {
    return override_path.front() == '/' ? override_path : "/" + override_path;
  }
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
  request_builder_->set_base_url(base_url);
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
  request_builder_->set_base_url(base_url);
  LOG_DEBUG(
      "OpenAI client initialized with base_url: {} and custom retry config",
      base_url);
}

OpenAIClient::OpenAIClient(
    const std::string& api_key, const std::string& base_url,
    bool use_responses, const std::map<std::string, std::string>& headers)
    : OpenAIClient(api_key, base_url,
                   CompatibleOptions{.base_url = base_url,
                                     .protocol = use_responses
                                                     ? "responses"
                                                     : "chat_completions",
                                     .headers = headers}) {}

OpenAIClient::OpenAIClient(const std::string& api_key,
                           const std::string& base_url,
                           const CompatibleOptions& options)
    : BaseProviderClient(
          providers::ProviderConfig{
              .api_key = api_key,
              .base_url = base_url,
              .completions_endpoint_path = compute_completions_path(
                  base_url, options.protocol == "responses",
                  options.completions_path),
              .embeddings_endpoint_path = get_embeddings_path(base_url),
              .auth_header_name =
                  options.protocol == "google" &&
                          options.base_url.find("opencode.ai") ==
                              std::string::npos
                      ? "x-goog-api-key"
                      : "Authorization",
              .auth_header_prefix =
                  options.protocol == "google" &&
                          options.base_url.find("opencode.ai") ==
                              std::string::npos
                      ? ""
                      : "Bearer ",
              .extra_headers = make_headers(options.headers),
              .retry_config = options.retry_config},
          std::make_unique<OpenAIRequestBuilder>(options.protocol ==
                                                 "responses"),
          std::make_unique<OpenAIResponseParser>()) {
  request_builder_->set_base_url(base_url);
  request_builder_->set_wire_protocol(options.protocol);
  wire_protocol_ = options.protocol.empty() ? "chat_completions" : options.protocol;
  LOG_INFO("OpenAI client path={} protocol={} base={}",
           config_.completions_endpoint_path, options.protocol, base_url);
}

StreamResult OpenAIClient::stream_text(const StreamOptions& options) {
  LOG_DEBUG(
      "Starting text streaming - model: {}, prompt length: {}", options.model,
      options.prompt.length());

  auto request_json = request_builder_->build_request_json(options);
  const auto google = wire_protocol_ == "google";
  if (!google) {
    request_json["stream"] = true;
    // Upstream openai-chat always asks for usage accounting while streaming
    // (cached/reasoning token details ride along in the final chunk).
    ProviderTransform::apply_stream_options(
        request_json,
        ProviderTransform::chat_transport_for(config_.base_url),
        /*stream=*/true);
  }
  LOG_DEBUG("Stream request JSON built protocol={}", wire_protocol_);

  auto headers = request_builder_->build_headers(config_);
  headers.emplace("Accept", "text/event-stream");

  const auto protocol =
      google ? StreamProtocol::kGeminiEnvelope : StreamProtocol::kOpenAI;
  auto impl = std::make_unique<OpenAIStreamImpl>(protocol);
  const auto stream_path = zen_stream_path(config_.completions_endpoint_path);
  impl->start_stream(config_.base_url + stream_path, headers, request_json);

  LOG_INFO("Text streaming started - model: {}", options.model);

  // Return StreamResult with implementation
  return StreamResult(std::move(impl));
}

std::string OpenAIClient::provider_name() const {
  return "openai";
}

std::vector<std::string> OpenAIClient::supported_models() const {
  return {};
}

bool OpenAIClient::supports_model(const std::string& model_name) const {
  return !model_name.empty();
}

std::string OpenAIClient::config_info() const {
  return "OpenAI API (base_url: " + config_.base_url + ")";
}

std::string OpenAIClient::default_model() const {
  return models::kDefaultModel;
}

}  // namespace openai
}  // namespace qcode
