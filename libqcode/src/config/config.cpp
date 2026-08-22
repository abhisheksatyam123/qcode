#include <qcode/config/config.h>
#include <qcode/session/session_store.h>
#include <qcode/http/ssl_config.h>
#include <qcode/providers/cursor.h>

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
#include <sstream>
#include <string>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include <qcode/logger/logger.h>

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
    model.id = id;
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
    if (model.context_window <= 0) {
        model.context_window = resolve_model_context_window(id);
    }
    return model;
}

// Live-probed 2026-08-22 through the full qcode pipeline (keyless): these are
// the only catalog "free" ids that actually complete a chat round-trip. The
// Zen Console free pool is invite-gated server-side, so most cost==0 catalog
// entries still 401 without OPENCODE_API_KEY; OpenRouter :free ids fail when
// their upstream provider is down or the model lacks tool support (qcode
// always advertises tools).
static constexpr std::array<const char*, 8> kVerifiedZenFree = {
    "big-pickle",
    "hy3-free",
    "laguna-s-2.1-free",
    "mimo-v2.5-free",
    "muse-spark-1.2-contributor-free",
    "nemotron-3-ultra-free",
    "nemotron-3.5-lightning-free",
    "x-preview-f-free",
};

static constexpr std::array<const char*, 13> kVerifiedOpenRouterFree = {
    "cohere/north-mini-code:free",
    "dots-studio/dots-3-note-preview:free",
    "liquid/lfm-2.5-2.6b:free",
    "nvidia/nemotron-3-nano-30b-a3b:free",
    "nvidia/nemotron-3-super-120b-a12b:free",
    "nvidia/nemotron-3-ultra-550b-a55b:free",
    "nvidia/nemotron-3.5-lightning:free",
    "nvidia/nemotron-nano-12b-v2-vl:free",
    "nvidia/nemotron-nano-9b-v2:free",
    "openrouter/free",
    "poolside/laguna-s-2.1:free",
    "poolside/laguna-xs-2.1:free",
    "stealth/ox-alpha",  // $0 promo pricing; verified working
};

template <std::size_t N>
bool id_in_list(const std::array<const char*, N>& list, const std::string& id) {
    return std::find_if(list.begin(), list.end(),
                        [&id](const char* entry) { return id == entry; }) !=
           list.end();
}

// Keep paid models (usable with keys) but drop free-tier entries that were
// probed broken keylessly — they only add picker noise.
static std::vector<ModelInfo> filter_verified(
    std::vector<ModelInfo> models,
    const std::array<const char*, 8>& zen_allow,
    const std::array<const char*, 13>& orr_allow,
    bool is_openrouter) {
    std::erase_if(models, [&](const ModelInfo& model) {
        const bool free_entry = model.input_cost == 0.0 && model.output_cost == 0.0;
        if (!free_entry) return false;  // paid: keep
        const bool allowed = is_openrouter ? id_in_list(orr_allow, model.id)
                                           : id_in_list(zen_allow, model.id);
        if (allowed) return false;  // verified free: keep
        LOG_DEBUG("Dropping non-working free model from picker: {}", model.id);
        return true;
    });
    return models;
}

// All Zen catalog models. Upstream hides cost>0 models when no OPENCODE_API_KEY
// is set (its keyless pool uses apiKey="public" and paid ids would 401); qcode
// keeps them visible because the model picker is explicit user intent — free
// models carry the catalog's "(Free)" naming and $0 costs, paid ones show real
// per-1M rates in the footer. has_api_key only affects default ordering needs,
// not availability.
static std::vector<ModelInfo> zen_models_from_catalog(
    const ordered_json& catalog, bool /*has_api_key*/) {
    std::vector<ModelInfo> models;
    const auto zen_it = catalog.find("opencode");
    if (zen_it == catalog.end()) return models;
    const auto& entries = zen_it->value("models", ordered_json::object());
    models.reserve(entries.size());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        models.push_back(model_from_catalog(it.key(), it.value()));
    }
    static constexpr std::array<const char*, 13> kNoOrrAllow = {};
    return filter_verified(std::move(models), kVerifiedZenFree,
                           kNoOrrAllow, false);
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
    if (or_it == catalog.end()) return provider;
    const auto& entries = or_it->value("models", ordered_json::object());
    provider.models.reserve(entries.size());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        provider.models.push_back(model_from_catalog(it.key(), it.value()));
    }
    static constexpr std::array<const char*, 8> kNoZenAllow = {nullptr};
    provider.models =
        filter_verified(std::move(provider.models), kNoZenAllow,
                        kVerifiedOpenRouterFree, true);
    return provider;
}

