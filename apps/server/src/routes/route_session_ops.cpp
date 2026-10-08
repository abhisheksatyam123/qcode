#include "routes_internal.h"
#include "session_runtime.h"
#include "http_utils.h"

#include <qcode/config/config.h>
#include <qcode/config/provider_info.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/providers/registry.h>
#include <qcode/session/session_store.h>
#include <qcode/session/system_prompt.h>
#include <qcode/providers/provider_profile.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/compaction/compaction_request.h>
#include <qcode/core/logger.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace qcode {
namespace server {

void register_session_ops_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list) {

// ── Compact session ──
svr.Post("/session/([^/]+)/compact", [providers_list](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }

    nlohmann::json body;
    try {
        if (!req.body.empty()) {
            body = nlohmann::json::parse(req.body);
        }
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }

    int keep = body.value("keep", 5);

    // Load conversation messages from DB
    auto snapshot = qcode::session::load_session_history_parsed(sid);
    if (snapshot.size() <= 2) {
        res.status = 400;
        res.set_content(R"({"error":"Nothing to compact: conversation is too short."})", "application/json");
        return;
    }

    // Use the session's provider/model, resolved exactly as the generate
    // route does: session rows may store the model's display name (e.g.
    // "MiMo-V2.6-Flash Free"), and sending it on the wire makes Zen answer
    // 401 ModelError ("model not supported") so compaction fails.
    std::string provider_id;
    std::string model_id;
    if (auto s = qcode::session::get_session_info(sid)) {
        provider_id = s->provider;
        model_id = s->model;
    }
    const auto resolved = resolve_provider_model(*providers_list, provider_id, model_id, res);
    if (!resolved.provider) return;
    const auto& sel = *resolved.provider;
    const qcode::ModelInfo* selected_model = resolved.model;
    if (selected_model) model_id = selected_model->id;

    qcode::providers::register_authenticated_providers();
    qcode::providers::ProviderOptions provider_options;
    provider_options.base_url = sel.api_url;
    provider_options.api_key = sel.api_key;
    provider_options.headers = sel.headers;
    provider_options.protocol =
        (selected_model != nullptr && !selected_model->protocol.empty())
            ? selected_model->protocol
            : sel.protocol;
    provider_options.project_id = sel.project_id;
    // Wire model id, exactly as the generate route resolves it — a raw
    // config id can silently route elsewhere (and break the cache prefix).
    const auto call = qcode::prepare_provider_call(provider_options, sel.id,
                                                   model_id);
    const std::string wire_model = call.wire_model_id;
    auto resolution = qcode::providers::ProviderRegistry::instance().resolve(
        sel.id, provider_options);
    if (!resolution.ok()) {
        res.status = 500;
        nlohmann::json err_j; err_j["error"] = "Compaction failed: " + resolution.error; res.set_content(err_j.dump(), "application/json");
        return;
    }

    qcode::Client client = std::move(resolution.client);

    // Mirror the generate route's turn context so the summarizer request
    // replays the last routed request's cacheable prefix byte-for-byte
    // (system prompt + tool schemas + history), with the directive appended
    // as the final user message. See compaction_request.h (dsh rule).
    // Same study-mode resolution and default system prompt as the generate
    // route (route_session.cpp), so the replayed system prefix matches.
    bool study_mode =
#ifdef __ANDROID__
        true;
#else
        false;
#endif
    if (body.contains("study_mode") && body["study_mode"].is_boolean()) {
        study_mode = body["study_mode"].get<bool>();
    }
    const std::string mode_str = body.value("mode", "");
    if (mode_str == "study") study_mode = true;
    if (mode_str == "code") study_mode = false;

    std::string persona_name = body.value("persona", "");
    if (persona_name.empty()) {
        persona_name = qcode::session::get_session_persona(sid);
    }
    std::string ws = qcode::session::get_session_workspace(sid);

    std::string system_prompt;
    if (body.contains("system_prompt") && body["system_prompt"].is_string() &&
        !body["system_prompt"].get<std::string>().empty()) {
        system_prompt = body["system_prompt"].get<std::string>();
    } else if (!persona_name.empty()) {
        std::string persona_prompt = load_persona_prompt(persona_name, ws);
        if (!persona_prompt.empty()) {
            bool vision_supported = (selected_model != nullptr && selected_model->vision);
            system_prompt = qcode::SystemPrompt::build(persona_prompt, qcode::ToolConfig::orchestrator(vision_supported));
        }
    }
    if (system_prompt.empty()) {
        if (study_mode) {
            system_prompt =
                qcode::SystemPrompt::build(qcode::SystemPrompt::study_identity());
        } else {
            system_prompt = qcode::SystemPrompt::build_default(
                qcode::ToolConfig::orchestrator(
                    selected_model != nullptr && selected_model->vision));
        }
    }

    std::string agent_mode = body.value("agent_mode", "");
    if (agent_mode.empty()) {
        auto modes = qcode::session::get_session_modes(sid);
        agent_mode = modes.first;
    }
    if (agent_mode.empty()) {
        agent_mode = (sid.rfind("ses_", 0) == 0) ? "subagent" : "orchestrator";
    }

    qcode::compaction::CacheReplayInput replay;
    replay.system_prompt = system_prompt;
    replay.agent_mode = agent_mode;
    replay.is_subagent = qcode::session::is_child_session(sid);
    replay.enable_tools = true;  // generate route hardcodes enable_tools=true
    replay.vision_supported =
        (selected_model != nullptr && selected_model->vision);
    replay.providers = providers_list.get();
    replay.session_id = sid;
    qcode::GenerateOptions opts = qcode::compaction::build_cache_replay_request(
        replay, wire_model, sel.id, snapshot);

    qcode::GenerateResult gen_res = client.generate_text(opts);
    // Observable evidence of the warm-prefix replay: a cache hit here means
    // the summarizer call reused the last routed request's prefix.
    LOG_INFO("Compaction summarizer: model={} cached_prompt_tokens={} prompt_tokens={} completion_tokens={}",
             opts.model, gen_res.usage.cached_prompt_tokens,
             gen_res.usage.prompt_tokens, gen_res.usage.completion_tokens);
    if (!gen_res.is_success() || (gen_res.error && !gen_res.error->empty())) {
        res.status = 500;
        std::string err = gen_res.error && !gen_res.error->empty() ? *gen_res.error : gen_res.error_message();
        nlohmann::json err_j; err_j["error"] = "Compaction failed: " + err; res.set_content(err_j.dump(), "application/json");
        return;
    }

    std::string summary = gen_res.text;
    if (summary.empty()) {
        res.status = 500;
        res.set_content(R"({"error":"Compaction failed: empty summary from model."})", "application/json");
        return;
    }

    // Project data stays in the workspace root, not the notes vault.
    std::string todo_path = qcode::compaction::write_handoff(sid, summary);
    const bool wrote = !todo_path.empty();

    std::string summary_body =
        "This conversation was compacted into a handoff packet" +
        (wrote ? (" written to: " + todo_path) : "") + ".\n\n" + summary;

    std::string note = "Conversation compacted: " + std::to_string(snapshot.size()) +
                       " messages -> handoff packet";
    note += wrote ? ("\nTodo file: " + todo_path) : " (todo file write failed)";

    // Replace history with the handoff summary + recent keep-tail messages.
    // Older turns are dropped so the message list and model context reset.
    qcode::Messages new_messages;
    new_messages.push_back(qcode::Message::system(note));
    new_messages.push_back(qcode::Message::user(summary_body));

    auto is_prior_compaction_msg = [](const qcode::Message& m) {
        if (m.role == qcode::kMessageRoleSystem) {
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    if (tp->text.find("Conversation compacted:") !=
                        std::string::npos) {
                        return true;
                    }
                }
            }
        }
        if (m.role == qcode::kMessageRoleUser) {
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    if (tp->text.find(
                            "This conversation was compacted into a "
                            "handoff packet") != std::string::npos) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    size_t keep_start = (snapshot.size() > static_cast<size_t>(keep))
                            ? snapshot.size() - static_cast<size_t>(keep)
                            : 0;
    for (size_t i = keep_start; i < snapshot.size(); ++i) {
        if (is_prior_compaction_msg(snapshot[i])) continue;
        new_messages.push_back(snapshot[i]);
    }

    // Overwrite history in SQLite database to persist the compact set
    qcode::session::overwrite_session_history(sid, new_messages);

    nlohmann::json res_j;
    res_j["status"] = "success";
    res_j["summary"] = summary;
    res_j["todo_path"] = todo_path;
    res_j["wrote"] = wrote;
    res_j["original_size"] = snapshot.size();
    res_j["keep"] = keep;
    res.set_content(res_j.dump(), "application/json");
});

// ── Get aggregate session statistics ──
svr.Get("/session/([^/]+)/stats", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    // No live counters: a running turn saves each ToolCall / ToolResult row
    // as it happens and persists its token usage per update, so the stored
    // totals already include it.
    auto st = qcode::session::get_session_stats(sid);
    nlohmann::json j = {
        {"id", st.id}, {"title", st.title}, {"workspace", st.workspace},
        {"provider", st.provider}, {"model", st.model},
        {"created_at", st.created_at}, {"message_count", st.message_count},
        {"user_messages", st.user_messages}, {"assistant_messages", st.assistant_messages},
        {"tool_calls", st.tool_calls}, {"prompt_tokens", st.prompt_tokens},
        {"completion_tokens", st.completion_tokens}, {"total_tokens", st.total_tokens},
        {"total_tool_time_ms", st.total_tool_time_ms}
    };
    res.set_content(j.dump(2), "application/json");
});

}

}  // namespace server
}  // namespace qcode
