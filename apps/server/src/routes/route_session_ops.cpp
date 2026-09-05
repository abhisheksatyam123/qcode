#include "routes_internal.h"
#include "session_runtime.h"
#include "http_utils.h"

#include <qcode/config/config.h>
#include <qcode/config/provider_info.h>
#include <qcode/core/message.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/providers/registry.h>
#include <qcode/session/session_store.h>

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

    // Get session info to know which provider/model to use for compaction
    std::string provider_id = "";
    std::string model_id = "";
    for (const auto& s : qcode::session::list_sessions_full()) {
        if (s.id == sid) {
            provider_id = s.provider;
            model_id = s.model;
            break;
        }
    }

    if (provider_id.empty() || model_id.empty()) {
        // Fallback to default provider/model
        provider_id = (*providers_list)[0].id;
        model_id = (*providers_list)[0].models[0].id;
    }

    // Find the ProviderInfo
    int sp = -1;
    for (int i = 0; i < static_cast<int>(providers_list->size()); ++i) {
        if ((*providers_list)[i].id == provider_id || (*providers_list)[i].name == provider_id) {
            sp = i;
            break;
        }
    }
    if (sp == -1) sp = 0;

    int sm = -1;
    for (int i = 0; i < static_cast<int>((*providers_list)[sp].models.size()); ++i) {
        if ((*providers_list)[sp].models[i].id == model_id || (*providers_list)[sp].models[i].name == model_id) {
            sm = i;
            break;
        }
    }
    if (sm == -1) sm = 0;

    const auto& sel = (*providers_list)[sp];
    const qcode::ModelInfo* selected_model =
        (sm >= 0 && sm < static_cast<int>(sel.models.size()))
            ? &sel.models[sm]
            : nullptr;

    // Format the transcript for compaction
    std::ostringstream transcript;
    for (const auto& m : snapshot) {
        std::string role_str;
        if (m.role == qcode::kMessageRoleUser) role_str = "User";
        else if (m.role == qcode::kMessageRoleAssistant) role_str = "Assistant";
        else if (m.role == qcode::kMessageRoleSystem) role_str = "System";
        else role_str = "Message";
        std::string text;
        for (const auto& part : m.content) {
            if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                text += tp->text + "\n";
            } else if (const auto* tcp = std::get_if<qcode::ToolCallContentPart>(&part)) {
                text += "[Tool call: " + tcp->tool_name + "]\n";
            } else if (std::holds_alternative<qcode::ToolResultContentPart>(part)) {
                text += "[Tool result]\n";
            } else if (const auto* rcp = std::get_if<qcode::ReasoningContentPart>(&part)) {
                if (!rcp->text.empty()) text += "[Reasoning: " + rcp->text + "]\n";
            }
        }
        transcript << role_str << ": " << text << "\n";
    }

    const std::string compaction_instruction =
        "Tools are available when needed, including bash for bounded read-only "
        "inspection. Do not modify files or create notes while generating the "
        "handoff summary.\n\n"
        "Summarize the following conversation into a concise handoff packet so a "
        "fresh session can take over. Preserve the next actionable task, verified "
        "evidence, blockers, and concise facts about the code, APIs, data "
        "structures, files, and user preferences that matter for the request.\n\n"
        "Keep only task state and concise facts.\n\n"
        "Output only:\n"
        "## Tasks\n"
        "## Systems\n\n"
        "Use tools only when they improve summary accuracy; otherwise answer directly.";

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
    auto resolution = qcode::providers::ProviderRegistry::instance().resolve(
        sel.id, provider_options);
    if (!resolution.ok()) {
        res.status = 500;
        nlohmann::json err_j; err_j["error"] = "Compaction failed: " + resolution.error; res.set_content(err_j.dump(), "application/json");
        return;
    }

    qcode::Client client = std::move(resolution.client);

    qcode::GenerateOptions opts;
    opts.model = (*providers_list)[sp].models[sm].id;
    opts.system = compaction_instruction;
    opts.messages = {qcode::Message::user(transcript.str())};

    qcode::GenerateResult gen_res = client.generate_text(opts);
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

    std::string notes_root = qcode::get_notes_root();
    std::error_code ec;
    std::filesystem::create_directories(
        notes_root + "/scratchpad/task/qcode-tui/active", ec);
    std::string todo_path = notes_root +
                            "/scratchpad/task/qcode-tui/active/todo-" + sid + ".md";
    bool wrote = false;
    if (!ec) {
        std::ofstream out(todo_path);
        if (out) {
            out << "# qcode compacted handoff\n\n" << summary << "\n";
            wrote = true;
        }
    }

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
    // Pull live counters from an in-memory generation session, if present.
    // Token totals are persisted per-turn in the DB (the cumulative source of
    // truth), so pass 0 for the live token args; only the tool-call counters
    // (reconstructed from stored messages for completed turns + in-flight ctx
    // for the current turn) need the live values.
    int live_tool_calls = 0;
    double live_tool_time_ms = 0.0;
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto it = g_sessions.find(sid);
        if (it != g_sessions.end()) {
            live_tool_calls = it->second->ctx.tool_call_count;
            live_tool_time_ms = it->second->ctx.total_tool_time_ms;
        }
    }
    auto st = qcode::session::get_session_stats(sid, live_tool_calls, live_tool_time_ms,
                                             0, 0, 0);
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
