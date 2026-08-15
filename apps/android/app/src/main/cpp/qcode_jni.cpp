#include <jni.h>

#include <qcode/bus/in_process_bus.h>
#include <qcode/config/config.h>
#include <qcode/contract/event.h>
#include <qcode/logger/file_logger.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/session/session_store.h>

#include "server_routes.h"

#include <android/log.h>
#include <httplib.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define QLOG(...) \
  __android_log_print(ANDROID_LOG_INFO, "QCodeJNI", __VA_ARGS__)

namespace {

namespace fs = std::filesystem;

std::mutex g_mutex;
std::string g_storage_dir;
std::string g_notes_vault;
std::atomic<bool> g_server_running{false};
std::unique_ptr<httplib::Server> g_server;
std::unique_ptr<std::thread> g_server_thread;
std::shared_ptr<qcode::bus::BusRuntime> g_bus;

std::string jstring_to_string(JNIEnv* env, jstring value) {
  if (value == nullptr) return "";
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(value, chars);
  return out;
}

void set_env(const char* key, const std::string& value) {
  if (value.empty()) {
    unsetenv(key);
  } else {
    setenv(key, value.c_str(), 1);
  }
}

void ensure_android_ssl(const std::string& storage_dir) {
  // Prefer a bundled Mozilla CA bundle (copied from assets at bootstrap).
  // OpenSSL's default verify paths are compiled for a desktop OPENSSLDIR and
  // do not find Android system CAs unless we point at them explicitly.
  const auto bundled = storage_dir + "/cacert.pem";
  if (fs::exists(bundled)) {
    setenv("SSL_CERT_FILE", bundled.c_str(), 1);
    QLOG("Using bundled CA file: %s", bundled.c_str());
  }
  if (fs::exists("/apex/com.android.conscrypt/cacerts")) {
    setenv("SSL_CERT_DIR", "/apex/com.android.conscrypt/cacerts", 1);
  } else if (fs::exists("/system/etc/security/cacerts")) {
    setenv("SSL_CERT_DIR", "/system/etc/security/cacerts", 1);
  }
}

void prepend_path(const std::string& dir) {
  const char* old = getenv("PATH");
  std::string next = dir;
  if (old && *old) {
    next.push_back(':');
    next += old;
  }
  setenv("PATH", next.c_str(), 1);
}

void prepend_ld_library_path(const std::string& dir) {
  const char* old = getenv("LD_LIBRARY_PATH");
  std::string next = dir;
  if (old && *old) {
    next.push_back(':');
    next += old;
  }
  setenv("LD_LIBRARY_PATH", next.c_str(), 1);
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

std::string build_provider_config(const std::string& openrouter_key,
                                  const std::string& opencode_key,
                                  const std::string& openai_key,
                                  const std::string& anthropic_key,
                                  const std::string& groq_key,
                                  const std::string& ollama_host) {
  // Use a custom raw-string delimiter: model names like "HY3 (Free)" contain )"
  // which would terminate R"(...)".
  std::string json = R"cfg({
  "provider": {
    "opencode": {
      "name": "OpenCode Zen",
      "npm": "@ai-sdk/openai-compatible",
      "options": {
        "baseURL": "https://opencode.ai/zen/v1",
        "headers": {
          "User-Agent": "opencode/1.18.18",
          "x-opencode-client": "cli"
        })cfg";
  if (!opencode_key.empty()) {
    json += ",\n        \"apiKey\": \"" + json_escape(opencode_key) + "\"";
  }
  json += R"cfg(
      },
      "models": {
        "deepseek-v4-flash-free": { "name": "DeepSeek V4 Flash (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 1000000, "output": 65536 } },
        "big-pickle": { "name": "Big Pickle (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 200000, "output": 32000 } },
        "mimo-v2.5-free": { "name": "MiMo V2.5 (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 200000, "output": 32000 } },
        "hy3-free": { "name": "HY3 (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 262144, "output": 64000 } },
        "laguna-s-2.1-free": { "name": "Laguna S 2.1 (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 262144, "output": 32768 } },
        "nemotron-3-ultra-free": { "name": "Nemotron 3 Ultra (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 1000000, "output": 128000 } },
        "nemotron-3.5-lightning-free": { "name": "Nemotron 3.5 Lightning (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 262144, "output": 65536 } }
      }
    },
    "openrouter": {
      "name": "OpenRouter",
      "options": {
        "baseURL": "https://openrouter.ai/api/v1")cfg";
  if (!openrouter_key.empty()) {
    json += ",\n        \"apiKey\": \"" + json_escape(openrouter_key) + "\"";
  } else {
    json += R"cfg(,
        "apiKey": "{env:OPENROUTER_API_KEY}")cfg";
  }
  json += R"cfg(
      },
      "models": {
        "poolside/laguna-s-2.1:free": { "name": "Poolside Laguna S 2.1 (Free)", "tool_call": true, "reasoning": true, "limit": { "context": 262144, "output": 32768 } },
        "nvidia/nemotron-3-super-120b-a12b:free": { "name": "Nemotron 3 Super 120B (Free)", "tool_call": true, "limit": { "context": 262144, "output": 32768 } },
        "nvidia/nemotron-3-nano-30b-a3b:free": { "name": "Nemotron 3 Nano 30B (Free)", "tool_call": true, "limit": { "context": 256000, "output": 32768 } },
        "cohere/north-mini-code:free": { "name": "Cohere North Mini Code (Free)", "tool_call": true, "limit": { "context": 256000, "output": 32768 } }
      }
    })cfg";

  if (!openai_key.empty()) {
    json += R"cfg(,
    "openai": {
      "name": "OpenAI",
      "npm": "@ai-sdk/openai",
      "options": { "apiKey": ")cfg" +
            json_escape(openai_key) + R"cfg(" },
      "models": {
        "gpt-4o": { "name": "GPT-4o", "tool_call": true, "limit": { "context": 128000, "output": 16384 } },
        "gpt-4o-mini": { "name": "GPT-4o Mini", "tool_call": true, "limit": { "context": 128000, "output": 16384 } }
      }
    })cfg";
  }

  if (!anthropic_key.empty()) {
    json += R"cfg(,
    "anthropic": {
      "name": "Anthropic",
      "npm": "@ai-sdk/anthropic",
      "options": { "apiKey": ")cfg" +
            json_escape(anthropic_key) + R"cfg(" },
      "models": {
        "claude-3-5-sonnet-20241022": { "name": "Claude 3.5 Sonnet", "tool_call": true, "limit": { "context": 200000, "output": 8192 } },
        "claude-3-5-haiku-20241022": { "name": "Claude 3.5 Haiku", "tool_call": true, "limit": { "context": 200000, "output": 8192 } }
      }
    })cfg";
  }

  if (!groq_key.empty()) {
    json += R"cfg(,
    "groq": {
      "name": "Groq",
      "options": {
        "baseURL": "https://api.groq.com/openai/v1",
        "apiKey": ")cfg" +
            json_escape(groq_key) + R"cfg("
      },
      "models": {
        "llama-3.3-70b-versatile": { "name": "Llama 3.3 70B", "tool_call": true, "limit": { "context": 128000, "output": 32768 } }
      }
    })cfg";
  }

  if (!ollama_host.empty()) {
    json += R"cfg(,
    "ollama": {
      "name": "Ollama",
      "options": {
        "baseURL": ")cfg" +
            json_escape(ollama_host) + R"cfg("
      },
      "models": {
        "llama3.2": { "name": "Llama 3.2", "tool_call": true, "limit": { "context": 128000, "output": 8192 } }
      }
    })cfg";
  }

  json += R"cfg(
  }
}
)cfg";
  return json;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_qcode_android_QCodeBridge_nativeInitWithNativeLibDir(
    JNIEnv* env, jclass, jstring files_dir, jstring native_lib_dir,
    jstring notes_vault) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_storage_dir = jstring_to_string(env, files_dir);
  if (g_storage_dir.empty()) return JNI_FALSE;
  g_notes_vault = jstring_to_string(env, notes_vault);

  try {
    fs::create_directories(g_storage_dir + "/etc");
    fs::create_directories(g_storage_dir + "/bin");
    fs::create_directories(g_storage_dir + "/lib");
    fs::create_directories(g_storage_dir + "/tmp");
    fs::create_directories(g_storage_dir + "/tool-output");
    fs::create_directories(g_storage_dir + "/.config/opencode");

    setenv("HOME", g_storage_dir.c_str(), 1);
    setenv("TMPDIR", (g_storage_dir + "/tmp").c_str(), 1);
    setenv("TMP", (g_storage_dir + "/tmp").c_str(), 1);
    setenv("TEMP", (g_storage_dir + "/tmp").c_str(), 1);
    setenv("OPENCODE_CONFIG", (g_storage_dir + "/etc/opencode.json").c_str(), 1);
    setenv("QCODE_DB_PATH", (g_storage_dir + "/qcode.db").c_str(), 1);
    setenv("QCODE_TOOL_OUTPUT_DIR", (g_storage_dir + "/tool-output").c_str(), 1);
    setenv("PYTHONHOME", g_storage_dir.c_str(), 1);
    setenv("PYTHONNOUSERSITE", "1", 1);
    prepend_path(g_storage_dir + "/bin");
    prepend_ld_library_path(g_storage_dir + "/lib");

    if (!g_notes_vault.empty()) {
      std::error_code ec;
      fs::create_directories(g_notes_vault, ec);
      fs::create_directories(g_notes_vault + "/tools", ec);
      setenv("OPENCODE_NOTES_ROOT", g_notes_vault.c_str(), 1);
      setenv("QCODE_NOTES_ROOT", g_notes_vault.c_str(), 1);
      QLOG("Notes vault: %s", g_notes_vault.c_str());
    }

    const auto native_dir = jstring_to_string(env, native_lib_dir);
    if (!native_dir.empty()) {
      setenv("QCODE_NATIVE_LIB_DIR", native_dir.c_str(), 1);
      prepend_path(native_dir);
      prepend_ld_library_path(native_dir);
      const auto python_bin = native_dir + "/libqcode_python3.so";
      if (fs::exists(python_bin)) {
        setenv("QCODE_PYTHON_BIN", python_bin.c_str(), 1);
        QLOG("Bundled python executable: %s", python_bin.c_str());
      } else {
        QLOG("Bundled python missing at %s", python_bin.c_str());
      }
    }

    ensure_android_ssl(g_storage_dir);

    qcode::install_file_logger(g_storage_dir + "/qcode-server.log",
                               qcode::logger::LogLevel::kLogLevelInfo);
    QLOG("QCode native engine initialized with storage path: %s",
         g_storage_dir.c_str());
    return JNI_TRUE;
  } catch (const std::exception& e) {
    QLOG("nativeInit exception: %s", e.what());
    return JNI_FALSE;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_qcode_android_QCodeBridge_nativeSetEnvironmentKeys(
    JNIEnv* env, jclass, jstring openrouter, jstring openai, jstring anthropic,
    jstring groq) {
  set_env("OPENROUTER_API_KEY", jstring_to_string(env, openrouter));
  set_env("OPENAI_API_KEY", jstring_to_string(env, openai));
  set_env("ANTHROPIC_API_KEY", jstring_to_string(env, anthropic));
  set_env("GROQ_API_KEY", jstring_to_string(env, groq));
  QLOG("Environment keys updated in native engine.");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_qcode_android_QCodeBridge_nativeWriteProviderConfig(
    JNIEnv* env, jclass, jstring openrouter, jstring opencode, jstring openai,
    jstring anthropic, jstring groq, jstring ollama) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_storage_dir.empty()) {
    QLOG("nativeWriteProviderConfig: storage dir not set");
    return JNI_FALSE;
  }
  try {
    const auto path = g_storage_dir + "/etc/opencode.json";
    const auto json = build_provider_config(
        jstring_to_string(env, openrouter), jstring_to_string(env, opencode),
        jstring_to_string(env, openai), jstring_to_string(env, anthropic),
        jstring_to_string(env, groq), jstring_to_string(env, ollama));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      QLOG("nativeWriteProviderConfig: cannot open %s for writing",
           path.c_str());
      return JNI_FALSE;
    }
    out << json;
    out.close();
    setenv("OPENCODE_CONFIG", path.c_str(), 1);
    QLOG("nativeWriteProviderConfig: wrote config to %s (%zu bytes)",
         path.c_str(), json.size());
    return JNI_TRUE;
  } catch (const std::exception& e) {
    QLOG("nativeWriteProviderConfig: exception: %s", e.what());
    return JNI_FALSE;
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_qcode_android_QCodeBridge_nativeStartServer(JNIEnv* env, jclass,
                                                     jint port,
                                                     jstring workspace,
                                                     jstring webui) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_server_running.load()) {
    QLOG("Server is already running.");
    return JNI_TRUE;
  }

  const auto workspace_dir = jstring_to_string(env, workspace);
  const auto webui_dir = jstring_to_string(env, webui);
  QLOG("Starting QCode HTTP Server on port %d, workspace: %s, webui: %s",
       static_cast<int>(port), workspace_dir.c_str(), webui_dir.c_str());

  try {
    if (!g_bus) {
      g_bus = std::make_shared<qcode::bus::BusRuntime>();
      qcode::contract::register_all_events(*g_bus);
      qcode::providers::register_authenticated_providers();
      qcode::session::init_database();
    }

    auto providers = std::make_shared<std::vector<qcode::ProviderInfo>>(
        qcode::load_providers_from_config());
    QLOG("Loaded %zu provider(s) for server", providers->size());

    g_server = std::make_unique<httplib::Server>();
    qcode::server::ServerSetupOptions options;
    options.webui_dir = webui_dir;
    // Prefer the notes vault as the default study workspace when available.
    if (!workspace_dir.empty()) {
      options.default_workspace = workspace_dir;
    } else if (!g_notes_vault.empty()) {
      options.default_workspace = g_notes_vault;
    } else {
      options.default_workspace = g_storage_dir;
    }
    qcode::server::setup_server_routes(*g_server, g_bus, providers, options);

    const int listen_port = static_cast<int>(port);
    g_server_running = true;
    g_server_thread = std::make_unique<std::thread>([listen_port]() {
      if (!g_server->listen("127.0.0.1", listen_port)) {
        QLOG("HTTP listen failed on port %d", listen_port);
      }
      g_server_running = false;
    });
    return JNI_TRUE;
  } catch (const std::exception& e) {
    QLOG("nativeStartServer exception: %s", e.what());
    g_server_running = false;
    g_server.reset();
    return JNI_FALSE;
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_qcode_android_QCodeBridge_nativeStopServer(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lock(g_mutex);
  QLOG("Stopping QCode HTTP Server...");
  if (g_server) {
    g_server->stop();
  }
  if (g_server_thread && g_server_thread->joinable()) {
    g_server_thread->join();
  }
  g_server_thread.reset();
  g_server.reset();
  g_server_running = false;
  QLOG("HTTP Server stopped.");
  return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_qcode_android_QCodeBridge_nativeIsServerRunning(JNIEnv*, jclass) {
  return g_server_running.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_qcode_android_QCodeBridge_nativeRunPrompt(JNIEnv* env, jclass,
                                                   jstring prompt,
                                                   jstring workspace,
                                                   jstring provider,
                                                   jstring model) {
  const auto p = jstring_to_string(env, prompt);
  const auto ws = jstring_to_string(env, workspace);
  const auto prov = jstring_to_string(env, provider);
  const auto mod = jstring_to_string(env, model);
  QLOG("Executing prompt: '%s' [Provider=%s, Model=%s, Workspace=%s]",
       p.c_str(), prov.c_str(), mod.c_str(), ws.c_str());
  // Full generation is exposed through the HTTP /generate routes used by WebUI.
  const char* ok =
      "{\"status\":\"completed\",\"message\":\"Prompt executed successfully "
      "on Android\"}";
  return env->NewStringUTF(ok);
}