// Official OpenCode Zen free catalog (https://opencode.ai/docs/zen/).
// IDs match GET https://opencode.ai/zen/v1/models. north-mini-code-free is
// not a Zen model (401 ModelError) — it lives on OpenRouter instead.
static std::vector<ModelInfo> official_opencode_free_models() {
    return {
        make_default_model("X-Preview Frontier (Free)", "x-preview-f-free",
                           1000000, 65536, true, true),
        make_default_model("Big Pickle (Free)", "big-pickle", 200000, 32000,
                           true, true),
        make_default_model("MiMo V2.5 (Free)", "mimo-v2.5-free", 200000, 32000,
                           true, true),
        make_default_model("HY3 (Free)", "hy3-free", 262144, 64000, true, true),
        make_default_model("Laguna S 2.1 (Free)", "laguna-s-2.1-free", 262144,
                           32768, true, true),
        make_default_model("Nemotron 3 Ultra (Free)", "nemotron-3-ultra-free",
                           1000000, 128000, true, true),
        make_default_model("Nemotron 3.5 Lightning (Free)",
                           "nemotron-3.5-lightning-free", 262144, 65536, true,
                           true),
    };
}

static bool opencode_has_api_key() {
    if (const char* key = std::getenv("OPENCODE_API_KEY"); key && *key) return true;
    // A config-file apiKey ({env:...} resolved earlier or literal) counts too;
    // callers pass what they know via parameter where needed.
    return false;
}

