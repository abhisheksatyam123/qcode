#include <qcode/session/usage_stats.h>

#include <qcode/config/provider_info.h>

#include <algorithm>

namespace qcode {
namespace session {

namespace {

constexpr double kPerToken = 1.0 / 1000000.0;

double number_at(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<double>() : 0.0;
}

std::string string_at(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

long long count_at(const nlohmann::json& j, const char* key) {
    return static_cast<long long>(number_at(j, key));
}

// A configured cost prices the call, even at zero (a free model).
bool has_price(const ModelInfo& model) {
    return model.cost_configured || model.input_cost > 0.0 || model.output_cost > 0.0;
}

nlohmann::json model_totals_to_json(const ModelUsageTotals& m) {
    return {
        {"calls", m.calls},
        {"unpriced_calls", m.unpriced_calls},
        {"input_tokens", m.input_tokens},
        {"cache_read_tokens", m.cache_read_tokens},
        {"cache_write_tokens", m.cache_write_tokens},
        {"output_tokens", m.output_tokens},
        {"reasoning_tokens", m.reasoning_tokens},
        {"cost", m.cost.to_json()},
    };
}

ModelUsageTotals model_totals_from_json(const nlohmann::json& j) {
    ModelUsageTotals m;
    if (!j.is_object()) return m;
    m.calls = static_cast<int>(number_at(j, "calls"));
    m.unpriced_calls = static_cast<int>(number_at(j, "unpriced_calls"));
    m.input_tokens = count_at(j, "input_tokens");
    m.cache_read_tokens = count_at(j, "cache_read_tokens");
    m.cache_write_tokens = count_at(j, "cache_write_tokens");
    m.output_tokens = count_at(j, "output_tokens");
    m.reasoning_tokens = count_at(j, "reasoning_tokens");
    if (const auto it = j.find("cost"); it != j.end()) m.cost = UsageCost::from_json(*it);
    return m;
}

}  // namespace

void UsageCost::add(const UsageCost& other) {
    priced = priced || other.priced;
    input += other.input;
    cache_read += other.cache_read;
    cache_write += other.cache_write;
    output += other.output;
}

nlohmann::json UsageCost::to_json() const {
    return {
        {"priced", priced},
        {"input", input},
        {"cache_read", cache_read},
        {"cache_write", cache_write},
        {"output", output},
        {"total", total()},
    };
}

UsageCost UsageCost::from_json(const nlohmann::json& j) {
    UsageCost c;
    if (!j.is_object()) return c;
    const auto it = j.find("priced");
    c.priced = it != j.end() && it->is_boolean() && it->get<bool>();
    c.input = number_at(j, "input");
    c.cache_read = number_at(j, "cache_read");
    c.cache_write = number_at(j, "cache_write");
    c.output = number_at(j, "output");
    return c;
}

UsageCost price_tokens(long long input, long long cache_read, long long cache_write,
                       long long output, const ModelInfo& model) {
    UsageCost cost;
    cost.priced = has_price(model);
    if (!cost.priced) return cost;
    const double read_rate =
        model.cache_read_cost > 0.0 ? model.cache_read_cost : model.input_cost;
    const double write_rate =
        model.cache_write_cost > 0.0 ? model.cache_write_cost : model.input_cost;
    cache_read = std::max(0LL, cache_read);
    cache_write = std::max(0LL, cache_write);
    const long long uncached = std::max(0LL, input - cache_read - cache_write);
    cost.input = static_cast<double>(uncached) * model.input_cost * kPerToken;
    cost.cache_read = static_cast<double>(cache_read) * read_rate * kPerToken;
    cost.cache_write = static_cast<double>(cache_write) * write_rate * kPerToken;
    cost.output = static_cast<double>(std::max(0LL, output)) * model.output_cost * kPerToken;
    return cost;
}

UsageCost price_call(const ModelCallUsage& call, const ModelInfo& model) {
    return price_tokens(call.input_tokens, call.cache_read_tokens, call.cache_write_tokens,
                        call.output_tokens, model);
}

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
    last_input_tokens = std::max(0, call.input_tokens);
    last_effort = call.effort;
    if (!call.variant.empty()) last_variant = call.variant;
    if (call.cost.priced) {
        ++priced_calls;
        cost.add(call.cost);
    } else {
        ++unpriced_calls;
    }
    if (!call.model.empty()) {
        auto& m = by_model[call.provider.empty() ? call.model
                                                 : call.provider + "/" + call.model];
        ++m.calls;
        m.input_tokens += std::max(0, call.input_tokens);
        m.cache_read_tokens += std::max(0, call.cache_read_tokens);
        m.cache_write_tokens += std::max(0, call.cache_write_tokens);
        m.output_tokens += std::max(0, call.output_tokens);
        m.reasoning_tokens += std::max(0, call.reasoning_tokens);
        if (call.cost.priced) {
            m.cost.add(call.cost);
        } else {
            ++m.unpriced_calls;
        }
    }
}

void SessionUsageStats::merge(const SessionUsageStats& other) {
    model_calls += other.model_calls;
    model_ms_total += other.model_ms_total;
    model_ms_max = std::max(model_ms_max, other.model_ms_max);
    ttft_ms_total += other.ttft_ms_total;
    ttft_count += other.ttft_count;
    input_tokens += other.input_tokens;
    cache_read_tokens += other.cache_read_tokens;
    cache_write_tokens += other.cache_write_tokens;
    output_tokens += other.output_tokens;
    reasoning_tokens += other.reasoning_tokens;
    cost.add(other.cost);
    priced_calls += other.priced_calls;
    unpriced_calls += other.unpriced_calls;
    legacy_calls += other.legacy_calls;
    for (const auto& [key, theirs] : other.by_model) {
        auto& m = by_model[key];
        m.calls += theirs.calls;
        m.unpriced_calls += theirs.unpriced_calls;
        m.input_tokens += theirs.input_tokens;
        m.cache_read_tokens += theirs.cache_read_tokens;
        m.cache_write_tokens += theirs.cache_write_tokens;
        m.output_tokens += theirs.output_tokens;
        m.reasoning_tokens += theirs.reasoning_tokens;
        m.cost.add(theirs.cost);
    }
}

nlohmann::json SessionUsageStats::to_json() const {
    nlohmann::json models = nlohmann::json::object();
    for (const auto& [key, totals] : by_model) models[key] = model_totals_to_json(totals);
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
        {"last_input_tokens", last_input_tokens},
        {"last_effort", last_effort},
        {"last_variant", last_variant},
        {"cost", cost.to_json()},
        {"priced_calls", priced_calls},
        {"unpriced_calls", unpriced_calls},
        {"legacy_calls", legacy_calls},
        {"by_model", std::move(models)},
    };
}

SessionUsageStats SessionUsageStats::from_json(const nlohmann::json& j) {
    SessionUsageStats s;
    if (!j.is_object()) return s;
    s.model_calls = static_cast<int>(number_at(j, "model_calls"));
    s.model_ms_total = number_at(j, "model_ms_total");
    s.model_ms_max = number_at(j, "model_ms_max");
    s.model_ms_last = number_at(j, "model_ms_last");
    s.ttft_ms_total = number_at(j, "ttft_ms_total");
    s.ttft_count = static_cast<int>(number_at(j, "ttft_count"));
    s.ttft_ms_last = j.contains("ttft_ms_last") ? number_at(j, "ttft_ms_last") : -1.0;
    s.input_tokens = count_at(j, "input_tokens");
    s.cache_read_tokens = count_at(j, "cache_read_tokens");
    s.cache_write_tokens = count_at(j, "cache_write_tokens");
    s.output_tokens = count_at(j, "output_tokens");
    s.reasoning_tokens = count_at(j, "reasoning_tokens");
    s.last_input_tokens = static_cast<int>(number_at(j, "last_input_tokens"));
    s.last_effort = string_at(j, "last_effort");
    s.last_variant = string_at(j, "last_variant");
    if (const auto it = j.find("cost"); it != j.end() && it->is_object()) {
        s.cost = UsageCost::from_json(*it);
        s.priced_calls = static_cast<int>(number_at(j, "priced_calls"));
        s.unpriced_calls = static_cast<int>(number_at(j, "unpriced_calls"));
        s.legacy_calls = static_cast<int>(number_at(j, "legacy_calls"));
    } else {
        // Written before per-call pricing: these calls carry no cost.
        s.legacy_calls = s.model_calls;
    }
    if (const auto it = j.find("by_model"); it != j.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            s.by_model[key] = model_totals_from_json(value);
        }
    }
    return s;
}

