#include <qcode/session/usage_stats.h>

#include <qcode/config/provider_info.h>

#include <algorithm>

namespace qcode {
namespace session {

void SessionUsageStats::add(const ModelCallUsage& call) {
    ++model_calls;
    model_ms_total += std::max(0.0, call.model_ms);
    model_ms_max = std::max(model_ms_max, call.model_ms);
    model_ms_last = call.model_ms;
    ttft_ms_last = call.ttft_ms;
    if (call.ttft_ms >= 0.0) {
        ttft_ms_total += call.ttft_ms;
        ++ttft_count;
    }
    input_tokens += std::max(0, call.input_tokens);
    cache_read_tokens += std::max(0, call.cache_read_tokens);
    cache_write_tokens += std::max(0, call.cache_write_tokens);
    output_tokens += std::max(0, call.output_tokens);
    reasoning_tokens += std::max(0, call.reasoning_tokens);
    last_effort = call.effort;
    if (!call.variant.empty()) last_variant = call.variant;
}

nlohmann::json SessionUsageStats::to_json() const {
    return {
        {"model_calls", model_calls},
        {"model_ms_total", model_ms_total},
        {"model_ms_max", model_ms_max},
        {"model_ms_last", model_ms_last},
        {"ttft_ms_total", ttft_ms_total},
        {"ttft_count", ttft_count},
        {"ttft_ms_last", ttft_ms_last},
        {"input_tokens", input_tokens},
        {"cache_read_tokens", cache_read_tokens},
        {"cache_write_tokens", cache_write_tokens},
        {"output_tokens", output_tokens},
        {"reasoning_tokens", reasoning_tokens},
        {"last_effort", last_effort},
        {"last_variant", last_variant},
    };
}

SessionUsageStats SessionUsageStats::from_json(const nlohmann::json& j) {
    SessionUsageStats s;
    if (!j.is_object()) return s;
    const auto num = [&](const char* key) -> double {
        const auto it = j.find(key);
        return (it != j.end() && it->is_number()) ? it->get<double>() : 0.0;
    };
    const auto str = [&](const char* key) -> std::string {
        const auto it = j.find(key);
        return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
    };
    s.model_calls = static_cast<int>(num("model_calls"));
    s.model_ms_total = num("model_ms_total");
    s.model_ms_max = num("model_ms_max");
    s.model_ms_last = num("model_ms_last");
    s.ttft_ms_total = num("ttft_ms_total");
    s.ttft_count = static_cast<int>(num("ttft_count"));
    s.ttft_ms_last = j.contains("ttft_ms_last") ? num("ttft_ms_last") : -1.0;
    s.input_tokens = static_cast<long long>(num("input_tokens"));
    s.cache_read_tokens = static_cast<long long>(num("cache_read_tokens"));
    s.cache_write_tokens = static_cast<long long>(num("cache_write_tokens"));
    s.output_tokens = static_cast<long long>(num("output_tokens"));
    s.reasoning_tokens = static_cast<long long>(num("reasoning_tokens"));
    s.last_effort = str("last_effort");
    s.last_variant = str("last_variant");
    return s;
}

UsageCost estimate_usage_cost(const SessionUsageStats& usage, const ModelInfo& model) {
    UsageCost cost;
    cost.priced = model.input_cost > 0.0 || model.output_cost > 0.0;
    if (!cost.priced) return cost;
    constexpr double kPerToken = 1.0 / 1000000.0;
    const double read_rate =
        model.cache_read_cost > 0.0 ? model.cache_read_cost : model.input_cost;
    const double write_rate =
        model.cache_write_cost > 0.0 ? model.cache_write_cost : model.input_cost;
    const long long uncached = std::max<long long>(
        0, usage.input_tokens - usage.cache_read_tokens - usage.cache_write_tokens);
    cost.input = static_cast<double>(uncached) * model.input_cost * kPerToken;
    cost.cache_read = static_cast<double>(usage.cache_read_tokens) * read_rate * kPerToken;
    cost.cache_write = static_cast<double>(usage.cache_write_tokens) * write_rate * kPerToken;
    cost.output = static_cast<double>(usage.output_tokens) * model.output_cost * kPerToken;
    return cost;
}

}  // namespace session
}  // namespace qcode
