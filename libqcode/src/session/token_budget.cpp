#include <qcode/core/perf.h>
#include <qcode/session/token_budget.h>
#include <qcode/tools/image_tool.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <variant>

namespace qcode {

// ── Token estimation ─────────────────────────────────────────────
// Heuristic: ~4 chars per token (good for English + code).
// Per-message overhead: ~4 tokens for role + formatting.
static constexpr size_t kCharsPerToken = 4;
static constexpr size_t kMessageOverheadTokens = 4;
// Images are billed by pixels, not base64 length, and providers downscale
// big ones (Anthropic caps an image at ~1600 tokens). Counting base64 chars
// made one 1 MB screenshot look like ~333k tokens and forced pruning.
static constexpr size_t kImageTokens = 1600;

namespace {

// Size of a JSON value as nlohmann::json::dump() would emit it, computed by
// walking the tree without building any string. Image-shaped objects
// ({"data": <string>, "mime_type": <string>}) are charged a flat
// kImageTokens and their base64 payload is never read.
struct JsonSize {
    size_t chars = 0;         // serialized byte count (excluding image cost)
    size_t image_tokens = 0;  // flat token cost for image payloads
};

size_t tokens_from_chars(size_t chars) {
    return (chars + kCharsPerToken - 1) / kCharsPerToken;
}

// Length of a string once quoted and escaped the way nlohmann dump() does.
size_t escaped_string_size(const std::string& s) {
    size_t n = 2;  // surrounding quotes
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            n += 2;
        } else if (c < 0x20) {
            n += (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') ? 2 : 6;
        } else {
            n += 1;
        }
    }
    return n;
}

bool is_image_shaped(const qcode::JsonValue& v) {
    auto d = v.find("data");
    auto m = v.find("mime_type");
    return d != v.end() && d->is_string() && m != v.end() && m->is_string();
}

void accumulate_json(const qcode::JsonValue& v, JsonSize& out) {
    if (v.is_string()) {
        out.chars += escaped_string_size(v.get_ref<const std::string&>());
    } else if (v.is_object()) {
        if (is_image_shaped(v)) {
            out.image_tokens += kImageTokens;
            return;
        }
        out.chars += 2;  // braces
        bool first = true;
        for (const auto& [key, val] : v.items()) {
            if (!first) out.chars += 1;  // ','
            first = false;
            out.chars += escaped_string_size(key) + 1;  // key + ':'
            accumulate_json(val, out);
        }
    } else if (v.is_array()) {
        out.chars += 2;  // brackets
        bool first = true;
        for (const auto& item : v) {
            if (!first) out.chars += 1;  // ','
            first = false;
            accumulate_json(item, out);
        }
    } else if (v.is_null()) {
        out.chars += 4;
    } else if (v.is_boolean()) {
        out.chars += v.get<bool>() ? 4 : 5;
    } else if (v.is_number()) {
        // Scalar numbers are short; dumping just the number is cheap and exact.
        out.chars += v.dump().size();
    }
}

size_t json_tokens(const qcode::JsonValue& v) {
    JsonSize s;
    accumulate_json(v, s);
    return tokens_from_chars(s.chars) + s.image_tokens;
}

// Token cost of a single content part (excluding per-message overhead).
// Shared by estimate_tokens() and prune_context()'s running total so the two
// can never drift apart.
size_t part_tokens(const qcode::ContentPart& part) {
    if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
        return tokens_from_chars(tp->text.size());
    }
    if (const auto* tcp = std::get_if<qcode::ToolCallContentPart>(&part)) {
        return tokens_from_chars(tcp->tool_name.size()) + json_tokens(tcp->arguments);
    }
    if (const auto* trp = std::get_if<qcode::ToolResultContentPart>(&part)) {
        return qcode::ImageTool::is_image_result(*trp) ? kImageTokens
                                                       : json_tokens(trp->result);
    }
    if (const auto* rp = std::get_if<qcode::ReasoningContentPart>(&part)) {
        return tokens_from_chars(rp->text.size());
    }
    if (std::holds_alternative<qcode::ImageContentPart>(part)) {
        return kImageTokens;
    }
    return 0;
}

size_t content_tokens(const qcode::MessageContent& content) {
    size_t total = 0;
    for (const auto& part : content) total += part_tokens(part);
    return total;
}

// Preview text for a pruned tool result, without serializing the whole value.
//   - String results: use the string's own prefix (no JSON quoting/escaping).
//   - Small structured results (<= kMaxPreviewDumpBytes serialized): dump them,
//     which is bounded by the size cap.
//   - Anything larger: a fixed placeholder carrying the serialized byte count.
constexpr size_t kPreviewChars = 80;
constexpr size_t kMaxPreviewDumpBytes = 1024;

std::string pruned_result_preview(const qcode::JsonValue& v, size_t json_bytes) {
    if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        size_t n = std::min(s.size(), kPreviewChars);
        // Do not split a multi-byte UTF-8 sequence.
        while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {
            --n;
        }
        return "[Pruned: " + s.substr(0, n) + "...]";
    }
    if (json_bytes <= kMaxPreviewDumpBytes) {
        return "[Pruned: " + v.dump().substr(0, kPreviewChars) + "...]";
    }
    return "[Pruned: " + std::to_string(json_bytes) + " bytes]";
}

}  // namespace