UsageCost estimate_usage_cost(const SessionUsageStats& usage, const ModelInfo& model) {
    return price_tokens(usage.input_tokens, usage.cache_read_tokens, usage.cache_write_tokens,
                        usage.output_tokens, model);
}

SessionCost session_cost(const SessionUsageStats& usage, const ModelInfo* current,
                         long long turn_prompt_tokens, long long turn_completion_tokens) {
    SessionCost out;
    const auto estimate = [&](UsageCost parts) {
        if (!parts.priced) return;
        out.parts = parts;
        out.total = parts.total();
        out.available = true;
        out.estimated = true;
    };
    if (usage.empty()) {
        // Recorded before per-call stats: only the turn totals exist.
        if (current != nullptr && turn_prompt_tokens + turn_completion_tokens > 0) {
            estimate(price_tokens(turn_prompt_tokens, 0, 0, turn_completion_tokens, *current));
        }
        return out;
    }
    if (usage.legacy_calls >= usage.model_calls) {
        // Every call predates per-call pricing.
        if (current != nullptr) estimate(estimate_usage_cost(usage, *current));
        return out;
    }
    out.parts = usage.cost;
    out.total = usage.cost.total();
    out.available = usage.priced_calls > 0;
    out.unpriced_calls = usage.unpriced_calls;
    out.legacy_calls = usage.legacy_calls;
    return out;
}

