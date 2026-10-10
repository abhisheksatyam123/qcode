#include <qcode/config/config.h>
#include <qcode/core/ssl_config.h>
#include <qcode/transform/provider_transform.h>
#include "providers/anthropic/anthropic_oauth.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include <qcode/core/logger.h>

namespace qcode {

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

static constexpr const char* kOpenRouterReferer = "https://opencode.ai/";

// GET https://opencode.ai/zen/v1/models no longer accepts these; they 401
// as ModelError. Drop from the picker even if an old opencode.json lists them.
static bool zen_id_is_retired(const std::string& id) {
    return id == "hy3-free" || id == "hy3-preview-free" ||
           id == "x-preview-f-free" || id == "north-mini-code-free" ||
           id == "kimi-k2.5-free";
}

static void header_if_absent(std::map<std::string, std::string>& headers,
                             const std::string& key,
                             const std::string& value) {
    if (!headers.contains(key)) headers.emplace(key, value);
}

// Well-known endpoints/headers only when opencode.json omitted them.
static void fill_known_provider_defaults(ProviderInfo& provider) {
    if (provider.id == "opencode") {
        if (provider.api_url.empty()) {
            provider.api_url = "https://opencode.ai/zen/v1";
        }
        header_if_absent(provider.headers, "User-Agent", "opencode/1.18.18");
        header_if_absent(provider.headers, "x-opencode-client", "cli");
    } else if (provider.id == "openrouter") {
        if (provider.api_url.empty()) {
            provider.api_url = "https://openrouter.ai/api/v1";
        }
        header_if_absent(provider.headers, "HTTP-Referer", kOpenRouterReferer);
        header_if_absent(provider.headers, "X-Title", "opencode");
    } else if (provider.id == "cursor") {
        if (provider.api_url.empty()) {
            provider.api_url = "https://agentn.global.api5.cursor.sh";
        }
    } else if (provider.id == "antigravity") {
        if (provider.api_url.empty()) {
            provider.api_url =
                "https://daily-cloudcode-pa.sandbox.googleapis.com/v1internal";
        }
    } else if (provider.id == "anthropic") {
        if (provider.api_url.empty()) {
            provider.api_url = "https://api.anthropic.com";
        }
        if (provider.protocol.empty()) {
            provider.protocol = "messages";
        }
    } else if (provider.id == "openai") {
        if (provider.api_url.empty()) {
            provider.api_url = "https://api.openai.com/v1";
        }
        if (provider.protocol.empty()) {
            provider.protocol = "chat_completions";
        }
    }
}

// Protocol/reasoning inference only when JSON left the field empty.
static void finalize_configured_models(const ProviderInfo& provider,
                                       std::vector<ModelInfo>& models) {
    for (auto& model : models) {
        ProviderTransform::apply_reasoning_defaults(model);
        if (!model.protocol.empty()) continue;
        if (provider.id == "opencode") {
            model.protocol = ProviderTransform::zen_api_protocol(model.id);
        } else if (provider.id == "anthropic") {
            model.protocol = "messages";
        } else if (!provider.protocol.empty()) {
            model.protocol = provider.protocol;
        } else {
            model.protocol = "chat_completions";
        }
    }
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

std::string get_anthropic_token(bool force_refresh) {
    if (const char* key = std::getenv("ANTHROPIC_API_KEY");
        key != nullptr && *key != '\0') {
        return key;
    }
    const auto creds = anthropic::read_claude_credentials();
    if (!creds || creds->access_token.empty()) {
        return "";
    }
    if (!force_refresh && !anthropic::is_claude_token_expired(creds->expires_at_ms)) {
        return creds->access_token;
    }

    static std::mutex refresh_mutex;
    std::lock_guard<std::mutex> lock(refresh_mutex);

    // Cross-process recovery: check if external process refreshed token on disk
    const auto fresh_disk = anthropic::read_claude_credentials();
    if (fresh_disk && fresh_disk->access_token != creds->access_token &&
        !anthropic::is_claude_token_expired(fresh_disk->expires_at_ms)) {
        return fresh_disk->access_token;
    }

    if (creds->refresh_token.empty()) {
        return creds->access_token;
    }

    const auto refreshed = anthropic::refresh_claude_token(creds->refresh_token);
    if (refreshed) {
        return refreshed->access_token;
    }
    return creds->access_token;
}

bool is_anthropic_oauth() {
    if (const char* key = std::getenv("ANTHROPIC_API_KEY");
        key != nullptr && *key != '\0') {
        return std::string_view(key).starts_with("sk-ant-oat");
    }
    const auto creds = anthropic::read_claude_credentials();
    return creds.has_value() && !creds->access_token.empty();
}

bool anthropic_token_needs_refresh() {
    if (const char* key = std::getenv("ANTHROPIC_API_KEY");
        key != nullptr && *key != '\0') {
        return false;
    }
    const auto creds = anthropic::read_claude_credentials();
    if (!creds || creds->access_token.empty()) {
        return true;
    }
    return anthropic::is_claude_token_expired(creds->expires_at_ms);
}

bool login_anthropic_oauth(std::string* out_error) {
    return anthropic::login_anthropic_oauth(out_error);
}

std::string config_path() {
    if (const char* configured = std::getenv("OPENCODE_CONFIG");
        configured != nullptr && *configured != '\0') {
        return configured;
    }
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME");
        xdg != nullptr && *xdg != '\0') {
        return (std::filesystem::path(xdg) / "opencode/opencode.json").string();
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return (std::filesystem::path(home) / ".config/opencode/opencode.json")
            .string();
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

static void remap_cursor_picker_ids(ProviderInfo& provider) {
    std::vector<ModelInfo> models;
    std::unordered_set<std::string> seen;
    for (const auto& configured : provider.models) {
        if (configured.id.empty()) continue;
        const auto picker_id = ProviderTransform::cursor_picker_id(configured.id);
        if (!seen.insert(picker_id).second) continue;
        ModelInfo model = configured;
        model.id = picker_id;
        if (model.name.empty() || model.name == configured.id) {
            model.name = (picker_id == "auto") ? "Auto" : picker_id;
        }
        models.push_back(std::move(model));
    }
    provider.models = std::move(models);
}

// Fill omitted endpoint/headers only. JSON models, protocol, and headers win.
static void apply_provider_runtime_defaults(ProviderInfo& provider) {
    fill_known_provider_defaults(provider);
    if (provider.id == "opencode") {
        std::erase_if(provider.models, [](const ModelInfo& model) {
            return zen_id_is_retired(model.id);
        });
    } else if (provider.id == "cursor") {
        remap_cursor_picker_ids(provider);
    }
    if (provider.id == "openrouter" && provider.api_key.empty()) {
        if (const char* key = std::getenv("OPENROUTER_API_KEY");
            key != nullptr && *key != '\0') {
            provider.api_key = key;
        }
    }
    if (provider.id == "cursor" && provider.api_key.empty()) {
        const auto token = get_cursor_access_token();
        if (!token.empty()) {
            provider.api_key = token;
        }
    }
    if (provider.id == "antigravity" && provider.api_key.empty()) {
        const auto token = get_antigravity_token();
        if (!token.empty()) {
            provider.api_key = token;
        }
    }
    if (provider.id == "anthropic" && provider.api_key.empty()) {
        const auto token = get_anthropic_token();
        if (!token.empty()) {
            provider.api_key = token;
        }
    }
    if (provider.id == "openai" && provider.api_key.empty()) {
        if (const char* key = std::getenv("OPENAI_API_KEY"); key && *key != '\0') {
            provider.api_key = key;
        }
    }
    finalize_configured_models(provider, provider.models);
    LOG_INFO("{} from config: {} models api={}", provider.id,
             provider.models.size(),
             provider.api_url.empty() ? "(none)" : provider.api_url);
}

}  // namespace

static bool is_supported_provider(std::string_view /*id*/) {
    return true;
}

namespace {
std::mutex g_config_warnings_mutex;
std::vector<std::string> g_config_warnings;

// A value of the wrong type: logged and kept for config_warnings().
void warn_mistyped(const std::string& where, const char* key, const char* expected,
                   const nlohmann::ordered_json& got) {
    std::string message = where + "." + key + " should be " + expected + " (got " +
                          got.type_name() + "); ignored";
    LOG_WARN("opencode.json: {}", message);
    std::lock_guard<std::mutex> lock(g_config_warnings_mutex);
    g_config_warnings.push_back(std::move(message));
}

// Type-tolerant readers: a mistyped field must not abort the whole config.
// With `where` (e.g. "anthropic/claude-sonnet-5-5.limit") a present value of
// the wrong type is reported; absent and null values are silent.
std::string json_string(const nlohmann::ordered_json& obj, const char* key,
                        const std::string& where = {}, std::string fallback = {}) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    if (it->is_string()) return it->get<std::string>();
    if (!where.empty()) warn_mistyped(where, key, "a string", *it);
    return fallback;
}
int json_int(const nlohmann::ordered_json& obj, const char* key,
             const std::string& where = {}) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return 0;
    if (it->is_number()) return static_cast<int>(it->get<double>());
    if (!where.empty()) warn_mistyped(where, key, "a number", *it);
    return 0;
}
double json_number(const nlohmann::ordered_json& obj, const char* key,
                   const std::string& where = {}) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return 0.0;
    if (it->is_number()) return it->get<double>();
    if (!where.empty()) warn_mistyped(where, key, "a number", *it);
    return 0.0;
}
bool json_bool(const nlohmann::ordered_json& obj, const char* key, bool fallback,
               const std::string& where = {}) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    if (!where.empty()) warn_mistyped(where, key, "true or false", *it);
    return fallback;
}
// An object-valued key ("limit", "cost", ...); reports any other type.
bool json_object_at(const nlohmann::ordered_json& obj, const char* key,
                    const std::string& where) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return false;
    if (it->is_object()) return true;
    warn_mistyped(where, key, "an object", *it);
    return false;
}
}  // namespace

