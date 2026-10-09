#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/client.h>

#include <string>

namespace qcode {

// A ready client for one catalog model (subagents, session titles).
struct ResolvedModelClient {
  Client client;
  std::string wire_model;
  std::string error;  // non-empty when no client could be built
  bool ok() const { return error.empty(); }
};

// Credentials are filled in like a lead turn: Antigravity / Anthropic OAuth
// tokens, Cursor token, OPENROUTER_API_KEY / OPENAI_API_KEY when the config
// has no key. `session_id` keys transports that route by session (Zen).
ResolvedModelClient resolve_model_client(const ProviderInfo& provider,
                                         const ModelInfo* model_info,
                                         const std::string& model_id,
                                         const std::string& session_id);

}  // namespace qcode
