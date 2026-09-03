#include <qcode/core/config.h>
#include <qcode/session/session_store.h>
#include <qcode/core/ssl_config.h>
#include <qcode/providers/cursor.h>
#include <qcode/providers/provider_transform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include <qcode/core/logger.h>

namespace qcode {

static int resolve_model_context_window(const std::string& model_id) {
    auto summary = session::get_model_performance_summary(model_id);
    if (summary.context_window > 0) return summary.context_window;
    std::string lower = model_id;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("2m") != std::string::npos || lower.find("gemini-3.1") != std::string::npos) {
        return 2000000;
    }
    if (lower.find("gemini") != std::string::npos || lower.find("deepseek") != std::string::npos ||
        lower.find("nemotron") != std::string::npos || lower.find("1m") != std::string::npos) {
        return 1000000;
    }
    if (lower.find("claude") != std::string::npos) {
        return 200000;
    }
    return 200000;
}

using ordered_json = nlohmann::ordered_json;
// OpenCode uses these bundled Antigravity OAuth client credentials to refresh
// the token stored by the Antigravity CLI.
constexpr const char* kAntigravityOAuthClientId =
    "1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com";
constexpr const char* kAntigravityOAuthClientSecret =
    "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";

// Skip refresh retries for a while after a hard failure (invalid_grant, etc).
constexpr int kOAuthRefreshBackoffSec = 300;

static bool antigravity_token_expired(const std::string& expiry) {
    // Format: 2026-07-10T23:01:11.63815062+05:30 — parse wall-clock prefix.
    if (expiry.size() < 19) return true;
    std::tm tm{};
    std::istringstream ss(expiry.substr(0, 19));
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return true;
    tm.tm_isdst = -1;
    const auto expiry_time = std::mktime(&tm);
    if (expiry_time < 0) return true;
    // Refresh one minute early.
    return std::time(nullptr) >= (expiry_time - 60);
}

static std::string normalize_api_url(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

static std::string resolve_config_value(const ordered_json& value) {
    if (!value.is_string()) return "";
    const auto text = value.get<std::string>();
    constexpr std::string_view prefix = "{env:";
    if (text.starts_with(prefix) && text.ends_with('}')) {
        const auto name = text.substr(prefix.size(), text.size() - prefix.size() - 1);
        if (const char* env = std::getenv(name.c_str())) return env;
        return "";
    }
    return text;
}

static ModelInfo make_default_model(std::string name,
                                    std::string id,
                                    int context_window,
                                    int output_limit,
                                    bool tool_call = true,
                                    bool reasoning = false) {
    ModelInfo model;
    model.name = std::move(name);
    model.id = std::move(id);
    model.context_window = context_window;
    model.output_limit = output_limit;
    model.tool_call = tool_call;
    model.reasoning = reasoning;
    return model;
}

// ── models.opencode.ai catalog (mirrors upstream opencode ModelsDev) ──
// Upstream opencode fetches {OPENCODE_MODELS_URL:-https://models.opencode.ai}/api.json
// and caches it on disk with a 5-minute TTL (packages/core/src/models-dev.ts).
// The catalog drives model discovery for OpenCode Zen and OpenRouter:
//   - Zen free pool: when no OPENCODE_API_KEY is configured, upstream injects
//     apiKey="public" and only models with cost.input == 0 are usable
//     (packages/core/src/plugin/provider/opencode.ts).
//   - OpenRouter: activated by OPENROUTER_API_KEY; models come straight from the
//     catalog with HTTP-Referer/X-Title headers (plugin/provider/openrouter.ts).
static constexpr const char* kModelsDevUrl = "https://models.opencode.ai/api.json";
static constexpr const char* kOpenRouterReferer = "https://opencode.ai/";

static std::string models_dev_cache_path() {
    if (const char* p = std::getenv("QCODE_MODELS_CACHE")) return p;
    std::vector<std::filesystem::path> candidates;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        candidates.emplace_back(std::filesystem::path(xdg) / "qcode" / "models-dev.json");
    }
    if (const char* home = std::getenv("HOME")) {
        candidates.emplace_back(std::filesystem::path(home) / ".cache" / "qcode" / "models-dev.json");
    }
    return candidates.empty() ? "models-dev.json" : candidates.front().string();
}

// Fetch the catalog, preferring a cache file younger than ttl_seconds. On
// network failure fall back to a stale cache of any age. Returns false when no
// data is available at all.
static bool load_models_dev_catalog(ordered_json& out) {
    constexpr long kTtlSeconds = 300;  // upstream: Duration.minutes(5)
    const auto cache_path = models_dev_cache_path();

    auto cache_fresh = [&] {
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(cache_path, ec);
        if (ec) return false;
        const auto age = std::filesystem::file_time_type::clock::now() - mtime;
        return std::chrono::duration_cast<std::chrono::seconds>(age).count() < kTtlSeconds;
    };

    auto read_cache = [&](ordered_json& target) {
        std::ifstream file(cache_path);
        if (!file) return false;
        try {
            file >> target;
            return target.is_object();
        } catch (...) {
            return false;
        }
    };

    if (cache_fresh() && read_cache(out)) return true;

    httplib::Client client("https://models.opencode.ai");
    qcode::http::configure_client_tls(client, true);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(10, 0);
    httplib::Headers headers{{"User-Agent", "opencode/1.18.18"}};
    if (auto res = client.Get("/api.json", headers);
        res && res->status == 200 && !res->body.empty()) {
        try {
            ordered_json parsed = ordered_json::parse(res->body);
            if (parsed.is_object()) {
                out = std::move(parsed);
                std::error_code ec;
                std::filesystem::create_directories(
                    std::filesystem::path(cache_path).parent_path(), ec);
                const auto tmp = cache_path + ".tmp";
                std::ofstream out_file(tmp);
                if (out_file) {
                    out_file << out.dump();
                    out_file.close();
                    std::filesystem::rename(tmp, cache_path, ec);
                }
                return true;
            }
        } catch (const std::exception& e) {
            LOG_WARN("models.dev catalog parse error: {}", e.what());
        }
    } else {
        LOG_WARN("models.dev catalog fetch failed; trying stale cache");
    }

    return read_cache(out);  // stale fallback of any age
}

