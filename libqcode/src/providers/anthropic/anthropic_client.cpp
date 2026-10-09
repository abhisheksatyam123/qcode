#include "anthropic_client.h"

#include <qcode/providers/anthropic.h>
#include <qcode/core/logger.h>
#include "anthropic_request_builder.h"
#include "anthropic_oauth.h"
#include "anthropic_response_parser.h"

#include "anthropic_stream.h"

#include <qcode/core/generate_options.h>
#include "generation/stream_step.h"
#include <cstdlib>
#include <string_view>


#include <httplib.h>

#include <algorithm>
#include <map>
#include <memory>

namespace qcode {
namespace anthropic {
namespace {

bool detect_oauth(const std::string& api_key, const CompatibleOptions& options) {
  if (options.is_oauth) return true;
  if (api_key.starts_with("sk-ant-oat")) return true;
  if (options.bearer_auth && (options.base_url.find("anthropic.com") != std::string::npos || options.base_url.empty())) {
    return true;
  }
  for (const auto& [k, v] : options.headers) {
    if (k == "User-Agent" && v.find("claude-cli") != std::string::npos) return true;
  }
  return false;
}

httplib::Headers extra_headers(
    const std::map<std::string, std::string>& headers,
    bool is_oauth,
    const std::string& token) {
  httplib::Headers extra{{"anthropic-version", "2023-06-01"}};
  if (is_oauth) {
    extra.emplace("anthropic-beta", kAnthropicBetaHeaders);
    extra.emplace("User-Agent", "claude-cli/2.1.300 (ext, cli)");
    extra.emplace("x-app", "cli");
    extra.emplace("x-anthropic-billing-header",
                  format_billing_header_value());
    const std::string token_suffix = token.size() > 8 ? token.substr(token.size() - 8) : token;
    extra.emplace("x-claude-code-session-id", sha1_hex("anthropic" + token_suffix));
  }
  for (const auto& [name, value] : headers) {
    if (is_oauth && (name == "User-Agent" || name == "anthropic-beta" || name == "x-app" || name == "x-claude-code-session-id" || name == "x-anthropic-billing-header")) {
      extra.erase(name);
    }
    extra.emplace(name, value);
  }
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
          [&]() {
            const bool oauth = detect_oauth(api_key, options);
            const bool bearer = options.bearer_auth || oauth;
            return providers::ProviderConfig{
                .api_key = api_key,
                .base_url = options.base_url,
                .completions_endpoint_path =
                    messages_path(options.base_url, options.completions_path),
                .auth_header_name = bearer ? "Authorization" : "x-api-key",
                .auth_header_prefix = bearer ? "Bearer " : "",
                .extra_headers = extra_headers(options.headers, oauth, api_key),
                .retry_config = options.retry_config};
          }(),
          std::make_unique<AnthropicRequestBuilder>(detect_oauth(api_key, options)),
          std::make_unique<AnthropicResponseParser>()) {
  const bool oauth = detect_oauth(api_key, options);
  LOG_DEBUG("Anthropic client initialized with base_url: {} bearer={} oauth={}",
            options.base_url, options.bearer_auth || oauth, oauth);
}

StreamResult AnthropicClient::stream_text(const StreamOptions& options) {
  LOG_DEBUG(
      "Starting text streaming - model: {}, prompt length: {}", options.model,
      options.prompt.length());

  auto request_json = request_builder_->build_stream_request_json(options);
  auto headers = request_builder_->build_headers(config_);
  headers.emplace("Accept", "text/event-stream");

  auto impl = std::make_unique<AnthropicStreamImpl>();
  if (config_.retry_config) {
    impl->set_max_retries(std::min(config_.retry_config->max_retries, 4));
  }
  impl->start_stream(config_.base_url + config_.completions_endpoint_path,
                     headers, request_json);

  LOG_INFO("Text streaming started - model: {}", options.model);
  return StreamResult(std::move(impl));
}

std::string AnthropicClient::provider_name() const {
  return "anthropic";
}

std::vector<std::string> AnthropicClient::supported_models() const {
  return {};
}

bool AnthropicClient::supports_model(const std::string& model_name) const {
  return !model_name.empty();
}

std::string AnthropicClient::config_info() const {
  return "Anthropic API (base_url: " + config_.base_url + ")";
}

std::string AnthropicClient::default_model() const {
  return models::kDefaultModel;
}


// Every Anthropic call streams: a non-streaming POST sends no bytes while the
// model thinks, and upstream cuts such idle connections after ~100s (the retry
// then regenerates from scratch). QCODE_ANTHROPIC_SYNC=1 restores the POST.
GenerateResult AnthropicClient::generate_text(const GenerateOptions& options) {
  if (const char* v = std::getenv("QCODE_ANTHROPIC_SYNC");
      v && std::string_view(v) == "1") {
    return BaseProviderClient::generate_text(options);
  }
  StreamOptions stream_opts(options);
  StreamResult stream;
  try {
    stream = stream_text(stream_opts);
  } catch (const std::exception& e) {
    return GenerateResult("Exception: " + std::string(e.what()));
  }
  StreamAccumulator accumulator;
  while (!stream.is_complete()) {
    if (options.abort_flag && options.abort_flag->load()) {
      stream.stop();
      return GenerateResult("Aborted by user");
    }
    if (auto event = stream.poll(std::chrono::milliseconds(250))) {
      accumulator.add(*event);
    }
  }
  // Drain anything queued after completion was flagged.
  while (auto event = stream.poll(std::chrono::milliseconds(0))) {
    accumulator.add(*event);
  }
  GenerateResult result = accumulator.take();
  result.model = options.model;
  return result;
}

}  // namespace anthropic
}  // namespace qcode
