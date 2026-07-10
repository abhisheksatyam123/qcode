#include <ai/tui/config.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include <ai/logger.h>

namespace ai {
namespace tui {

using ordered_json = nlohmann::ordered_json;

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

static ProviderInfo default_opencode_provider() {
    ProviderInfo provider;
    provider.name = "OpenCode Zen";
    provider.id = "opencode";
    provider.api_url = "https://opencode.ai/zen/v1";
    provider.protocol = "chat_completions";
    provider.models = {
        {"Nemotron 3 Ultra", "nemotron-3-ultra-free"},
        {"DeepSeek V4 Flash", "deepseek-v4-flash-free"},
        {"North Mini Code", "north-mini-code-free"}};
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

std::string get_antigravity_token() {
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
    if (refresh_token.empty() || client_id == nullptr || *client_id == '\0') {
        return access_token;
    }

    auto body = "grant_type=refresh_token&refresh_token=" +
                form_encode(refresh_token) + "&client_id=" +
                form_encode(client_id);
    if (client_secret != nullptr && *client_secret != '\0') {
        body += "&client_secret=" + form_encode(client_secret);
    }
    httplib::Client client("https://oauth2.googleapis.com");
    client.set_connection_timeout(20);
    const auto response =
        client.Post("/token", body, "application/x-www-form-urlencoded");
    if (!response || response->status != 200) {
        LOG_ERROR("Antigravity OAuth refresh failed with status {}",
                  response ? response->status : 0);
        return access_token;
    }
    try {
        const auto refreshed = ordered_json::parse(response->body);
        const auto fresh_token = refreshed.value("access_token", "");
        if (fresh_token.empty()) return access_token;
        token["access_token"] = fresh_token;
        if (refreshed.contains("expires_in")) {
            token["expires_in"] = refreshed["expires_in"];
        }
        const auto temporary = token_path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            output << document.dump(2);
        }
        std::error_code error;
        std::filesystem::rename(temporary, token_path, error);
        if (error) std::filesystem::remove(temporary);
        return fresh_token;
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to parse Antigravity OAuth response: {}", error.what());
    }
    return access_token;
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
    if (const char* r = std::getenv("OPENCODE_NOTES_ROOT")) return r;
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/notes";
    return "notes";
}

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
    } catch (const std::exception& e) {
        LOG_ERROR("Config parse error: {}", e.what());
    }
    return loaded;
}

} // namespace tui
} // namespace ai

namespace ai {
namespace tui {

void update_modified_files(ChatState& state) {
    state.modified_files->clear();
    std::string command = "git status --porcelain 2>/dev/null";
    std::array<char, 128> buffer;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        std::string line(buffer.data());
        // Git status porcelain output format: "XY path/to/file"
        if (line.length() > 3) {
            std::string path = line.substr(3);
            // Trim newline
            auto nl = path.find('\n');
            if (nl != std::string::npos) {
                path = path.substr(0, nl);
            }
            state.modified_files->push_back(path);
        }
    }
    pclose(pipe);
}

} // namespace tui
} // namespace ai

namespace ai {
namespace tui {

void copy_to_clipboard(const std::string& text) {
    // Try xclip
    FILE* pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (!pipe) {
        // Try xsel
        pipe = popen("xsel --clipboard --input 2>/dev/null", "w");
    }
    if (pipe) {
        fwrite(text.c_str(), 1, text.length(), pipe);
        pclose(pipe);
    }
}

} // namespace tui
} // namespace ai
