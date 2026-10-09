#include <qcode/compaction/compaction_request.h>

#include "generation/stream_step.h"

#include <qcode/core/stream_options.h>
#include <qcode/generation/turn_prefix.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/session/session_store.h>
#include <qcode/session/task_notes.h>

#include <chrono>
#include <cstdlib>
#include <string_view>

#include <filesystem>
#include <fstream>

namespace qcode {
namespace compaction {

namespace {

// Delivered as the FINAL user message of the replayed conversation — never
// as a replacement system prompt — so the conversation's own system prompt,
// tools and message prefix stay in front of it and the provider's warm
// prefix cache is reused (dsh compaction-basic summarizer rule).
const char* kDirective =
    "You are now acting as a compaction engine for this AI coding assistant. "
    "Condense the conversation ABOVE into a structured checkpoint (handoff "
    "packet) so a fresh session can resume the work with no loss of "
    "essential context.\n\n"
    "Output EXACTLY the Markdown structure below: keep both sections, in "
    "order. Use terse bullets, not prose paragraphs. Write \"(none)\" for an "
    "empty section — never drop a section.\n\n"
    "## Tasks\n"
    "- [the user's requests and instructions; the task in progress right "
    "now and its exact next step; other open tasks with the verified/current "
    "state of each]\n\n"
    "## Systems\n"
    "- [concise facts that matter: code, APIs, data structures, files, "
    "commands, verified evidence, blockers, and user preferences]\n\n"
    "Rules:\n"
    "- Preserve exact file paths, commands, error strings, identifiers, "
    "numeric values, and user instructions — especially corrections and "
    "feedback.\n"
    "- Preserve the next actionable task, pending jobs, blockers, and "
    "verified evidence.\n"
    "- Do NOT mention this summarization request or that the context was "
    "compacted.\n"
    "- Output only the checkpoint text. Do NOT call any tool and take no "
    "other action.\n";

}  // namespace

std::string directive() { return kDirective; }

std::string handoff_path(const std::string& session_id) {
  namespace fs = std::filesystem;
  std::string ws = session::get_session_workspace(session_id);
  std::error_code ec;
  fs::path root = ws.empty() ? fs::current_path(ec) : fs::path(ws);
  return (root / "scratchpad" / ("handoff-" + session_id + ".md")).string();
}

std::string write_handoff(const std::string& session_id,
                          const std::string& summary) {
  namespace fs = std::filesystem;
  const fs::path path = handoff_path(session_id);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return {};
  std::ofstream out(path);
  if (!out) return {};
  out << "# qcode compacted handoff\n\n" << summary << "\n";
  return out ? path.string() : std::string{};
}

GenerateOptions build_cache_replay_request(const CacheReplayInput& input,
                                           const std::string& wire_model,
                                           const std::string& provider_id,
                                           Messages history) {
  const bool is_subagent =
      input.is_subagent || (input.agent_mode == "subagent");

  static const std::vector<ProviderInfo> kNoProviders;

  GenerateOptions opts;
  opts.model = wire_model;
  // Byte-identical to ChatBus's system prompt for the same inputs.
  opts.system = build_turn_system_prompt(input.system_prompt, is_subagent,
                                         input.providers ? *input.providers
                                                         : kNoProviders);
  // Replay the history through the same normalization pass the live turn
  // uses, then append the directive as the trailing user message.
  const Model transform_model(wire_model, provider_id);
  opts.messages =
      ProviderTransform::normalize_messages(std::move(history), transform_model);
  opts.messages.push_back(Message::user(directive()));
  // Same request parameters as the turn (generation_service run_generation):
  // a summarizer without the turn's thinking settings would drop the signed
  // thinking blocks from the replayed history and miss the message cache;
  // the variant prompt rides on the system prompt.
  apply_turn_sampling(opts, input.model, transform_model);
  apply_variant_options(opts, input.model, input.reasoning_mode, wire_model);
  // Same tool schemas as the last routed request — tool definitions are
  // part of the cacheable prefix (and Zen requires bash/read declared).
  if (input.enable_tools) {
    opts.tools = build_turn_tools(!is_subagent,
                                  input.vision_supported);
  }
  opts.session_id = input.session_id;
  opts.max_steps = 1;  // single wire call; tools are declared, never executed
  return opts;
}

size_t auto_compact_threshold(const ModelInfo* model) {
  if (model == nullptr || !model->auto_compact) return 0;
  const size_t configured =
      model->compact_threshold > 0 ? static_cast<size_t>(model->compact_threshold) : 0;
  if (model->context_window <= 0) return configured;
  const size_t window = static_cast<size_t>(model->context_window);
  // Room for the reply: the request's output budget, kept between 10% and
  // 25% of the window.
  const auto out = ProviderTransform::max_output_tokens(*model);
  const size_t out_tokens = out ? static_cast<size_t>(*out) : 0;
  const size_t reserve = std::max(window / 10, std::min(out_tokens, window / 4));
  const size_t cap = window > reserve ? window - reserve : window / 2;
  return configured > 0 ? std::min(configured, cap) : cap;
}

GenerateOptions build_in_loop_request(const GenerateOptions& step_request) {
  GenerateOptions opts = step_request;
  opts.messages.push_back(Message::user(directive()));
  opts.prompt.clear();
  opts.max_steps = 1;
  opts.on_step_finish.reset();
  opts.on_tool_call_start.reset();
  opts.on_tool_call_finish.reset();
  opts.on_tool_call_confirm.reset();
  opts.on_retry.reset();
  opts.subagent_runner = nullptr;
  opts.routing_board = nullptr;
  opts.has_queued_work = nullptr;
  return opts;
}

std::string continuation_message(const std::string& summary,
                                 const std::string& handoff_path,
                                 const std::string& workspace, bool mid_turn) {
  std::string out = kSummaryMarker;
  out += handoff_path.empty() ? std::string(".") : " written to: " + handoff_path + ".";
  out += "\n\n";
  out += summary;
  // The task file is the durable thread state (Tasks / Systems / Log): give
  // the fresh context its current contents, as Claude Code re-attaches its
  // todo list after compacting.
  const auto notes = task_notes::resolve(workspace);
  if (notes.exists) {
    std::ifstream in(notes.path);
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    constexpr size_t kMaxTaskFileChars = 24000;
    if (!text.empty()) {
      out += "\n\nTask file " + notes.path + " (current contents";
      if (text.size() > kMaxTaskFileChars) {
        // Keep the head (Tasks, Systems) and the newest Log lines.
        const size_t head = kMaxTaskFileChars * 2 / 3;
        const size_t tail = kMaxTaskFileChars - head;
        text = text.substr(0, head) + "\n[... truncated ...]\n" +
               text.substr(text.size() - tail);
        out += ", truncated";
      }
      out += "):\n" + text;
    }
  }
  if (mid_turn) {
    out +=
        "\n\nThe earlier conversation was replaced by the summary above "
        "because the context grew large. Continue the in-progress task from "
        "where it left off without asking the user any further questions. "
        "Re-read any file before editing it; earlier tool output is no longer "
        "visible.";
  }
  return out;
}

GenerateResult run_summarizer(Client& client, const GenerateOptions& request) {
  // Same switch as the tool loop (QCODE_TOOL_STREAMING=0 forces blocking).
  const char* env = std::getenv("QCODE_TOOL_STREAMING");
  const bool streaming = !(env && std::string_view(env) == "0");
  if (!streaming || !client.supports_tool_streaming()) {
    return client.generate_text(request);
  }
  StreamOptions options;
  static_cast<GenerateOptions&>(options) = request;
  StreamResult stream;
  try {
    stream = client.stream_text(options);
  } catch (const std::exception& e) {
    return GenerateResult("Exception: " + std::string(e.what()));
  }
  StreamAccumulator summary;
  while (!stream.is_complete()) {
    auto event = stream.poll(std::chrono::milliseconds(250));
    if (!event) continue;
    summary.add(*event);
    if (event->is_error()) break;
  }
  return summary.take();
}

}  // namespace compaction
}  // namespace qcode
