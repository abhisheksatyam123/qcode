#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <qcode/config/provider_info.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/chat_state.h>

namespace qcode {
namespace tui {

// Case-insensitive character equality
inline bool ci_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Case-insensitive substring match used by model/session/theme pickers.
inline bool matches_query(const std::string& target, const std::string& query) {
    if (query.empty()) return true;
    auto it = std::search(
        target.begin(), target.end(), query.begin(), query.end(),
        [](unsigned char ch1, unsigned char ch2) {
            return std::tolower(ch1) == std::tolower(ch2);
        });
    return it != target.end();
}

// Refresh ChatState.session_title from the DB (or a known title).
inline void sync_session_title(ChatState& state,
                               const std::string& known_title = "") {
    if (!state.session_title) return;
    if (!known_title.empty()) {
        *state.session_title = known_title;
        return;
    }
    if (!state.session_id || state.session_id->empty()) {
        state.session_title->clear();
        return;
    }
    *state.session_title = qcode::session::get_session_title(*state.session_id);
}

// Select the active session row index after a list refresh.
inline int index_of_session(const std::vector<qcode::session::SessionInfo>& entries,
                            const std::string& session_id) {
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[i].id == session_id) return i;
    }
    return 0;
}

// Resolves provider and model query strings (provider name/id, model name/id,
// or combo "provider:model") to (provider_idx, model_idx).
// Returns std::nullopt if no match is found.
inline std::optional<std::pair<int, int>> resolve_provider_model_indices(
    const std::vector<ProviderInfo>& providers,
    std::string_view prov_query,
    std::string_view model_query) {
    if (providers.empty()) return std::nullopt;

    std::string prov_str(prov_query);
    std::string model_str(model_query);

    // If provider is empty but model contains "provider:model", split them.
    if (prov_str.empty() && !model_str.empty()) {
        auto colon = model_str.find(':');
        if (colon != std::string::npos && colon > 0 && colon + 1 < model_str.size()) {
            std::string prefix = model_str.substr(0, colon);
            for (const auto& p : providers) {
                if (ci_equal(p.id, prefix) || ci_equal(p.name, prefix)) {
                    prov_str = prefix;
                    model_str = model_str.substr(colon + 1);
                    break;
                }
            }
        }
    }

    // Try finding provider
    int found_prov_idx = -1;
    if (!prov_str.empty()) {
        for (int i = 0; i < static_cast<int>(providers.size()); ++i) {
            if (providers[i].name == prov_str || providers[i].id == prov_str) {
                found_prov_idx = i;
                break;
            }
        }
        if (found_prov_idx < 0) {
            for (int i = 0; i < static_cast<int>(providers.size()); ++i) {
                if (ci_equal(providers[i].name, prov_str) || ci_equal(providers[i].id, prov_str)) {
                    found_prov_idx = i;
                    break;
                }
            }
        }
    }

    // If provider was found, look for model inside that provider
    if (found_prov_idx >= 0) {
        const auto& p = providers[found_prov_idx];
        if (!model_str.empty()) {
            // First check exact match
            for (int j = 0; j < static_cast<int>(p.models.size()); ++j) {
                if (p.models[j].name == model_str || p.models[j].id == model_str) {
                    return std::make_pair(found_prov_idx, j);
                }
            }
            // Case-insensitive match
            for (int j = 0; j < static_cast<int>(p.models.size()); ++j) {
                if (ci_equal(p.models[j].name, model_str) || ci_equal(p.models[j].id, model_str)) {
                    return std::make_pair(found_prov_idx, j);
                }
            }
            // If model_str has a "prefix:submodel" where prefix is this provider, strip prefix
            auto colon = model_str.find(':');
            if (colon != std::string::npos && colon + 1 < model_str.size()) {
                std::string stripped = model_str.substr(colon + 1);
                for (int j = 0; j < static_cast<int>(p.models.size()); ++j) {
                    if (ci_equal(p.models[j].name, stripped) || ci_equal(p.models[j].id, stripped)) {
                        return std::make_pair(found_prov_idx, j);
                    }
                }
            }
        }
        if (!p.models.empty()) {
            return std::make_pair(found_prov_idx, 0);
        }
    }

    // If provider was not found or was empty, search all providers for matching model
    if (!model_str.empty()) {
        for (int i = 0; i < static_cast<int>(providers.size()); ++i) {
            const auto& p = providers[i];
            for (int j = 0; j < static_cast<int>(p.models.size()); ++j) {
                if (p.models[j].name == model_str || p.models[j].id == model_str ||
                    ci_equal(p.models[j].name, model_str) || ci_equal(p.models[j].id, model_str)) {
                    return std::make_pair(i, j);
                }
            }
        }
        // Also check if model_str contains colon without provider match earlier
        auto colon = model_str.find(':');
        if (colon != std::string::npos && colon + 1 < model_str.size()) {
            std::string stripped = model_str.substr(colon + 1);
            for (int i = 0; i < static_cast<int>(providers.size()); ++i) {
                const auto& p = providers[i];
                for (int j = 0; j < static_cast<int>(p.models.size()); ++j) {
                    if (ci_equal(p.models[j].name, stripped) || ci_equal(p.models[j].id, stripped)) {
                        return std::make_pair(i, j);
                    }
                }
            }
        }
    }

    return std::nullopt;
}

}  // namespace tui
}  // namespace qcode
