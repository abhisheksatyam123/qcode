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
        // Run self-healing Python keyring lookup & OAuth refresh
        std::string cmd = std::string(py) +
            " -c \"import keyring, json, urllib.request, urllib.parse, datetime\n"
            "try:\n"
            "    raw = keyring.get_password('gemini', 'antigravity')\n"
            "    if raw:\n"
            "        d = json.loads(raw); tok = d.get('token', {}); rt = tok.get('refresh_token'); exp = tok.get('expiry', ''); expired = True\n"
            "        if exp:\n"
            "            try:\n"
            "                t_str = exp.split('+')[0].split('.')[0]\n"
            "                if datetime.datetime.strptime(t_str, '%Y-%m-%dT%H:%M:%S') > datetime.datetime.now() + datetime.timedelta(minutes=5): expired = False\n"
            "            except: pass\n"
            "        if expired and rt:\n"
            "            data = urllib.parse.urlencode({'grant_type': 'refresh_token', 'refresh_token': rt, 'client_id': '1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com', 'client_secret': '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'}).encode('utf-8')\n"
            "            req = urllib.request.Request('https://oauth2.googleapis.com/token', data=data)\n"
            "            with urllib.request.urlopen(req) as resp: res = json.loads(resp.read().decode('utf-8'))\n"
            "            tok['access_token'] = res['access_token']\n"
            "            new_exp = datetime.datetime.now() + datetime.timedelta(seconds=res.get('expires_in', 3600))\n"
            "            tok['expiry'] = new_exp.strftime('%Y-%m-%dT%H:%M:%S') + '+05:30'\n"
            "            d['token'] = tok; keyring.set_password('gemini', 'antigravity', json.dumps(d))\n"
            "            print(res['access_token'])\n"
            "        else:\n"
            "            print(tok.get('access_token', ''))\n"
            "except Exception as e:\n"
            "    print(raw if 'raw' in locals() and raw else '')\" 2>/dev/null";

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
    LOG_DEBUG("load_providers: path={}", path);
    if (!std::filesystem::exists(path)) {
        LOG_WARN("Config not found, using defaults");
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