// Convert one catalog model entry to ModelInfo. Catalog costs are USD per
// token; qcode stores USD per 1M tokens (views.cpp divides token counts by 1e6).
static ModelInfo model_from_catalog(const std::string& id,
                                    const ordered_json& data) {
    ModelInfo model;
    model.name = data.value("name", id);
    // Prefer the catalog's wire id when the object key is a display slug.
    if (data.contains("id") && data["id"].is_string()) {
        const auto wire_id = data["id"].get<std::string>();
        model.id = wire_id.empty() ? id : wire_id;
    } else {
        model.id = id;
    }
    if (data.contains("limit") && data["limit"].is_object()) {
        model.context_window = data["limit"].value("context", 0);
        model.output_limit = data["limit"].value("output", 0);
    }
    if (data.contains("cost") && data["cost"].is_object()) {
        constexpr double kPerTokenToPerMillion = 1000000.0;
        model.input_cost =
            data["cost"].value("input", 0.0) * kPerTokenToPerMillion;
        model.output_cost =
            data["cost"].value("output", 0.0) * kPerTokenToPerMillion;
    }
    model.reasoning = data.value("reasoning", false);
    model.tool_call = data.value("tool_call", false);
    // Reasoning variants (mirrors upstream reasoningVariants): catalog
    // reasoning_options of type "effort" list the supported effort levels,
    // and interleaved.field names the assistant back-channel used to replay
    // reasoning on openai-compatible transports (e.g. "reasoning_content").
    if (data.contains("reasoning_options") && data["reasoning_options"].is_array()) {
        for (const auto& option : data["reasoning_options"]) {
            if (!option.is_object() || option.value("type", "") != "effort") continue;
            if (option.contains("values") && option["values"].is_array()) {
                for (const auto& value : option["values"]) {
                    if (value.is_string()) {
                        model.reasoning_efforts.push_back(value.get<std::string>());
                    }
                }
            }
        }
    }
    if (data.contains("interleaved") && data["interleaved"].is_object()) {
        const auto field = data["interleaved"].value("field", "");
        if (!field.empty()) model.reasoning_field = field;
    }
    if (model.context_window <= 0) {
        model.context_window = resolve_model_context_window(id);
    }
    ProviderTransform::apply_reasoning_defaults(model);
    return model;
}

// Curated picker allowlists (see below) intersect every models.dev catalog
// merge; config-declared models always pass through unfiltered.
template <std::size_t N>
bool id_in_list(const std::array<const char*, N>& list, const std::string& id) {
    return std::find_if(list.begin(), list.end(),
                        [&id](const char* entry) { return id == entry; }) !=
           list.end();
}

// Picker curation: the models.dev catalog exposes hundreds of ids per
// provider which bury the working entries in the picker (OpenRouter alone
// lists 350+). qcode only shows models that were probed to complete a chat
// round-trip through the full pipeline, so catalog merges are intersected
// with these curated allowlists instead of keeping every paid entry.
static constexpr std::array<const char*, 14> kCuratedZenCatalog = {
    "big-pickle",
    "laguna-s-2.1-free",
    "mimo-v2.5-free",
    "nemotron-3.5-lightning-free",
    "nemotron-3-ultra-free",
    "muse-spark-1.2-contributor-free",
    "muse-spark-1.3-contributor-free",
    "deepseek-v4-flash-free",
    "ling-3.0-flash-fin-free",
    "gpt-5.3-codex",
    "gemini-3.8-flash",
    "gemini-3.1-pro",
    "gemini-3-flash",
    "claude-sonnet-4-6",
};

static constexpr std::array<const char*, 18> kCuratedOpenRouterCatalog = {
    "deepseek/deepseek-v4-flash-0731",
    "openai/gpt-5.3-codex",
    "moonshotai/kimi-k3",
    "meta/muse-spark-1.2",
    "nvidia/nemotron-3-ultra-550b-a55b:free",
    "nvidia/nemotron-3.5-lightning:free",
    "nvidia/nemotron-3-super-120b-a12b:free",
    "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free",
    "google/gemma-4-31b-it:free",
    "google/gemma-4-26b-a4b-it:free",
    "poolside/laguna-s-2.1:free",
    "poolside/laguna-xs-2.1:free",
    "cohere/north-mini-code:free",
    "inclusionai/ling-3.0-flash-fin:free",
    "minimax/minimax-m3:free",
    "z-ai/glm-5.2:free",
    "dots-studio/dots-3-note-preview:free",
    "openrouter/free",
};

// GET https://opencode.ai/zen/v1/models no longer accepts these; they 401
// as ModelError. Drop from the picker even if an old opencode.json lists them.
static bool zen_id_is_retired(const std::string& id) {
    return id == "hy3-free" || id == "hy3-preview-free" ||
           id == "x-preview-f-free" || id == "north-mini-code-free" ||
           id == "kimi-k2.5-free";
}

template <std::size_t N>
static std::vector<ModelInfo> filter_catalog_allowlist(
    std::vector<ModelInfo> models,
    const std::array<const char*, N>& allow) {
    std::erase_if(models, [&allow](const ModelInfo& model) {
        if (id_in_list(allow, model.id)) return false;
        // Catalog keys drift; never drop Muse / free-pool aliases the TUI
        // is built to run. OpenRouter :free ids are also picker-safe.
        const auto& id = model.id;
        if (id.find("muse-spark") != std::string::npos ||
            id.find(":free") != std::string::npos ||
            id == "openrouter/free" ||
            id.find("-free") != std::string::npos) {
            return false;
        }
        LOG_DEBUG("Dropping non-curated catalog model from picker: {}",
                  model.id);
        return true;
    });
    return models;
}

