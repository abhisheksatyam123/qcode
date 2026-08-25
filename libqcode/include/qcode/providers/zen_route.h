#pragma once

#include <qcode/providers/registry.h>

#include <string>
#include <string_view>

namespace qcode {

/// Wire formats from OpenCode's zen.mdx endpoint table.
enum class WireProtocol {
  kChatCompletions,
  kResponses,
  kMessages,
  kGoogle,
};

struct ModelRoute {
  WireProtocol protocol = WireProtocol::kChatCompletions;
  std::string path = "/chat/completions";
};

const char* wire_protocol_id(WireProtocol protocol);
WireProtocol parse_wire_protocol(std::string_view id);

/// Zen route for a catalog/wire model id (Ox Alpha → chat/completions, etc.).
ModelRoute zen_model_route(const std::string& model_id);

/// Google streaming uses `:streamGenerateContent?alt=sse` instead of
/// `:generateContent`. Other routes are unchanged.
std::string zen_stream_path(std::string completions_path);

/// Overwrite protocol + path on the options used to resolve a Zen client.
void apply_zen_route(providers::ProviderOptions& options,
                     const std::string& model_id);

}  // namespace qcode