// Build the default provider set when no opencode.json exists: OpenCode Zen
// plus OpenRouter when its API key is present (upstream env-activation rule).
static std::vector<ProviderInfo> default_providers() {
    const bool has_zen_key = opencode_has_api_key();
    const bool has_or_key = [] {
        const char* key = std::getenv("OPENROUTER_API_KEY");
        return key && *key;
    }();

    ordered_json catalog;
    const bool have_catalog = load_models_dev_catalog(catalog);

    std::vector<ProviderInfo> providers;

    ProviderInfo zen;
    zen.name = "OpenCode Zen";
    zen.id = "opencode";
    zen.api_url = "https://opencode.ai/zen/v1";
    zen.protocol = "chat_completions";
    zen.headers["User-Agent"] = "opencode/1.18.18";
    zen.headers["x-opencode-client"] = "cli";
    if (!has_zen_key) zen.api_key = "__EMPTY__";  // registry sends "public"
    if (have_catalog) {
        zen.models = zen_models_from_catalog(catalog, has_zen_key);
    }
    if (zen.models.empty()) {
        zen.models = official_opencode_free_models();  // offline fallback
    }
    providers.push_back(std::move(zen));

    if (has_or_key) {
        ProviderInfo openrouter = have_catalog
            ? openrouter_provider_from_catalog(catalog)
            : ProviderInfo{};
        if (!have_catalog || openrouter.models.empty()) {
            openrouter = ProviderInfo{};
            openrouter.name = "OpenRouter";
            openrouter.id = "openrouter";
            openrouter.api_url = "https://openrouter.ai/api/v1";
            openrouter.protocol = "chat_completions";
            openrouter.headers["HTTP-Referer"] = kOpenRouterReferer;
            openrouter.headers["X-Title"] = "opencode";
        }
        providers.push_back(std::move(openrouter));
    }

    LOG_INFO("Default providers: {} (zen {} models from catalog{}, openrouter {} with {} models)",
             providers.size(), providers.front().models.size(),
             has_zen_key ? ", keyed" : ", keyless",
             has_or_key ? "on" : "off",
             providers.size() > 1 ? providers.back().models.size() : 0);
    return providers;
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
        return antigravity_token_expired(token.value("expiry", ""));
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

std::string cursor_model_name(std::string id) {
    std::replace(id.begin(), id.end(), '-', ' ');
    std::ostringstream name;
    bool first = true;
    std::istringstream words(id);
    for (std::string word; words >> word;) {
        if (!first) name << ' ';
        first = false;
        if (word == "gpt") {
            name << "GPT";
        } else if (word == "claude") {
            name << "Claude";
        } else if (word == "xhigh") {
            name << "XHigh";
        } else {
            word.front() =
                static_cast<char>(std::toupper(static_cast<unsigned char>(
                    word.front())));
            name << word;
        }
    }
    return name.str();
}

ModelInfo make_cursor_model(const std::string& id, std::string name = "") {
    ModelInfo model;
    model.name = name.empty() ? cursor_model_name(id) : std::move(name);
    model.id = id;
    model.reasoning = id.find("thinking") != std::string::npos ||
                      id.find("gpt-5") != std::string::npos;
    model.tool_call = true;
    // Cursor catalog does not expose context limits; use a sane default so
    // TUI prune/warn and footer windows are not blind (0).
    model.context_window = 200000;
    model.output_limit = 8192;
    return model;
}

bool is_selected_cursor_family(const std::string& id) {
    return id.find("grok-4.6") != std::string::npos ||
           id.find("grok-4-6") != std::string::npos ||
           id.find("opus-5") != std::string::npos ||
           id.find("opus-5.0") != std::string::npos ||
           id.find("claude-5-opus") != std::string::npos ||
           id.find("claude-opus-5") != std::string::npos;
}

std::string cursor_display_name(const std::string& id,
                                const std::string& discovered_name) {
    auto name = discovered_name.empty() ? cursor_model_name(id)
                                        : discovered_name;
    return name;
}

void add_cursor_model_if_missing(std::vector<ModelInfo>& models,
                                 const std::string& id,
                                 const std::string& name) {
    const auto found = std::find_if(
        models.begin(), models.end(),
        [&id](const ModelInfo& model) { return model.id == id; });
    if (found == models.end()) {
        models.emplace_back(make_cursor_model(id, name));
    }
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
    if (provider.protocol.empty()) {
        provider.protocol = "chat_completions";
    }
    provider.headers["User-Agent"] = "opencode/1.18.18";
    provider.headers["x-opencode-client"] = "cli";
    // north-mini-code-free lives on OpenRouter only; Zen rejects it (401).
    std::erase_if(provider.models, [](const ModelInfo& model) {
        return model.id == "north-mini-code-free";
    });

    // Upstream rule: no OPENCODE_API_KEY => "public" free pool only
    // (cost.input == 0); with a key the whole Zen catalog is available.
    const bool has_key =
        !provider.api_key.empty() && provider.api_key != "__EMPTY__";
    ordered_json catalog;
    if (load_models_dev_catalog(catalog)) {
        for (auto& model :
             zen_models_from_catalog(catalog, has_key || opencode_has_api_key())) {
            add_opencode_model_if_missing(provider.models, std::move(model));
        }
        return;
    }
    LOG_WARN("models.dev catalog unavailable; using built-in free list");
    for (auto& model : official_opencode_free_models()) {
        add_opencode_model_if_missing(provider.models, std::move(model));
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
    if (have_catalog) {
        for (auto& model :
             openrouter_provider_from_catalog(catalog).models) {
            add_opencode_model_if_missing(existing->models, std::move(model));
        }
    }
}

void hydrate_cursor_models(ProviderInfo& provider) {
    const auto token =
        provider.api_key.empty() ? get_cursor_access_token() : provider.api_key;
    if (!token.empty()) {
        qcode::cursor::Options options;
        if (!provider.api_url.empty()) {
            options.agent_base_url = provider.api_url;
        }
        const auto available = qcode::cursor::list_models(token, options);
        if (!available.empty()) {
            std::vector<ModelInfo> live_models;
            live_models.reserve(available.size());
            for (const auto& model : available) {
                if (!is_selected_cursor_family(model.id)) continue;
                add_cursor_model_if_missing(
                    live_models, model.id,
                    cursor_display_name(model.id, model.name));
            }
            provider.models = std::move(live_models);
            LOG_INFO("Loaded {} live Cursor models", provider.models.size());
            return;
        }
        LOG_WARN("Cursor model discovery failed; using configured fallbacks");
    }

    // Keep configured entries and seed current IDs when the catalog endpoint is
    // temporarily unavailable. Cursor will still enforce account entitlement.
    std::erase_if(provider.models, [](const ModelInfo& model) {
        return !is_selected_cursor_family(model.id);
    });
    add_cursor_model_if_missing(provider.models, "cursor-grok-4.6",
                                "Cursor Grok 4.6");
    add_cursor_model_if_missing(provider.models, "claude-opus-5-high",
                                "Claude Opus 5");
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
                if (package == "@ai-sdk/openai") {
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
        }
        maybe_hydrate_openrouter(loaded);
    } catch (const std::exception& e) {
        LOG_ERROR("Config parse error: {}", e.what());
    }
    return loaded;
}

} // namespace qcode

