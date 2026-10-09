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
#include <qcode/generation/call_usage.h>
#include <qcode/generation/turn_prefix.h>
#include <qcode/core/logger.h>

#include <nlohmann/json.hpp>

#include <chrono>
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
    if (!resolved.provider || !resolved.model) return;
    const auto& sel = *resolved.provider;
    const qcode::ModelInfo* selected_model = resolved.model;
    model_id = selected_model->id;

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
    // Session id too: Zen keys its cache routing on x-opencode-session.
    const auto call = qcode::prepare_provider_call(provider_options, sel.id,
                                                   model_id, sid);
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
    // Same default system prompt as the generate route (route_session.cpp),
    // so the replayed system prefix matches.
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
        system_prompt = qcode::SystemPrompt::build_default(
            qcode::ToolConfig::orchestrator(
                selected_model != nullptr && selected_model->vision));
    }

    // The generate route stores the modes of each turn (set_session_modes).
    const auto stored_modes = qcode::session::get_session_modes(sid);
    std::string agent_mode = body.value("agent_mode", "");
    if (agent_mode.empty()) {
        agent_mode = stored_modes.first;
    }
    std::string reasoning_mode = body.value("reasoning_mode", "");
    if (reasoning_mode.empty()) {
        reasoning_mode = stored_modes.second;
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
    replay.model = selected_model;
    replay.reasoning_mode = reasoning_mode;
    // The history exactly as the generate route sends it.
    qcode::GenerateOptions opts = qcode::compaction::build_cache_replay_request(
        replay, wire_model, sel.id,
        qcode::prepare_turn_history(snapshot, /*drop_system_notes=*/false));

    const auto started = std::chrono::steady_clock::now();
    qcode::GenerateResult gen_res = qcode::compaction::run_summarizer(client, opts);
    const double model_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    // Billed like any turn call: the session's Stats include it.
    qcode::record_model_call(
        nullptr, sid, selected_model,
        qcode::model_call_usage(opts, gen_res, sel.id, selected_model, model_ms),
        0, false, gen_res.is_success());
    // Observable evidence of the warm-prefix replay: a cache hit here means
    // the summarizer call reused the last routed request's prefix.
    LOG_INFO("Compaction summarizer: model={} cached_prompt_tokens={} "
             "cache_write_tokens={} prompt_tokens={} completion_tokens={}",
             opts.model, gen_res.usage.cached_prompt_tokens,
             gen_res.usage.cache_write_tokens, gen_res.usage.prompt_tokens,
             gen_res.usage.completion_tokens);
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
svr.Get("/session/([^/]+)/stats", [providers_list](const httplib::Request& req, httplib::Response& res) {
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
    // The session's model as configured in opencode.json (prices, limits,
    // thinking); null when it is no longer in the catalog.
    const qcode::ModelInfo* model = nullptr;
    if (providers_list) {
        for (const auto& p : *providers_list) {
            if (p.id != st.provider && p.name != st.provider) continue;
            for (const auto& m : p.models) {
                if (m.id == st.model || m.name == st.model) {
                    model = &m;
                    break;
                }
            }
            break;
        }
    }
    const auto usage = qcode::session::get_session_usage_stats(sid);
    // Per-call usage: billed input with cache split, latency, and the cost of
    // every call at its own model's prices (session_cost).
    j["usage"] = qcode::session::usage_summary_json(usage, model, st.prompt_tokens,
                                                    st.completion_tokens);
    if (model != nullptr) {
        j["model_info"] = {
            {"id", model->id},
            {"name", model->name},
            {"context_window", model->context_window},
            {"output_limit", model->output_limit},
            {"max_tokens", model->max_tokens},
            {"cost", {{"input", model->input_cost},
                      {"output", model->output_cost},
                      {"cache_read", model->cache_read_cost},
                      {"cache_write", model->cache_write_cost}}},
            {"thinking", {{"type", model->thinking_type},
                          {"display", model->thinking_display},
                          {"allow_off", model->thinking_allow_off}}},
            {"reasoning_default", model->reasoning_default},
            {"variants", model->reasoning_efforts},
        };
    } else {
        j["model_info"] = nullptr;
    }
    // Context in use = the prompt of the latest call; the window comes only
    // from opencode.json limit.context (0 = unknown).
    j["context"] = {{"used", usage.last_input_tokens},
                    {"window", model != nullptr ? model->context_window : 0}};
    // Delegated work is billed in the child sessions; roll it up here.
    const auto subagents = qcode::session::get_subagent_usage(sid);
    j["subagents"] = qcode::session::usage_summary_json(subagents.usage, nullptr);
    j["subagents"]["sessions"] = subagents.sessions;
    res.set_content(j.dump(2), "application/json");
});

}

}  // namespace server
}  // namespace qcode
