#include <ai/tui/config.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include <ai/logger.h>

namespace ai {
namespace tui {

using ordered_json = nlohmann::ordered_json;

static std::string normalize_api_url(std::string url) {
    while (url.size() >= 3 &&
           url.compare(url.size() - 3, 3, "/v1") == 0) {
        url.resize(url.size() - 3);
    }
    return url;
}

std::string get_antigravity_token() {
    char* api_key_env = std::getenv("ANTIGRAVITY_API_KEY");
    if (api_key_env && std::string(api_key_env).length() > 0)
        return api_key_env;

    for (const char* py : {"/home/abhi/miniconda3/bin/python", "python3"}) {
        std::string cmd = std::string(py) +
            " -c \"import keyring; print(keyring.get_password("
            "'gemini', 'antigravity'))\" 2>/dev/null";
        std::array<char, 1024> buf;
        std::string output;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) continue;
        while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
            output += buf.data();
        int rc = pclose(pipe);
        if (rc != 0 || output.empty() || output == "None\n") continue;

        try {
            auto parsed = ordered_json::parse(output);
            if (parsed.contains("token") && parsed["token"].contains("access_token"))
                return parsed["token"]["access_token"].get<std::string>();
            if (parsed.contains("access_token"))
                return parsed["access_token"].get<std::string>();
        } catch (...) {}

        auto nl = output.find('\n');
        std::string trimmed = (nl != std::string::npos) ? output.substr(0, nl) : output;
        if (!trimmed.empty()) return trimmed;
    }
    return "";
}

std::vector<ProviderInfo> load_providers_from_config() {
    std::vector<ProviderInfo> loaded;
    std::string path = "/home/abhi/notes/etc/opencode.json";
    ai::logger::log_debug("load_providers: path={}", path);
    if (!std::filesystem::exists(path)) {
        ai::logger::log_warn("Config not found, using defaults");
        return {
            {"OpenCode Zen", "opencode", "https://opencode.ai/zen/v1",
             {{"Nemotron 3 Ultra", "nemotron-3-ultra-free"},
              {"DeepSeek V4 Flash", "deepseek-v4-flash-free"},
              {"North Mini Code", "north-mini-code-free"}}}
        };
    }
    try {
        std::ifstream file(path);
        ordered_json config = ordered_json::parse(file);
        if (config.contains("provider")) {
            for (auto& [prov_id, prov_data] : config["provider"].items()) {
                ProviderInfo prov;
                prov.id = prov_id;
                prov.name = prov_data.value("name", prov_id);
                prov.api_url = normalize_api_url(prov_data.value("api", ""));
                if (prov_data.contains("models")) {
                    for (auto& [model_id, model_data] : prov_data["models"].items()) {
                        ModelInfo model{model_data.value("name", model_id), model_id};
                        prov.models.push_back(model);
                    }
                }
                loaded.push_back(prov);
                ai::logger::log_info("Loaded provider: {} ({}), {} models, api={}",
                                     prov.name, prov.id, prov.models.size(),
                                     prov.api_url.empty() ? "(hardcoded)" : prov.api_url);
            }
        }
    } catch (const std::exception& e) {
        ai::logger::log_error("Config parse error: {}", e.what());
    }
    return loaded;
}

} // namespace tui
} // namespace ai
