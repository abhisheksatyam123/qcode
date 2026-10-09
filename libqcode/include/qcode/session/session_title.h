#pragma once

#include <qcode/config/provider_info.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Session titles written by a small model, like opencode's title agent: the
// first prompt of a session that still has its default title is sent to a
// small, cheap model, and the session is renamed to its answer.
namespace qcode::session_title {

// "Session - <model>", the id itself, or empty: never named by the user.
bool is_default_title(const std::string& title, const std::string& session_id);

// The model's reply as a title: first non-empty line without thinking
// blocks, quotes, markdown or a "Title:" prefix, at most 60 characters.
// Empty when nothing usable is left.
std::string clean_title(std::string raw);

// Models to ask, best first (at most 3): opencode.json "small_model"
// ("provider/model"), then free fast models (opencode first), then other
// free models. Paid models only when named by small_model.
std::vector<std::pair<const ProviderInfo*, const ModelInfo*>> candidates(
    const std::vector<ProviderInfo>& providers, const std::string& small_model);

// Name `session_id` from `first_prompt` on a background thread when it is a
// top-level session with a default title (once per session per process).
// `on_titled(session_id, title)` runs on that thread after the rename.
void maybe_generate_async(
    std::shared_ptr<const std::vector<ProviderInfo>> providers,
    const std::string& session_id, const std::string& first_prompt,
    std::function<void(const std::string&, const std::string&)> on_titled);

// Wait up to `timeout` for running title jobs (process exit).
void shutdown(std::chrono::milliseconds timeout);

}  // namespace qcode::session_title