// Zen catalog models intersected with kCuratedZenCatalog so the picker only
// lists ids that were probed to work end-to-end. has_api_key is unused here;
// availability curation happens via the allowlist, not the key state.
static std::vector<ModelInfo> zen_models_from_catalog(
    const ordered_json& catalog, bool has_api_key) {
    std::vector<ModelInfo> models;
    const auto zen_it = catalog.find("opencode");
    if (zen_it == catalog.end() || !zen_it->is_object()) return models;
    const auto models_it = zen_it->find("models");
    if (models_it == zen_it->end() || !models_it->is_object()) return models;
    models.reserve(models_it->size());
    for (auto it = models_it->begin(); it != models_it->end(); ++it) {
        const auto& data = it.value();
        // Upstream: without OPENCODE_API_KEY only cost.input == 0 models
        // stay enabled (packages/core/src/plugin/provider/opencode.ts).
        if (!has_api_key && data.contains("cost") && data["cost"].is_object() &&
            data["cost"].value("input", 0.0) > 0.0) {
            continue;
        }
        models.push_back(model_from_catalog(it.key(), data));
    }
    return filter_catalog_allowlist(std::move(models), kCuratedZenCatalog);
}

// OpenRouter provider from the catalog. Only built when OPENROUTER_API_KEY is
// configured — upstream treats env presence as activation.
static ProviderInfo openrouter_provider_from_catalog(
    const ordered_json& catalog) {
    ProviderInfo provider;
    provider.id = "openrouter";
    provider.name = "OpenRouter";
    provider.api_url = "https://openrouter.ai/api/v1";
    provider.protocol = "chat_completions";
    // Mirror plugin/provider/openrouter.ts attribution headers.
    provider.headers["HTTP-Referer"] = kOpenRouterReferer;
    provider.headers["X-Title"] = "opencode";

    const auto or_it = catalog.find("openrouter");
    if (or_it == catalog.end() || !or_it->is_object()) return provider;
    const auto models_it = or_it->find("models");
    if (models_it == or_it->end() || !models_it->is_object()) return provider;
    provider.models.reserve(models_it->size());
    for (auto it = models_it->begin(); it != models_it->end(); ++it) {
        provider.models.push_back(model_from_catalog(it.key(), it.value()));
    }
    provider.models = filter_catalog_allowlist(
        std::move(provider.models), kCuratedOpenRouterCatalog);
    return provider;
}

// Official OpenCode Zen free catalog (https://opencode.ai/docs/zen/).
// IDs match GET https://opencode.ai/zen/v1/models. Retired ids
// (hy3-free, x-preview-f-free, north-mini-code-free) 401 as ModelError.
static std::vector<ModelInfo> official_opencode_free_models() {
    std::vector<ModelInfo> models;
    models.reserve(9);

    {
        ModelInfo deepseek = make_default_model(
            "DeepSeek V4 Flash (Free)", "deepseek-v4-flash-free", 262144,
            65536, true, true);
        deepseek.reasoning_efforts = {"low", "high", "max"};
        deepseek.reasoning_field = "reasoning_content";
        models.push_back(std::move(deepseek));
    }
    {
        ModelInfo muse12 = make_default_model(
            "Muse Spark 1.2 (Free)", "muse-spark-1.2-contributor-free", 262144,
            65536, true, true);
        muse12.reasoning_efforts = {"low", "medium", "high"};
        muse12.reasoning_field = "reasoning";
        muse12.protocol = "responses";
        models.push_back(std::move(muse12));
    }
    {
        ModelInfo muse13 = make_default_model(
            "Muse Spark 1.3 (Free)", "muse-spark-1.3-contributor-free", 262144,
            65536, true, true);
        muse13.reasoning_efforts = {"low", "medium", "high"};
        muse13.reasoning_field = "reasoning";
        muse13.protocol = "responses";
        models.push_back(std::move(muse13));
    }
    models.push_back(make_default_model("Big Pickle (Free)", "big-pickle",
                                        200000, 32000, true, true));
    models.push_back(
        make_default_model("MiMo V2.5 (Free)", "mimo-v2.5-free", 200000, 32000, true, true));
    models.push_back(make_default_model("Laguna S 2.1 (Free)", "laguna-s-2.1-free",
                                        262144, 32768, true, true));
    models.push_back(make_default_model("Nemotron 3 Ultra (Free)",
                                        "nemotron-3-ultra-free", 1000000, 128000, true,
                                        true));
    models.push_back(make_default_model("Nemotron 3.5 Lightning (Free)",
                                        "nemotron-3.5-lightning-free", 262144, 65536, true,
                                        true));
    models.push_back(make_default_model("Ling 3.0 Flash Fin (Free)",
                                        "ling-3.0-flash-fin-free", 262144, 65536, true,
                                        true));
    return models;
}

static std::vector<ModelInfo> official_openrouter_core_models() {
    std::vector<ModelInfo> models;
    {
        ModelInfo muse = make_default_model(
            "Muse Spark 1.2", "meta/muse-spark-1.2", 1000000, 65536, true,
            true);
        muse.reasoning_efforts = {"minimal", "low", "medium", "high", "xhigh"};
        muse.reasoning_field = "reasoning";
        models.push_back(std::move(muse));
    }
    models.push_back(make_default_model(
        "Nemotron 3 Super 120B (Free)",
        "nvidia/nemotron-3-super-120b-a12b:free", 262144, 32768, true, true));
    models.push_back(make_default_model(
        "Laguna XS 2.1 (Free)", "poolside/laguna-xs-2.1:free", 262144, 32768,
        true, true));
    models.push_back(make_default_model(
        "Ling 3.0 Flash Fin (Free)", "inclusionai/ling-3.0-flash-fin:free",
        262144, 65536, true, true));
    models.push_back(make_default_model(
        "MiniMax M3 (Free)", "minimax/minimax-m3:free", 200000, 32768, true,
        true));
    return models;
}

