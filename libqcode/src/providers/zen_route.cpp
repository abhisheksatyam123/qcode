#include <qcode/providers/zen_route.h>
#include <qcode/providers/provider_transform.h>

#include <cctype>
#include <string>
#include <string_view>

namespace qcode {
namespace {

std::string to_lower(std::string s) {
  for (auto& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool contains(const std::string& s, const std::string& sub) {
  return s.find(sub) != std::string::npos;
}

std::string bare_id(const std::string& id) {
  const auto slash = id.rfind('/');
  return slash == std::string::npos ? id : id.substr(slash + 1);
}

}  // namespace

const char* wire_protocol_id(WireProtocol protocol) {
  switch (protocol) {
    case WireProtocol::kResponses:
      return "responses";
    case WireProtocol::kMessages:
      return "messages";
    case WireProtocol::kGoogle:
      return "google";
    case WireProtocol::kChatCompletions:
      return "chat_completions";
  }
  return "chat_completions";
}

WireProtocol parse_wire_protocol(std::string_view id) {
  if (id == "responses") return WireProtocol::kResponses;
  if (id == "messages") return WireProtocol::kMessages;
  if (id == "google") return WireProtocol::kGoogle;
  return WireProtocol::kChatCompletions;
}

ModelRoute zen_model_route(const std::string& model_id) {
  const std::string id = to_lower(model_id);
  const std::string bare = bare_id(id);
  ModelRoute route;

  if (contains(id, "muse-spark") || contains(id, "grok") ||
      bare.rfind("gpt-", 0) == 0) {
    route.protocol = WireProtocol::kResponses;
    route.path = "/responses";
    return route;
  }
  if (contains(id, "claude") || bare.rfind("qwen3.", 0) == 0 ||
      bare.rfind("qwen3-", 0) == 0) {
    route.protocol = WireProtocol::kMessages;
    route.path = "/messages";
    return route;
  }
  if (contains(id, "gemini")) {
    route.protocol = WireProtocol::kGoogle;
    route.path = "/models/" + ProviderTransform::zen_wire_model_id(model_id) +
                 ":generateContent";
    return route;
  }
  route.protocol = WireProtocol::kChatCompletions;
  route.path = "/chat/completions";
  return route;
}

std::string zen_stream_path(std::string completions_path) {
  constexpr std::string_view kUnary = ":generateContent";
  const auto pos = completions_path.find(kUnary);
  if (pos == std::string::npos) return completions_path;
  completions_path.replace(pos, kUnary.size(), ":streamGenerateContent");
  if (completions_path.find('?') == std::string::npos) {
    completions_path += "?alt=sse";
  }
  return completions_path;
}

void apply_zen_route(providers::ProviderOptions& options,
                     const std::string& model_id) {
  const auto route = zen_model_route(model_id);
  if (options.protocol.empty()) {
    options.protocol = wire_protocol_id(route.protocol);
  }
  if (options.completions_path.empty()) {
    if (options.protocol == wire_protocol_id(WireProtocol::kResponses)) {
      options.completions_path = "/responses";
    } else if (options.protocol == wire_protocol_id(WireProtocol::kMessages)) {
      options.completions_path = "/messages";
    } else if (options.protocol == wire_protocol_id(WireProtocol::kGoogle)) {
      options.completions_path = route.path;
    } else if (options.protocol == wire_protocol_id(WireProtocol::kChatCompletions)) {
      options.completions_path = "/chat/completions";
    } else {
      options.completions_path = route.path;
    }
  }
}

}  // namespace qcode
