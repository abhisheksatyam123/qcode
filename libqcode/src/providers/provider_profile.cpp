#include <qcode/providers/provider_profile.h>
#include <qcode/transform/provider_transform.h>

#include "providers/internal/opencode_zen_headers.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace qcode {
namespace {

std::string to_lower(std::string_view in) {
  std::string out{in};
  for (auto& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string normalize_workspace(std::string workspace) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(workspace, ec);
  if (!ec) workspace = canonical.string();
  while (workspace.size() > 1 && workspace.back() == '/') workspace.pop_back();
  return workspace;
}

}  // namespace

ProviderKind provider_kind(std::string_view provider_id,
                           std::string_view base_url) {
  const auto id = to_lower(provider_id);
  if (id == "cursor") return ProviderKind::kCursor;
  if (id == "opencode" || id == "zen") return ProviderKind::kOpenCodeZen;
  if (id == "openrouter") return ProviderKind::kOpenRouter;
  if (id == "antigravity") return ProviderKind::kAntigravity;

  const auto transport = ProviderTransform::chat_transport_for(std::string{base_url});
  if (transport == ProviderTransform::ChatTransport::kOpenCodeZen) {
    return ProviderKind::kOpenCodeZen;
  }
  if (transport == ProviderTransform::ChatTransport::kOpenRouter) {
    return ProviderKind::kOpenRouter;
  }
  return ProviderKind::kCompatible;
}

ProviderCall prepare_provider_call(providers::ProviderOptions& options,
                                   const std::string& provider_id,
                                   std::string model_id,
                                   const std::string& session_id,
                                   const std::string& parent_session_id,
                                   const std::string& workspace) {
  ProviderCall call;
  call.kind = provider_kind(provider_id, options.base_url);
  call.wire_model_id = std::move(model_id);

  switch (call.kind) {
    case ProviderKind::kOpenCodeZen: {
      call.wire_model_id =
          ProviderTransform::zen_wire_model_id(call.wire_model_id);
      apply_zen_route(options, call.wire_model_id);
      // Replace any session identity left on reused options; the caller's
      // current session (and parent, when it is a subagent) is authoritative.
      options.headers.erase("x-opencode-session-id");
      options.headers.erase("x-opencode-session");
      options.headers.erase("x-session-affinity");
      options.headers.erase("X-Session-Id");
      options.headers.erase("x-opencode-parent-session-id");
      options.headers.erase("x-parent-session-id");
      // OpenCode routes cache affinity through the parent session for
      // subagents while still reporting the child's concrete session id.
      const std::string affinity =
          parent_session_id.empty() ? session_id : parent_session_id;
      if (!session_id.empty()) {
        options.headers["x-opencode-session-id"] = session_id;
      }
      if (!affinity.empty()) {
        options.headers["x-opencode-session"] = affinity;
        options.headers["x-session-affinity"] = affinity;
      }
      if (!parent_session_id.empty()) {
        options.headers["x-opencode-parent-session-id"] = parent_session_id;
        options.headers["x-parent-session-id"] = parent_session_id;
      }
      if (!options.project_id.empty()) {
        options.headers["x-opencode-project"] = options.project_id;
      } else if (!workspace.empty()) {
        options.headers["x-opencode-project"] =
            providers::stable_opencode_project_id(
                normalize_workspace(workspace));
      }
      break;
    }
    case ProviderKind::kOpenRouter:
      call.wire_model_id =
          ProviderTransform::openrouter_wire_model_id(call.wire_model_id);
      if (options.protocol.empty()) {
        options.protocol = wire_protocol_id(WireProtocol::kChatCompletions);
      }
      break;
    case ProviderKind::kCursor:
    case ProviderKind::kAntigravity:
    case ProviderKind::kCompatible:
      if (to_lower(provider_id) == "anthropic") {
        if (call.wire_model_id.ends_with("-thinking")) {
          call.wire_model_id =
              call.wire_model_id.substr(0, call.wire_model_id.size() - 9);
        }
        if (options.protocol.empty()) {
          options.protocol = "messages";
        }
      }
      break;
  }
  return call;
}

}  // namespace qcode