std::vector<std::string> config_warnings() {
    std::lock_guard<std::mutex> lock(g_config_warnings_mutex);
    return g_config_warnings;
}

std::vector<ProviderInfo> load_providers_from_config() {
    std::vector<ProviderInfo> loaded;
    {
        std::lock_guard<std::mutex> lock(g_config_warnings_mutex);
        g_config_warnings.clear();
    }
    std::string path = config_path();
    LOG_DEBUG("load_providers: path={}", path);
    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config not found at {} — add providers in that opencode.json",
                 path);
        return loaded;
    }
    try {
        std::ifstream file(path);
        ordered_json config = ordered_json::parse(file);
        std::unordered_set<std::string> disabled_providers;
        if (config.contains("disabled_providers") && config["disabled_providers"].is_array()) {
            for (const auto& item : config["disabled_providers"]) {
                if (item.is_string()) disabled_providers.insert(item.get<std::string>());
            }
        }
        std::optional<std::unordered_set<std::string>> enabled_providers;
        if (config.contains("enabled_providers") && config["enabled_providers"].is_array()) {
            std::unordered_set<std::string> set;
            for (const auto& item : config["enabled_providers"]) {
                if (item.is_string()) set.insert(item.get<std::string>());
            }
            enabled_providers = std::move(set);
        }

        // Top-level "model_defaults" apply to every provider's models.
        const ordered_json global_model_defaults =
            config.contains("model_defaults") && config["model_defaults"].is_object()
                ? config["model_defaults"]
                : ordered_json::object();
        if (config.contains("provider")) {
            for (auto& [prov_id, prov_data] : config["provider"].items()) {
                if (enabled_providers.has_value() && !enabled_providers->contains(prov_id)) {
                    continue;
                }
                if (disabled_providers.contains(prov_id)) {
                    continue;
                }
                if (!is_supported_provider(prov_id)) {
                    continue;
                }
                if (!prov_data.is_object()) {
                    warn_mistyped("provider", prov_id.c_str(), "an object", prov_data);
                    continue;
                }
                ProviderInfo prov;
                prov.id = prov_id;
                prov.name = json_string(prov_data, "name", prov_id, prov_id);
                const auto options = prov_data.value(
                    "options", ordered_json::object());
                prov.api_url = normalize_api_url(
                    resolve_config_value(options.value("baseURL", ordered_json{})));
                if (prov.api_url.empty()) {
                    prov.api_url = normalize_api_url(json_string(prov_data, "api", prov_id));
                }
                prov.api_key = resolve_config_value(
                    options.value("apiKey", ordered_json{}));
                if (options.contains("headers") && options["headers"].is_object()) {
                    for (const auto& [name, value] : options["headers"].items()) {
                        const auto resolved = resolve_config_value(value);
                        if (!resolved.empty()) prov.headers.emplace(name, resolved);
                    }
                }
                const auto package = json_string(prov_data, "npm", prov_id);
                if (options.contains("protocol") && options["protocol"].is_string()) {
                    prov.protocol = options["protocol"].get<std::string>();
                } else if (prov_data.contains("protocol") &&
                           prov_data["protocol"].is_string()) {
                    prov.protocol = prov_data["protocol"].get<std::string>();
                } else if (package == "@ai-sdk/openai") {
                    prov.protocol = "responses";
                } else {
                    prov.protocol = "chat_completions";
                }
                prov.project_id = resolve_config_value(
                    options.value("project", ordered_json{}));
                if (prov_data.contains("models") && !prov_data["models"].is_object()) {
                    warn_mistyped(prov_id, "models", "an object", prov_data["models"]);
                } else if (prov_data.contains("models")) {
                    // "model_defaults" (thinking, variants, limit, max_tokens,
                    // cost, ...) are merged under every model, top-level first,
                    // then the provider's (RFC 7386 merge patch: model keys win,
                    // a null value removes an inherited default).
                    ordered_json model_defaults = global_model_defaults;
                    if (prov_data.contains("model_defaults") &&
                        prov_data["model_defaults"].is_object()) {
                        model_defaults.merge_patch(prov_data["model_defaults"]);
                    }
                    for (auto& [model_id, raw_model_data] : prov_data["models"].items()) {
                        ordered_json model_data = model_defaults;
                        if (raw_model_data.is_object()) {
                            model_data.merge_patch(raw_model_data);
                        }
                        // Report a mistyped value by its path, e.g.
                        // "anthropic/claude-sonnet-5-5.limit.output".
                        const std::string where = prov_id + "/" + model_id;
                        ModelInfo model;
                        model.name = json_string(model_data, "name", where, model_id);
                        model.id = model_id;
                        if (json_object_at(model_data, "limit", where)) {
                            const auto& limit = model_data["limit"];
                            model.context_window = json_int(limit, "context", where + ".limit");
                            model.output_limit = json_int(limit, "output", where + ".limit");
                        }
                        model.max_tokens = json_int(model_data, "max_tokens", where);
                        if (json_object_at(model_data, "cost", where)) {
                            const auto& cost = model_data["cost"];
                            const std::string at = where + ".cost";
                            model.cost_configured = true;
                            model.input_cost = json_number(cost, "input", at);
                            model.output_cost = json_number(cost, "output", at);
                            model.cache_read_cost = json_number(cost, "cache_read", at);
                            model.cache_write_cost = json_number(cost, "cache_write", at);
                        }
                        if (json_object_at(model_data, "thinking", where)) {
                            const auto& thinking = model_data["thinking"];
                            const std::string at = where + ".thinking";
                            model.thinking_type = json_string(thinking, "type", at);
                            model.thinking_display = json_string(thinking, "display", at);
                            model.thinking_allow_off = json_bool(thinking, "allow_off", true, at);
                        }
                        // Sampling: "temperature" (number, or false = send
                        // none) and "top_p", at the model or under "options".
                        auto read_sampling = [&](const nlohmann::ordered_json& obj,
                                                 const std::string& at) {
                            if (const auto it = obj.find("temperature");
                                it != obj.end() && !it->is_null()) {
                                if (it->is_boolean()) {
                                    model.temperature_supported = it->get<bool>();
                                } else if (it->is_number()) {
                                    model.temperature = it->get<double>();
                                } else {
                                    warn_mistyped(at, "temperature", "a number or false", *it);
                                }
                            }
                            if (const auto it = obj.find("top_p");
                                it != obj.end() && !it->is_null()) {
                                if (it->is_number()) {
                                    model.top_p = it->get<double>();
                                } else {
                                    warn_mistyped(at, "top_p", "a number", *it);
                                }
                            }
                        };
                        read_sampling(model_data, where);
                        if (json_object_at(model_data, "options", where)) {
                            read_sampling(model_data["options"], where + ".options");
                        }
                        if (json_object_at(model_data, "compaction", where)) {
                            const auto& compaction = model_data["compaction"];
                            const std::string at = where + ".compaction";
                            model.auto_compact = json_bool(compaction, "auto", true, at);
                            model.compact_threshold =
                                json_int(compaction, "threshold_tokens", at);
                        }
                        model.tool_call = json_bool(model_data, "tool_call", false, where);
                        model.vision = json_bool(model_data, "vision", false, where);
                        if (model_data.contains("protocol") &&
                            model_data["protocol"].is_string()) {
                            model.protocol = model_data["protocol"].get<std::string>();
                        }
                        if (model_data.contains("reasoning")) {
                            if (model_data["reasoning"].is_boolean()) {
                                model.reasoning = model_data["reasoning"].get<bool>();
                            } else if (model_data["reasoning"].is_object()) {
                                model.reasoning = true;
                                const auto& reasoning = model_data["reasoning"];
                                if (reasoning.contains("efforts") &&
                                    reasoning["efforts"].is_array()) {
                                    for (const auto& effort : reasoning["efforts"]) {
                                        if (effort.is_string()) {
                                            model.reasoning_efforts.push_back(
                                                effort.get<std::string>());
                                        }
                                    }
                                }
                                model.reasoning_default =
                                    json_string(reasoning, "default", where + ".reasoning");
                                model.reasoning_field =
                                    json_string(reasoning, "field", where + ".reasoning");
                                model.reasoning_summary =
                                    json_string(reasoning, "summary",
                                                where + ".reasoning");
                            }
                        }
                        auto append_efforts = [&model](const ordered_json& value) {
                            if (!value.is_array()) return;
                            for (const auto& effort : value) {
                                if (effort.is_string()) {
                                    model.reasoning_efforts.push_back(
                                        effort.get<std::string>());
                                }
                            }
                        };
                        // An explicit "reasoning": false opts out of inherited
                        // variants (model_defaults) instead of contradicting it.
                        const bool reasoning_off =
                            model_data.contains("reasoning") &&
                            model_data["reasoning"].is_boolean() &&
                            !model_data["reasoning"].get<bool>();
                        const bool variant_objects =
                            !reasoning_off && model_data.contains("variants") &&
                            model_data["variants"].is_object();
                        if (variant_objects) {
                            // The variants object is authoritative: its keys (in
                            // file order) are the picker list. Older effort
                            // lists are ignored so merged defaults cannot
                            // reorder or resurrect removed variants.
                            model.reasoning_efforts.clear();
                            for (const auto& [variant_id, spec] :
                                 model_data["variants"].items()) {
                                if (variant_id.empty() || variant_id == "off") continue;
                                if (spec.is_object() &&
                                    spec.contains("disabled") &&
                                    spec["disabled"].is_boolean() &&
                                    spec["disabled"].get<bool>()) {
                                    continue;
                                }
                                VariantInfo variant;
                                variant.id = variant_id;
                                if (spec.is_object()) {
                                    const std::string at = where + ".variants." + variant_id;
                                    variant.label = json_string(spec, "label", at);
                                    variant.description = json_string(spec, "description", at);
                                    variant.effort = json_string(spec, "effort", at);
                                    variant.max_tokens = json_int(spec, "max_tokens", at);
                                    variant.budget_tokens = json_int(spec, "budget_tokens", at);
                                    variant.prompt = json_string(spec, "prompt", at);
                                }
                                model.reasoning_efforts.push_back(variant_id);
                                model.variants.push_back(std::move(variant));
                            }
                            model.reasoning = model.reasoning || !model.variants.empty();
                        } else {
                            if (model_data.contains("reasoning_efforts")) {
                                append_efforts(model_data["reasoning_efforts"]);
                            }
                            if (model_data.contains("variants")) {
                                append_efforts(model_data["variants"]);
                            }
                        }
                        {
                            // Drop duplicate ids (several sources may list one).
                            std::vector<std::string> unique;
                            for (auto& effort : model.reasoning_efforts) {
                                if (std::find(unique.begin(), unique.end(), effort) ==
                                    unique.end()) {
                                    unique.push_back(std::move(effort));
                                }
                            }
                            model.reasoning_efforts = std::move(unique);
                        }
                        if (model.reasoning_default.empty()) {
                            model.reasoning_default =
                                json_string(model_data, "reasoning_default", where);
                        }
                        if (model.reasoning_default.empty()) {
                            model.reasoning_default = json_string(model_data, "variant", where);
                        }
                        if (model.reasoning_field.empty() &&
                            model_data.contains("reasoning_field") &&
                            model_data["reasoning_field"].is_string()) {
                            model.reasoning_field =
                                model_data["reasoning_field"].get<std::string>();
                        }
                        ProviderTransform::apply_reasoning_defaults(model);
                        prov.models.push_back(std::move(model));
                    }
                }
                loaded.push_back(std::move(prov));
            }
        }


        for (auto& provider : loaded) {
            apply_provider_runtime_defaults(provider);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Config parse error: {}", e.what());
        std::lock_guard<std::mutex> lock(g_config_warnings_mutex);
        g_config_warnings.push_back(std::string("parse error: ") + e.what());
    }
    return loaded;
}

