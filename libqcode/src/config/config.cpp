#include <qcode/config/config.h>
#include <qcode/session/session_store.h>
#include <qcode/core/ssl_config.h>
#include <qcode/transform/provider_transform.h>

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
            model.name = picker_id;
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
    finalize_configured_models(provider, provider.models);
    LOG_INFO("{} from config: {} models api={}", provider.id,
             provider.models.size(),
             provider.api_url.empty() ? "(none)" : provider.api_url);
}

}  // namespace

static bool is_supported_provider(std::string_view id) {
    return id == "cursor" || id == "opencode" || id == "openrouter" || id == "antigravity";
}

std::vector<ProviderInfo> load_providers_from_config() {
    std::vector<ProviderInfo> loaded;
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
        if (config.contains("provider")) {
            for (auto& [prov_id, prov_data] : config["provider"].items()) {
                if (!is_supported_provider(prov_id)) {
                    continue;
                }
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
                if (prov_data.contains("models")) {
                    for (auto& [model_id, model_data] : prov_data["models"].items()) {
                        ModelInfo model;
                        model.name = model_data.value("name", model_id);
                        model.id = model_id;
                        if (model_data.contains("limit") &&
                            model_data["limit"].is_object()) {
                            const auto& limit = model_data["limit"];
                            model.context_window = limit.value("context", 0);
                            model.output_limit = limit.value("output", 0);
                        }
                        if (model_data.contains("cost") &&
                            model_data["cost"].is_object()) {
                            const auto& cost = model_data["cost"];
                            model.input_cost = cost.value("input", 0.0);
                            model.output_cost = cost.value("output", 0.0);
                        }
                        model.tool_call = model_data.value("tool_call", false);
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
                                    reasoning.value("default", "");
                                model.reasoning_field =
                                    reasoning.value("field", "");
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
                        if (model_data.contains("reasoning_efforts")) {
                            append_efforts(model_data["reasoning_efforts"]);
                        }
                        if (model_data.contains("variants")) {
                            append_efforts(model_data["variants"]);
                        }
                        if (model.reasoning_default.empty()) {
                            model.reasoning_default =
                                model_data.value("reasoning_default", "");
                        }
                        if (model.reasoning_default.empty()) {
                            model.reasoning_default =
                                model_data.value("variant", "");
                        }
                        if (model.reasoning_field.empty() &&
                            model_data.contains("reasoning_field") &&
                            model_data["reasoning_field"].is_string()) {
                            model.reasoning_field =
                                model_data["reasoning_field"].get<std::string>();
                        }
                        if (model.context_window <= 0) {
                            model.context_window =
                                resolve_model_context_window(model_id);
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
    }
    return loaded;
}

std::string format_provider_catalog_for_prompt(const std::vector<ProviderInfo>& providers) {
    if (providers.empty()) return "";

    std::ostringstream ss;
    ss << "### Available Providers & Models (from opencode.json)\n\n"
       << "You have access to the following configured providers and models. "
       << "When delegating subtasks with `task` (`op: \"spawn\"`), you can assign distinct models "
       << "in parallel using `model: \"<provider>/<model_id>\"` or `model: \"<model_id>\"`:\n\n";

    for (const auto& provider : providers) {
        if (provider.models.empty()) continue;
        ss << "- **" << provider.id << "** (" << (provider.name.empty() ? provider.id : provider.name) << "):\n";
        for (const auto& model : provider.models) {
            ss << "  - `" << model.id << "`";
            if (!model.name.empty() && model.name != model.id) {
                ss << " (" << model.name << ")";
            }
            if (model.reasoning) {
                ss << " [reasoning]";
            }
            ss << "\n";
        }
    }

    ss << "\n#### Multi-Agent Parallel Delegation Guidelines\n"
       << "- Run independent subtasks concurrently by specifying `background: true` on `task.spawn`.\n"
       << "- Match tasks to model strengths (e.g. fast models for search/inspection, reasoning models for architecture/refactoring).\n"
       << "- Collect results with `task` operation `result` and `background_task_id`.\n";

    return ss.str();
}

} // namespace qcode