static std::vector<ModelInfo> official_antigravity_models() {
    std::vector<ModelInfo> models;
    models.reserve(8);
    {
        ModelInfo flash38 = make_default_model(
            "Gemini 3.8 Flash", "gemini-3.8-flash", 1000000, 65536, true, true);
        flash38.reasoning_efforts = {"low", "medium", "high"};
        models.push_back(std::move(flash38));
    }
    {
        ModelInfo flash37 = make_default_model(
            "Gemini 3.7 Flash", "gemini-3.7-flash", 1000000, 65536, true, true);
        flash37.reasoning_efforts = {"low", "medium", "high"};
        models.push_back(std::move(flash37));
    }
    {
        ModelInfo flash36 = make_default_model(
            "Gemini 3.6 Flash", "gemini-3.6-flash", 1000000, 65536, true, true);
        flash36.reasoning_efforts = {"low", "medium", "high"};
        models.push_back(std::move(flash36));
    }
    {
        ModelInfo pro = make_default_model(
            "Gemini 3.1 Pro", "gemini-3.1-pro", 2000000, 65536, true, true);
        pro.reasoning_efforts = {"low", "medium", "high"};
        models.push_back(std::move(pro));
    }
    models.push_back(make_default_model("Gemini 3 Flash", "gemini-3-flash",
                                        1000000, 8192, true, true));
    {
        ModelInfo sonnet = make_default_model(
            "Claude Sonnet 4.6", "claude-sonnet-4-6", 200000, 8192, true, true);
        sonnet.reasoning_efforts = {"low", "medium", "high"};
        models.push_back(std::move(sonnet));
    }
    {
        ModelInfo opus = make_default_model(
            "Claude Opus 4.6 Thinking", "claude-opus-4-6-thinking", 200000,
            8192, true, true);
        opus.reasoning_efforts = {"low", "medium", "high"};
        models.push_back(std::move(opus));
    }
    models.push_back(make_default_model("Gemini 2.5 Flash", "gemini-2.5-flash",
                                        1000000, 8192, true, true));
    return models;
}

static bool opencode_has_api_key() {
    if (const char* key = std::getenv("OPENCODE_API_KEY"); key && *key) return true;
    // A config-file apiKey ({env:...} resolved earlier or literal) counts too;
    // callers pass what they know via parameter where needed.
    return false;
}

static bool skip_live_model_discovery() {
#ifdef QCODE_TESTING
    return true;
#endif
    const char* value = std::getenv("QCODE_SKIP_LIVE_MODELS");
    return value != nullptr && *value != '\0' && value[0] != '0';
}

static bool zen_id_is_keyless(const std::string& id,
                              const ordered_json* catalog_entry) {
    if (id.find("-free") != std::string::npos || id == "big-pickle") {
        return true;
    }
    if (catalog_entry != nullptr && catalog_entry->contains("cost") &&
        (*catalog_entry)["cost"].is_object()) {
        return (*catalog_entry)["cost"].value("input", 0.0) <= 0.0;
    }
    return false;
}

// GET https://opencode.ai/zen/v1/models — source of truth for ids Zen
// currently accepts. Cached in-process for 5 minutes.
static bool load_zen_live_model_ids(std::vector<std::string>& out) {
    if (skip_live_model_discovery()) return false;

    static std::mutex mutex;
    static std::vector<std::string> cached;
    static std::chrono::steady_clock::time_point fetched_at{};
    static bool have_cache = false;
    constexpr auto kTtl = std::chrono::seconds(300);

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (have_cache &&
            std::chrono::steady_clock::now() - fetched_at < kTtl) {
            out = cached;
            return !out.empty();
        }
    }

    httplib::Client client("https://opencode.ai");
    qcode::http::configure_client_tls(client, true);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(8, 0);
    httplib::Headers headers{
        {"User-Agent", "opencode/1.18.18"},
        {"x-opencode-client", "cli"},
    };
    if (const char* key = std::getenv("OPENCODE_API_KEY"); key && *key) {
        headers.emplace("Authorization", std::string("Bearer ") + key);
    } else {
        headers.emplace("Authorization", "Bearer public");
    }

    std::vector<std::string> ids;
    if (auto res = client.Get("/zen/v1/models", headers);
        res && res->status == 200 && !res->body.empty()) {
        try {
            const auto parsed = ordered_json::parse(res->body);
            const auto& data =
                parsed.contains("data") ? parsed["data"] : parsed;
            if (data.is_array()) {
                ids.reserve(data.size());
                for (const auto& entry : data) {
                    if (!entry.is_object()) continue;
                    auto id = entry.value("id", "");
                    if (!id.empty()) ids.push_back(std::move(id));
                }
            }
        } catch (const std::exception& error) {
            LOG_WARN("Zen /v1/models parse error: {}", error.what());
        }
    } else {
        LOG_WARN("Zen /v1/models fetch failed (status {})",
                 res ? res->status : 0);
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        cached = ids;
        fetched_at = std::chrono::steady_clock::now();
        have_cache = !ids.empty();
    }
    out = std::move(ids);
    return !out.empty();
}

static const ordered_json* catalog_model_entry(const ordered_json& catalog,
                                               const char* provider,
                                               const std::string& id) {
    const auto provider_it = catalog.find(provider);
    if (provider_it == catalog.end() || !provider_it->is_object()) return nullptr;
    const auto models_it = provider_it->find("models");
    if (models_it == provider_it->end() || !models_it->is_object()) return nullptr;
    const auto model_it = models_it->find(id);
    if (model_it == models_it->end() || !model_it->is_object()) return nullptr;
    return &(*model_it);
}

static bool antigravity_credentials_present() {
    if (const char* key = std::getenv("ANTIGRAVITY_API_KEY");
        key != nullptr && *key != '\0') {
        return true;
    }
    std::filesystem::path token_path;
    if (const char* configured = std::getenv("ANTIGRAVITY_TOKEN_FILE")) {
        token_path = configured;
    } else if (const char* home = std::getenv("HOME")) {
        token_path = std::filesystem::path(home) /
                     ".gemini/antigravity-cli/antigravity-oauth-token";
    }
    std::error_code error;
    return !token_path.empty() && std::filesystem::is_regular_file(token_path, error);
}