bool is_provider_authenticated(const ProviderInfo& provider) {
    if (!provider.api_key.empty()) return true;
    if (provider.id == "opencode" || provider.name == "OpenCode Zen") return true;
    if (provider.id == "openrouter" || provider.name == "OpenRouter") {
        const char* key = std::getenv("OPENROUTER_API_KEY");
        if (key && *key != '\0') return true;
    }
    if (provider.id == "antigravity" || provider.name == "Antigravity") {
        if (!get_antigravity_token().empty()) return true;
    }
    if (provider.id == "cursor" || provider.name == "Cursor") {
        if (!get_cursor_access_token().empty()) return true;
    }
    if (provider.id == "anthropic" || provider.name == "Anthropic") {
        const char* key = std::getenv("ANTHROPIC_API_KEY");
        if (key && *key != '\0') return true;
        if (!get_anthropic_token().empty()) return true;
    }
    if (provider.id == "openai" || provider.name == "OpenAI") {
        const char* key = std::getenv("OPENAI_API_KEY");
        if (key && *key != '\0') return true;
    }
    // Unit tests / mocks where api_url is empty
    if (provider.api_url.empty() && provider.api_key.empty()) return true;
    return false;
}

bool is_model_working(const ProviderInfo& provider, const ModelInfo& model) {
    if (model.id.empty()) return false;
    if (provider.id == "opencode" && zen_id_is_retired(model.id)) return false;
    // Delegation runs a tool loop: a model configured without tool calls
    // ("tool_call": false or absent) cannot serve it.
    if (!model.tool_call) return false;
    return true;
}