size_t estimate_tokens(const qcode::Messages& messages) {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += kMessageOverheadTokens + content_tokens(msg.content);
    }
    return total;
}

size_t estimate_system_tokens(const std::string& system_prompt) {
    return (system_prompt.size() + kCharsPerToken - 1) / kCharsPerToken;
}

// ── Context pruning ──────────────────────────────────────────────
// Prune a COPY of the messages to fit within the context budget.
// Returns a pruned copy; the original is never modified.
//
// Strategy (applied in escalating passes until under budget):
//   Pass 1: Replace old ToolResult JSON >200 chars with compact placeholder
//   Pass 2: Strip old ReasoningContentPart text entirely
//   Pass 3: Replace old assistant text with "[Earlier response pruned]"
// Messages in the last `keep_recent` window are always preserved.
qcode::Messages prune_context(qcode::Messages messages,
                           size_t context_window,
                           size_t keep_recent) {
    if (context_window == 0 || messages.size() <= keep_recent)
        return messages;

    PERF_SCOPE("prune_context");
    qcode::Messages result = std::move(messages);
    const size_t budget = (context_window * 4) / 5;  // 80%

    // Running total, always equal to estimate_tokens(result). Each mutation
    // subtracts the old part cost and adds the new one, so no pass has to
    // re-walk the whole conversation.
    size_t total = estimate_tokens(result);
    if (total <= budget) return result;

    // Replace a part via `mutate`, keeping `total` in sync.
    auto mutate_part = [&total](qcode::ContentPart& part, auto&& mutate) {
        total -= part_tokens(part);
        mutate(part);
        total += part_tokens(part);
    };

    const size_t prune_limit = result.size() - keep_recent;

    // ── Pass 1: Prune large tool results ──
    for (size_t i = 0; i < prune_limit; ++i) {
        for (auto& part : result[i].content) {
            auto* trp = std::get_if<qcode::ToolResultContentPart>(&part);
            if (!trp) continue;
            if (qcode::ImageTool::is_image_result(*trp)) {
                mutate_part(part, [&](qcode::ContentPart& p) {
                    auto& t = std::get<qcode::ToolResultContentPart>(p);
                    t.result = "[Pruned: " + qcode::ImageTool::summary(t.result) + "]";
                });
                continue;
            }
            JsonSize s;
            accumulate_json(trp->result, s);
            if (s.image_tokens > 0) {
                // Contains an image payload: never serialize the base64.
                mutate_part(part, [](qcode::ContentPart& p) {
                    std::get<qcode::ToolResultContentPart>(p).result =
                        "[Pruned: embedded image payload]";
                });
            } else if (s.chars > 200) {
                mutate_part(part, [&](qcode::ContentPart& p) {
                    auto& t = std::get<qcode::ToolResultContentPart>(p);
                    t.result = pruned_result_preview(t.result, s.chars);
                });
            }
        }
    }
    if (total <= budget) return result;

    // ── Pass 2: Strip old reasoning content ──
    for (size_t i = 0; i < prune_limit; ++i) {
        for (auto& part : result[i].content) {
            auto* rp = std::get_if<qcode::ReasoningContentPart>(&part);
            if (rp && !rp->text.empty()) {
                mutate_part(part, [](qcode::ContentPart& p) {
                    auto& r = std::get<qcode::ReasoningContentPart>(p);
                    r.text = "[Reasoning pruned]";
                    r.signature.clear();
                });
            }
        }
    }
    if (total <= budget) return result;

    // ── Pass 3: Replace old assistant text with summary placeholder ──
    for (size_t i = 0; i < prune_limit; ++i) {
        if (result[i].role != qcode::kMessageRoleAssistant) continue;
        for (auto& part : result[i].content) {
            auto* tp = std::get_if<qcode::TextContentPart>(&part);
            if (tp && tp->text.size() > 100) {
                mutate_part(part, [](qcode::ContentPart& p) {
                    std::get<qcode::TextContentPart>(p).text = "[Earlier response pruned]";
                });
            }
        }
    }
    if (total <= budget) return result;

    // ── Pass 4: Drop old assistant messages entirely (keep role marker) ──
    for (size_t i = 0; i < prune_limit; ++i) {
        if (result[i].role == qcode::kMessageRoleAssistant) {
            total -= content_tokens(result[i].content);
            result[i].content = {qcode::TextContentPart{"[Older messages pruned]"}};
            total += content_tokens(result[i].content);
        }
        if (total <= budget) break;
    }

    return result;
}

} // namespace qcode

// ── Calibration ──────────────────────────────────────────────────
namespace qcode {

size_t calibrate_estimate(size_t heuristic,
                          size_t last_actual,
                          size_t last_estimated) {
    if (last_actual == 0 || last_estimated == 0) return heuristic;
    double ratio = (double)last_actual / (double)last_estimated;
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 2.0) ratio = 2.0;
    return (size_t)(heuristic * ratio);
}

} // namespace qcode