static std::string form_encode(std::string_view value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (const auto ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << ch;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0')
                    << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string get_antigravity_token(bool force_refresh) {
    if (const char* token = std::getenv("ANTIGRAVITY_API_KEY");
        token != nullptr && *token != '\0') {
        return token;
    }

    std::filesystem::path token_path;
    if (const char* configured = std::getenv("ANTIGRAVITY_TOKEN_FILE")) {
        token_path = configured;
    } else if (const char* home = std::getenv("HOME")) {
        token_path = std::filesystem::path(home) /
                     ".gemini/antigravity-cli/antigravity-oauth-token";
    }
    if (token_path.empty()) return "";

    ordered_json document;
    try {
        std::ifstream input(token_path);
        if (!input) return "";
        document = ordered_json::parse(input);
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to read Antigravity token file: {}", error.what());
        return "";
    }

    if (!document.contains("token") || !document["token"].is_object()) {
        LOG_ERROR("Antigravity token file has no token object");
        return "";
    }
    auto& token = document["token"];
    const auto access_token = token.value("access_token", "");
    const auto refresh_token = token.value("refresh_token", "");
    const char* client_id = std::getenv("ANTIGRAVITY_OAUTH_CLIENT_ID");
    const char* client_secret = std::getenv("ANTIGRAVITY_OAUTH_CLIENT_SECRET");
    if (client_id == nullptr || *client_id == '\0') {
        client_id = kAntigravityOAuthClientId;
    }
    if (client_secret == nullptr || *client_secret == '\0') {
        client_secret = kAntigravityOAuthClientSecret;
    }
    if (refresh_token.empty() || client_id == nullptr || *client_id == '\0') {
        return access_token;
    }

    // Reuse a still-valid access token unless a forced refresh was requested.
    const auto expiry = token.value("expiry", "");
    if (!force_refresh && !expiry.empty() && !antigravity_token_expired(expiry)) {
        return access_token;
    }

    static std::mutex refresh_mutex;
    static std::time_t last_refresh_failure = 0;
    {
        std::lock_guard<std::mutex> lock(refresh_mutex);
        if (!force_refresh && last_refresh_failure > 0 &&
            std::time(nullptr) - last_refresh_failure < kOAuthRefreshBackoffSec) {
            return access_token;
        }
    }

    auto body = "grant_type=refresh_token&refresh_token=" +
                form_encode(refresh_token) + "&client_id=" +
                form_encode(client_id);
    if (client_secret != nullptr && *client_secret != '\0') {
        body += "&client_secret=" + form_encode(client_secret);
    }
    httplib::Client client("https://oauth2.googleapis.com");
    qcode::http::configure_client_tls(client, true);
    client.set_connection_timeout(5);
    client.set_read_timeout(10);
    const auto response =
        client.Post("/token", body, "application/x-www-form-urlencoded");
    if (!response || response->status != 200) {
        LOG_ERROR("Antigravity OAuth refresh failed with status {}",
                  response ? response->status : 0);
        std::lock_guard<std::mutex> lock(refresh_mutex);
        last_refresh_failure = std::time(nullptr);
        return access_token;
    }
    try {
        const auto refreshed = ordered_json::parse(response->body);
        const auto fresh_token = refreshed.value("access_token", "");
        if (fresh_token.empty()) return access_token;
        token["access_token"] = fresh_token;
        if (refreshed.contains("expires_in")) {
            token["expires_in"] = refreshed["expires_in"];
            const auto expires_in = refreshed["expires_in"].get<int>();
            const auto expiry_time = std::time(nullptr) + expires_in;
            std::tm tm{};
            localtime_r(&expiry_time, &tm);
            std::ostringstream expiry_out;
            expiry_out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
            token["expiry"] = expiry_out.str();
        }
        const auto temporary = token_path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            output << document.dump(2);
        }
        std::error_code error;
        std::filesystem::rename(temporary, token_path, error);
        if (error) std::filesystem::remove(temporary);
        {
            std::lock_guard<std::mutex> lock(refresh_mutex);
            last_refresh_failure = 0;
        }
        return fresh_token;
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to parse Antigravity OAuth response: {}", error.what());
        std::lock_guard<std::mutex> lock(refresh_mutex);
        last_refresh_failure = std::time(nullptr);
    }
    return access_token;
}

bool antigravity_token_needs_refresh() {
    if (const char* token = std::getenv("ANTIGRAVITY_API_KEY");
        token != nullptr && *token != '\0') {
        return false;
    }
    std::filesystem::path token_path;
    if (const char* configured = std::getenv("ANTIGRAVITY_TOKEN_FILE")) {
        token_path = configured;
    } else if (const char* home = std::getenv("HOME")) {
        token_path = std::filesystem::path(home) /
                     ".gemini/antigravity-cli/antigravity-oauth-token";
    }
    if (token_path.empty()) return true;
    try {
        std::ifstream input(token_path);
        if (!input) return true;
        const auto document = ordered_json::parse(input);
        if (!document.contains("token") || !document["token"].is_object()) {
            return true;
        }
        const auto& token = document["token"];
        if (token.value("access_token", "").empty()) return true;
        const auto expiry = token.value("expiry", "");
        if (!antigravity_token_expired(expiry)) return false;

#ifdef QCODE_TESTING
        return true;
#else
        const auto token_before = token.value("access_token", "");
        const auto fresh = get_antigravity_token();
        return fresh.empty() || fresh == token_before;
#endif
    } catch (...) {
        return true;
    }
}

std::string get_cursor_access_token() {
    if (const char* token = std::getenv("CURSOR_API_KEY");
        token != nullptr && *token != '\0') {
        return token;
    }
    std::filesystem::path token_path;
    if (const char* configured = std::getenv("CURSOR_AUTH_FILE")) {
        token_path = configured;
    } else if (const char* home = std::getenv("HOME")) {
        token_path = std::filesystem::path(home) / ".config/cursor/auth.json";
    }
    if (token_path.empty()) return "";

    ordered_json document;
    try {
        std::ifstream input(token_path);
        if (!input) return "";
        document = ordered_json::parse(input);
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to read Cursor auth file: {}", error.what());
        return "";
    }
    std::string access = document.value("accessToken", "");
    if (access.empty()) {
        LOG_ERROR("Cursor auth file has no accessToken");
    }
    return access;
}

std::string config_path() {
    if (const char* p = std::getenv("OPENCODE_CONFIG")) return p;
    std::vector<std::filesystem::path> candidates;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        candidates.emplace_back(
            std::filesystem::path(xdg) / "opencode/opencode.json");
    }
    if (const char* h = std::getenv("HOME")) {
        const auto home = std::filesystem::path(h);
        candidates.emplace_back(home / ".config/opencode/opencode.json");
        candidates.emplace_back(home / "notes/etc/opencode.json");
    }
    if (const char* notes = std::getenv("OPENCODE_NOTES_ROOT")) {
        candidates.emplace_back(
            std::filesystem::path(notes) / "etc/opencode.json");
    }
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate.string();
        }
    }
    return "opencode.json";
}

