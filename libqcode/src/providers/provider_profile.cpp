#include <qcode/providers/provider_profile.h>
#include <qcode/providers/provider_transform.h>

#include <cctype>
#include <string>

namespace qcode {
namespace {

std::string to_lower(std::string_view in) {
  std::string out{in};
  for (auto& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

}  // namespace

ProviderKind provider_kind(std::string_view provider_id,
                           std::string_view base_url) {
  const auto id = to_lower(provider_id);
  if (id == "opencode" || id == "zen") return ProviderKind::kOpenCodeZen;
  if (id == "openrouter") return ProviderKind::kOpenRouter;
  if (id == "openai") return ProviderKind::kOpenAI;
  if (id == "anthropic" || id == "claude") return ProviderKind::kAnthropic;
  if (id == "cursor") return ProviderKind::kCursor;
  if (id == "antigravity") return ProviderKind::kAntigravity;
  if (id == "qpilot") return ProviderKind::kQPilot;
  if (id == "qgenie") return ProviderKind::kQGenie;

  const auto transport = ProviderTransform::chat_transport_for(std::string{base_url});
  if (transport == ProviderTransform::ChatTransport::kOpenCodeZen) {
    return ProviderKind::kOpenCodeZen;
  }
  if (transport == ProviderTransform::ChatTransport::kOpenRouter) {
    return ProviderKind::kOpenRouter;
  }
  if (transport == ProviderTransform::ChatTransport::kOpenAI) {
    return ProviderKind::kOpenAI;
  }
  if (to_lower(base_url).find("api.anthropic.com") != std::string::npos) {
    return ProviderKind::kAnthropic;
  }
  return ProviderKind::kCompatible;
}

ProviderCall prepare_provider_call(providers::ProviderOptions& options,
                                   const std::string& provider_id,
                                   std::string model_id,
                                   const std::string& session_id) {
  ProviderCall call;
  call.kind = provider_kind(provider_id, options.base_url);
  call.wire_model_id = std::move(model_id);

  switch (call.kind) {
    case ProviderKind::kOpenCodeZen:
      call.wire_model_id =
          ProviderTransform::zen_wire_model_id(call.wire_model_id);
      apply_zen_route(options, call.wire_model_id);
      if (!session_id.empty()) {
        options.headers["x-opencode-session"] = session_id;
      }
      if (!options.project_id.empty()) {
        options.headers["x-opencode-project"] = options.project_id;
      }
      break;
    case ProviderKind::kOpenRouter:
      call.wire_model_id =
          ProviderTransform::openrouter_wire_model_id(call.wire_model_id);
      options.protocol = wire_protocol_id(WireProtocol::kChatCompletions);
      options.completions_path.clear();
      break;
    case ProviderKind::kAnthropic:
      options.protocol = wire_protocol_id(WireProtocol::kMessages);
      break;
    case ProviderKind::kOpenAI:
    case ProviderKind::kCursor:
    case ProviderKind::kAntigravity:
    case ProviderKind::kQPilot:
    case ProviderKind::kQGenie:
    case ProviderKind::kCompatible:
      break;
  }
  return call;
}

}  // namespace qcode
