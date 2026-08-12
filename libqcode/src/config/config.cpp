#include <qcode/config/config.h>
#include <qcode/http/ssl_config.h>
#include <qcode/providers/cursor.h>

#include <algorithm>
#include <array>
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
    ModelInfo model{std::move(name), std::move(id)};
    model.context_window = context_window;
    model.output_limit = output_limit;
    model.tool_call = tool_call;
    model.reasoning = reasoning;
    return model;
}

static ProviderInfo default_opencode_provider() {
    ProviderInfo provider;
    provider.name = "OpenCode Zen";
    provider.id = "opencode";
    provider.api_url = "https://opencode.ai/zen/v1";
    provider.protocol = "chat_completions";
    provider.models = {
        make_default_model("HY3 (Free)", "hy3-free", 262144, 64000),
        make_default_model("Nemotron 3 Ultra (Free)", "nemotron-3-ultra-free",
                           1000000, 128000, true, true),
        make_default_model("DeepSeek V4 Flash (Free)", "deepseek-v4-flash-free",
                           1000000, 65536),
        make_default_model("Laguna S 2.1 (Free)", "laguna-s-2.1-free", 128000,
                           32768, true, true),
        make_default_model("North Mini Code (Free)", "north-mini-code-free",
                           256000, 64000)};
    return provider;
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
    ModelInfo model{name.empty() ? cursor_model_name(id) : std::move(name), id};
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
    return id.starts_with("cursor-grok-") || id.starts_with("gpt-5.6-") ||
           id.starts_with("claude-fable-5-") || id.starts_with("composer-");
}

std::string cursor_display_name(const std::string& id,
                                const std::string& discovered_name) {
    auto name = discovered_name.empty() ? cursor_model_name(id)
                                        : discovered_name;
    if (id.starts_with("claude-fable-5-") &&
        name.find("Mythos") == std::string::npos) {
        name = "Mythos - " + name;
    }
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
    add_cursor_model_if_missing(provider.models, "cursor-grok-4.5-high",
                                "Cursor Grok 4.5");
    add_cursor_model_if_missing(provider.models, "composer-2.5",
                                "Composer 2.5");
    add_cursor_model_if_missing(provider.models, "composer-2.5-fast",
                                "Composer 2.5 Fast");
    add_cursor_model_if_missing(provider.models, "gpt-5.6-sol-medium",
                                "GPT-5.6 Sol");
    add_cursor_model_if_missing(provider.models, "gpt-5.6-terra-medium",
                                "GPT-5.6 Terra");
    add_cursor_model_if_missing(provider.models, "gpt-5.6-luna-medium",
                                "GPT-5.6 Luna");
    add_cursor_model_if_missing(provider.models, "claude-fable-5-high",
                                "Mythos - Fable 5");
    add_cursor_model_if_missing(provider.models,
                                "claude-fable-5-thinking-high",
                                "Mythos - Fable 5 Thinking High");
}

}  // namespace

std::vector<ProviderInfo> load_providers_from_config() {
    std::vector<ProviderInfo> loaded;
    std::string path = config_path();
    LOG_DEBUG("load_providers: path={}", path);
    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config not found, using defaults");
        return {default_opencode_provider()};
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
                        ModelInfo model{model_data.value("name", model_id), model_id};
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
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Config parse error: {}", e.what());
    }
    return loaded;
}

} // namespace qcode