std::string get_notes_root() {
    if (const char* r = std::getenv("QCODE_NOTES_ROOT")) return r;
    if (const char* r = std::getenv("OPENCODE_NOTES_ROOT")) return r;
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/notes";
    return "notes";
}

namespace {

std::string cursor_display_name(const std::string& picker_id) {
    if (picker_id == "grok-4.6") return "Grok 4.6";
    if (picker_id == "claude-opus-5") return "Claude Opus 5";
    if (picker_id == "claude-sonnet-5") return "Claude Sonnet 5";
    if (picker_id == "claude-fable-5") return "Claude Fable 5";
    if (picker_id == "claude-fable-5-1") return "Claude Fable 5.1";
    if (picker_id == "composer-2.5") return "Composer 2.5";
    if (picker_id == "gpt-5.6-sol") return "GPT-5.6 Sol";
    if (picker_id == "gpt-5.6-terra") return "GPT-5.6 Terra";
    if (picker_id == "gpt-5.6-luna") return "GPT-5.6 Luna";
    return picker_id;
}

ModelInfo make_cursor_family_model(const std::string& id, std::string name) {
    ModelInfo model;
    model.name = name.empty() ? cursor_display_name(id) : std::move(name);
    model.id = id;
    model.reasoning = true;
    model.reasoning_efforts = {"low", "medium", "high"};
    model.tool_call = true;
    model.context_window = 200000;
    model.output_limit = 8192;
    return model;
}

// Current Cursor Agent families (GetUsableModels SKUs collapse onto these
// via cursor_picker_id). Used as fallback and always merged so the picker
// still lists working ids when the live catalog is empty.
std::vector<ModelInfo> official_cursor_models() {
    return {
        make_cursor_family_model("grok-4.6", "Grok 4.6"),
        make_cursor_family_model("claude-opus-5", "Claude Opus 5"),
        make_cursor_family_model("claude-sonnet-5", "Claude Sonnet 5"),
        make_cursor_family_model("claude-fable-5-1", "Claude Fable 5.1"),
        make_cursor_family_model("composer-2.5", "Composer 2.5"),
        make_cursor_family_model("gpt-5.6-sol", "GPT-5.6 Sol"),
        make_cursor_family_model("gpt-5.6-terra", "GPT-5.6 Terra"),
    };
}

void add_opencode_model_if_missing(std::vector<ModelInfo>& models,
                                   ModelInfo model) {
    const auto found = std::find_if(
        models.begin(), models.end(),
        [&model](const ModelInfo& existing) { return existing.id == model.id; });
    if (found == models.end()) {
        models.emplace_back(std::move(model));
    }
}

void hydrate_opencode_models(ProviderInfo& provider) {
    if (provider.api_url.empty()) {
        provider.api_url = "https://opencode.ai/zen/v1";
    }
    provider.protocol = "chat_completions";
    provider.headers["User-Agent"] = "opencode/1.18.18";
    provider.headers["x-opencode-client"] = "cli";
    // Retired Zen ids 401 as ModelError (hy3-free, Ox Alpha, North Mini).
    std::erase_if(provider.models, [](const ModelInfo& model) {
        return zen_id_is_retired(model.id);
    });

    const bool has_key =
        (!provider.api_key.empty() && provider.api_key != "__EMPTY__") ||
        opencode_has_api_key();

    ordered_json catalog;
    const bool have_catalog = load_models_dev_catalog(catalog);
    std::vector<std::string> live_ids;
    if (load_zen_live_model_ids(live_ids)) {
        std::unordered_map<std::string, ModelInfo> configured;
        for (const auto& model : provider.models) {
            configured.emplace(model.id, model);
        }
        std::unordered_map<std::string, ModelInfo> official;
        for (auto& model : official_opencode_free_models()) {
            official.emplace(model.id, std::move(model));
        }

        std::vector<ModelInfo> live_models;
        live_models.reserve(live_ids.size() + provider.models.size());
        std::unordered_set<std::string> seen;

        // Keep configured models in their original configured order
        for (auto& model : provider.models) {
            if (seen.insert(model.id).second) {
                ProviderTransform::apply_reasoning_defaults(model);
                model.protocol = ProviderTransform::zen_api_protocol(model.id);
                live_models.push_back(std::move(model));
            }
        }

        for (const auto& id : live_ids) {
            if (seen.count(id)) continue;
            const auto* entry =
                have_catalog ? catalog_model_entry(catalog, "opencode", id)
                             : nullptr;
            if (!has_key && !zen_id_is_keyless(id, entry)) continue;

            ModelInfo model;
            if (entry != nullptr) {
                model = model_from_catalog(id, *entry);
            } else if (auto it = official.find(id); it != official.end()) {
                model = it->second;
            } else {
                model = make_default_model(id, id, resolve_model_context_window(id),
                                           8192, true, false);
            }
            if (auto it = configured.find(id); it != configured.end()) {
                if (!it->second.name.empty()) model.name = it->second.name;
            } else if (auto it = official.find(id);
                       it != official.end() && model.name == model.id) {
                model.name = it->second.name;
            }
            ProviderTransform::apply_reasoning_defaults(model);
            model.protocol = ProviderTransform::zen_api_protocol(model.id);
            seen.insert(id);
            live_models.push_back(std::move(model));
        }

        provider.models = std::move(live_models);
        for (auto& model : provider.models) {
            ProviderTransform::apply_reasoning_defaults(model);
            if (model.protocol.empty()) {
                model.protocol = ProviderTransform::zen_api_protocol(model.id);
            }
        }
        LOG_INFO("OpenCode Zen picker: {} live models ({})",
                 provider.models.size(), has_key ? "keyed" : "keyless");
        return;
    }

    if (have_catalog) {
        for (auto& model : zen_models_from_catalog(catalog, has_key)) {
            add_opencode_model_if_missing(provider.models, std::move(model));
        }
    } else {
        LOG_WARN("models.dev catalog unavailable; using built-in free list");
    }
    for (auto& model : official_opencode_free_models()) {
        add_opencode_model_if_missing(provider.models, std::move(model));
    }
    for (auto& model : provider.models) {
        ProviderTransform::apply_reasoning_defaults(model);
        model.protocol = ProviderTransform::zen_api_protocol(model.id);
    }
}

// Auto-discover OpenRouter models from the catalog when its API key is set,
// mirroring upstream env-activation. No-op when key or catalog is missing.
// OpenRouter hydration: when OPENROUTER_API_KEY is set, ensure an openrouter
// provider exists and merge in the full catalog model list (config-defined
// entries win; catalog fills the rest — mirrors upstream's merge-over-database).
static void maybe_hydrate_openrouter(std::vector<ProviderInfo>& providers) {
    const char* key = std::getenv("OPENROUTER_API_KEY");
    if (!key || !*key) return;

    // Only auto-add or hydrate OpenRouter if config already defines an openrouter entry.
    // Do not inject OpenRouter when a scoped config test defines a custom non-OpenRouter provider.
    bool has_openrouter_or_no_config = false;
    for (const auto& provider : providers) {
        if (provider.id == "openrouter") {
            has_openrouter_or_no_config = true;
            break;
        }
    }
    if (!has_openrouter_or_no_config) return;

    ProviderInfo* existing = nullptr;
    for (auto& provider : providers) {
        if (provider.id == "openrouter") { existing = &provider; break; }
    }

    static const auto default_headers = [] {
        std::map<std::string, std::string> headers;
        headers["HTTP-Referer"] = std::string(kOpenRouterReferer);
        headers["X-Title"] = "opencode";
        return headers;
    }();

    ordered_json catalog;
    const bool have_catalog = load_models_dev_catalog(catalog);

    if (!existing) {
        ProviderInfo provider;
        provider.name = "OpenRouter";
        provider.id = "openrouter";
        provider.api_url = "https://openrouter.ai/api/v1";
        provider.protocol = "chat_completions";
        provider.headers = default_headers;
        provider.api_key = key;
        if (have_catalog) {
            provider.models =
                openrouter_provider_from_catalog(catalog).models;
        }
        for (auto& model : official_openrouter_core_models()) {
            add_opencode_model_if_missing(provider.models, std::move(model));
        }
        providers.push_back(std::move(provider));
        return;
    }

    // Merge into the config-defined provider: attribution headers, api key
    // fallback, then catalog models appended after configured ones.
    existing->headers.insert(default_headers.begin(), default_headers.end());
    if (existing->api_key.empty()) existing->api_key = key;
    if (existing->api_url.empty()) {
        existing->api_url = "https://openrouter.ai/api/v1";
    }
    // Keep every config-declared model (including paid). Catalog merge is
    // already allowlisted / :free-filtered so the picker stays usable.
    if (have_catalog) {
        for (auto& model :
             openrouter_provider_from_catalog(catalog).models) {
            add_opencode_model_if_missing(existing->models, std::move(model));
        }
    }
    for (auto& model : official_openrouter_core_models()) {
        add_opencode_model_if_missing(existing->models, std::move(model));
    }
    for (auto& model : existing->models) {
        ProviderTransform::apply_reasoning_defaults(model);
        if (model.protocol.empty()) model.protocol = "chat_completions";
    }
    existing->protocol = "chat_completions";
}

std::vector<ModelInfo> collapse_cursor_live_models(
    const std::vector<qcode::cursor::AvailableModel>& live) {
    std::vector<ModelInfo> models;
    std::unordered_set<std::string> seen;
    for (const auto& entry : live) {
        if (entry.id.empty()) continue;
        const auto picker_id = ProviderTransform::cursor_picker_id(entry.id);
        if (!seen.insert(picker_id).second) continue;
        std::string name = entry.name;
        if (name.empty() || name == entry.id) {
            name = cursor_display_name(picker_id);
        }
        models.push_back(make_cursor_family_model(picker_id, std::move(name)));
    }
    return models;
}

void hydrate_cursor_models(ProviderInfo& provider) {
    if (provider.api_url.empty()) {
        provider.api_url = "https://agentn.global.api5.cursor.sh";
    }
    provider.protocol = "chat_completions";

    std::vector<ModelInfo> models;
    const auto token = !provider.api_key.empty()
                           ? provider.api_key
                           : get_cursor_access_token();
    if (!skip_live_model_discovery() && !token.empty()) {
        try {
            const auto live = qcode::cursor::list_models(token);
            models = collapse_cursor_live_models(live);
        } catch (const std::exception& error) {
            LOG_WARN("Cursor live model catalog failed: {}", error.what());
        }
    }

    // Keep collapsed config-declared families (composer-2.5, gpt-5.6-*, …).
    for (const auto& configured : provider.models) {
        if (configured.id.empty()) continue;
        const auto picker_id = ProviderTransform::cursor_picker_id(configured.id);
        ModelInfo model = configured;
        model.id = picker_id;
        if (model.name.empty() || model.name == configured.id) {
            model.name = cursor_display_name(picker_id);
        }
        ProviderTransform::apply_reasoning_defaults(model);
        if (model.reasoning_efforts.empty()) {
            model.reasoning_efforts = {"low", "medium", "high"};
        }
        add_opencode_model_if_missing(models, std::move(model));
    }

    for (auto& model : official_cursor_models()) {
        add_opencode_model_if_missing(models, std::move(model));
    }

    for (auto& model : models) {
        ProviderTransform::apply_reasoning_defaults(model);
        if (model.protocol.empty()) model.protocol = "chat_completions";
    }

    LOG_INFO("Cursor picker models: {} ({})", models.size(),
             token.empty() ? "official" : "live+official");
    provider.models = std::move(models);
}

void hydrate_antigravity_models(ProviderInfo& provider) {
    if (provider.api_url.empty()) {
        provider.api_url =
            "https://daily-cloudcode-pa.sandbox.googleapis.com/v1internal";
    }
    provider.protocol = "chat_completions";
    for (auto& model : official_antigravity_models()) {
        add_opencode_model_if_missing(provider.models, std::move(model));
    }
    for (auto& model : provider.models) {
        ProviderTransform::apply_reasoning_defaults(model);
    }
    LOG_INFO("Antigravity picker models: {}", provider.models.size());
}

// Inject a Cursor provider when Cursor auth is available but the config does
// not declare one, so the model picker lists Cursor alongside the configured
// providers. Config-declared Cursor providers are left untouched.
static void maybe_inject_cursor_provider(std::vector<ProviderInfo>& providers) {
    if (std::getenv("OPENCODE_CONFIG") != nullptr) return;
    for (const auto& provider : providers) {
        if (provider.id == "cursor") return;
    }
    const auto token = get_cursor_access_token();
    if (token.empty()) return;

    ProviderInfo cursor;
    cursor.name = "Cursor";
    cursor.id = "cursor";
    cursor.api_url = "https://agentn.global.api5.cursor.sh";
    cursor.protocol = "chat_completions";
    hydrate_cursor_models(cursor);
    LOG_INFO("Injected Cursor provider with {} models", cursor.models.size());
    providers.push_back(std::move(cursor));
}

static void maybe_inject_antigravity_provider(std::vector<ProviderInfo>& providers) {
    if (std::getenv("OPENCODE_CONFIG") != nullptr) return;
    for (const auto& provider : providers) {
        if (provider.id == "antigravity") return;
    }
    if (!antigravity_credentials_present()) return;

    ProviderInfo antigravity;
    antigravity.name = "Antigravity";
    antigravity.id = "antigravity";
    hydrate_antigravity_models(antigravity);
    LOG_INFO("Injected Antigravity provider with {} models",
             antigravity.models.size());
    providers.push_back(std::move(antigravity));
}

// Build the default provider set when no opencode.json exists: OpenCode Zen
// plus OpenRouter, Cursor, and Antigravity when credentials are present.
static std::vector<ProviderInfo> default_providers() {
    std::vector<ProviderInfo> providers;

    ProviderInfo zen;
    zen.name = "OpenCode Zen";
    zen.id = "opencode";
    zen.api_url = "https://opencode.ai/zen/v1";
    zen.protocol = "chat_completions";
    hydrate_opencode_models(zen);
    providers.push_back(std::move(zen));

    if (const char* key = std::getenv("OPENROUTER_API_KEY"); key && *key) {
        ProviderInfo openrouter;
        openrouter.name = "OpenRouter";
        openrouter.id = "openrouter";
        providers.push_back(std::move(openrouter));
        maybe_hydrate_openrouter(providers);
    }

    maybe_inject_cursor_provider(providers);
    maybe_inject_antigravity_provider(providers);

    LOG_INFO("Default providers: {}", providers.size());
    return providers;
}

}  // namespace