nlohmann::json usage_summary_json(const SessionUsageStats& usage, const ModelInfo* current,
                                  long long turn_prompt_tokens,
                                  long long turn_completion_tokens) {
    nlohmann::json j = usage.to_json();
    const double model_secs = usage.model_ms_total / 1000.0;
    j["avg_call_ms"] = usage.model_calls > 0 ? usage.model_ms_total / usage.model_calls : 0.0;
    j["avg_ttft_ms"] = usage.ttft_count > 0
                           ? nlohmann::json(usage.ttft_ms_total / usage.ttft_count)
                           : nlohmann::json(nullptr);
    j["output_tok_per_s"] =
        model_secs > 0.0 ? static_cast<double>(usage.output_tokens) / model_secs : 0.0;
    j["cache_hit_pct"] = usage.input_tokens > 0
                             ? static_cast<double>(usage.cache_read_tokens) * 100.0 /
                                   static_cast<double>(usage.input_tokens)
                             : 0.0;
    j["uncached_input_tokens"] = std::max(
        0LL, usage.input_tokens - usage.cache_read_tokens - usage.cache_write_tokens);
    const SessionCost cost =
        session_cost(usage, current, turn_prompt_tokens, turn_completion_tokens);
    nlohmann::json session = cost.parts.to_json();
    session["available"] = cost.available;
    session["estimated"] = cost.estimated;
    session["total"] = cost.total;
    session["unpriced_calls"] = cost.unpriced_calls;
    session["legacy_calls"] = cost.legacy_calls;
    j["session_cost"] = std::move(session);
    return j;
}

}  // namespace session
}  // namespace qcode