std::vector<ProviderInfo> filter_working_providers(const std::vector<ProviderInfo>& providers) {
    std::vector<ProviderInfo> result;
    for (const auto& prov : providers) {
        if (!is_provider_authenticated(prov)) continue;
        ProviderInfo filtered = prov;
        filtered.models.clear();
        for (const auto& model : prov.models) {
            if (is_model_working(prov, model)) {
                filtered.models.push_back(model);
            }
        }
        if (!filtered.models.empty()) {
            result.push_back(std::move(filtered));
        }
    }
    return result;
}

std::string format_provider_catalog_for_prompt(
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id,
    std::string_view current_model_id) {
    const auto working_providers = filter_working_providers(providers);
    const auto& catalog = working_providers.empty() ? providers : working_providers;
    if (catalog.empty()) return "";

    const bool has_self = !current_provider_id.empty() && !current_model_id.empty();
    std::string out = "### Models for subagents (from opencode.json)\n\n";
    out += "Pick the provider:model that fits each task (capability, speed, cost) and pass "
           "it as `model` to `task`. Omit `model` to use your own";
    if (has_self) {
        out += " (`" + std::string(current_provider_id) + ":" + std::string(current_model_id) + "`)";
    }
    out += ". Prices are USD per 1M input/output tokens.\n\n";
    char price[64];
    for (const auto& provider : catalog) {
        std::string lines;
        for (const auto& model : provider.models) {
            if (model.id.empty()) continue;
            lines += "  - `" + model.id + "`";
            if (!model.name.empty() && model.name != model.id) lines += " (" + model.name + ")";
            if (model.reasoning) lines += " [reasoning]";
            if (model.vision) lines += " [vision]";
            if (model.input_cost > 0 || model.output_cost > 0) {
                std::snprintf(price, sizeof(price), " $%g/$%g", model.input_cost, model.output_cost);
                lines += price;
            }
            const bool is_self = has_self &&
                (provider.id == current_provider_id || provider.name == current_provider_id) &&
                (model.id == current_model_id || model.name == current_model_id);
            if (is_self) lines += " [you]";
            lines += "\n";
        }
        if (lines.empty()) continue;
        out += "- **" + provider.id + "** (" + (provider.name.empty() ? provider.id : provider.name) +
               "):\n" + lines;
    }
    return out;
}

std::string format_provider_catalog_for_error(
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id,
    std::string_view current_model_id) {
    (void)current_provider_id;
    (void)current_model_id;
    const auto working_providers = filter_working_providers(providers);
    const auto& catalog = working_providers.empty() ? providers : working_providers;

    std::ostringstream ss;
    int count = 0;
    for (const auto& provider : catalog) {
        for (const auto& model : provider.models) {
            if (model.id.empty()) continue;

            if (count == 0) {
                ss << "Available models from opencode.json (use provider:model):\n";
            }
            ss << "- `" << provider.id << ":" << model.id << "`";
            if (!model.name.empty() && model.name != model.id) {
                ss << " (" << model.name << ")";
            }
            ss << "\n";
            ++count;
            if (count >= 64) {
                ss << "- ...\n";
                return ss.str();
            }
        }
    }
    return ss.str();
}

} // namespace qcode