std::vector<ProviderInfo> load_providers_from_config() {
    std::vector<ProviderInfo> loaded;
    std::string path = config_path();
    LOG_DEBUG("load_providers: path={}", path);
    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config not found, using defaults");
        return default_providers();
    }
    try {
        std::ifstream file(path);
        ordered_json config = ordered_json::parse(file);
        if (config.contains("provider")) {
            for (auto& [prov_id, prov_data] : config["provider"].items()) {
                ProviderInfo prov;
                prov.id = prov_id;
                prov.name = prov_data.value("name", prov_id);
                const auto options = prov_data.value(
                    "options", ordered_json::object());
                prov.api_url = normalize_api_url(
                    resolve_config_value(options.value("baseURL", ordered_json{})));
                if (prov.api_url.empty()) {
                    prov.api_url = normalize_api_url(prov_data.value("api", ""));
                }
                prov.api_key = resolve_config_value(
                    options.value("apiKey", ordered_json{}));
                if (options.contains("headers") && options["headers"].is_object()) {
                    for (const auto& [name, value] : options["headers"].items()) {
                        const auto resolved = resolve_config_value(value);
                        if (!resolved.empty()) prov.headers.emplace(name, resolved);
                    }
                }
                const auto package = prov_data.value("npm", "");
                if (prov.id == "opencode" || prov.id == "openrouter") {
                    prov.protocol = "chat_completions";
                } else if (package == "@ai-sdk/openai") {
                    prov.protocol = "responses";
                } else {
                    prov.protocol = "chat_completions";
                }
                prov.project_id = resolve_config_value(
                    options.value("project", ordered_json{}));
                if (prov_data.contains("models")) {
                    for (auto& [model_id, model_data] : prov_data["models"].items()) {
                        ModelInfo model;
                        model.name = model_data.value("name", model_id);
                        model.id = model_id;
                        if (model_data.contains("limit")) {
                            const auto& limit = model_data["limit"];
                            model.context_window = limit.value("context", 0);
                            model.output_limit = limit.value("output", 0);
                        }
                        if (model_data.contains("cost")) {
                            auto& c = model_data["cost"];
                            model.input_cost = c.value("input", 0.0);
                            model.output_cost = c.value("output", 0.0);
                        }
                        model.reasoning = model_data.value("reasoning", false);
                        ProviderTransform::apply_reasoning_defaults(model);
                        model.tool_call = model_data.value("tool_call", false);
                        model.protocol = model_data.value("protocol", prov.protocol);
                        if (model.context_window <= 0) {
                            model.context_window = resolve_model_context_window(model_id);
                        }
                        prov.models.push_back(model);
                    }
                }
                loaded.push_back(prov);
                LOG_INFO("Loaded provider: {} ({}), {} models, api={}",
                                     prov.name, prov.id, prov.models.size(),
                                     prov.api_url.empty() ? "(hardcoded)" : prov.api_url);
            }
        }
        for (auto& provider : loaded) {
            if (provider.id == "cursor") {
                hydrate_cursor_models(provider);
            }
            if (provider.id == "opencode") {
                hydrate_opencode_models(provider);
            }
            if (provider.id == "antigravity") {
                hydrate_antigravity_models(provider);
            }
        }
        maybe_hydrate_openrouter(loaded);
        maybe_inject_cursor_provider(loaded);
        maybe_inject_antigravity_provider(loaded);
    } catch (const std::exception& e) {
        LOG_ERROR("Config parse error: {}", e.what());
    }
    return loaded;
}

} // namespace qcode

