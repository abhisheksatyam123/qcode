#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace qcode {

struct ModelInfo;

namespace session {

// USD at a model's opencode.json prices (per 1M tokens: cost.input/output/
// cache_read/cache_write). priced=false when no price applied.
struct UsageCost {
    bool priced = false;
    double input = 0.0;        // uncached input
    double cache_read = 0.0;
    double cache_write = 0.0;
    double output = 0.0;
    [[nodiscard]] double total() const { return input + cache_read + cache_write + output; }
    void add(const UsageCost& other);
    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] static UsageCost from_json(const nlohmann::json& j);
};

// Price tokens at `model`'s rates. `input` is the whole prompt (incl. cache
// reads/writes); cache reads/writes without a configured price are billed at
// the input price. priced=false when the model has no input/output price.
[[nodiscard]] UsageCost price_tokens(long long input, long long cache_read,
                                     long long cache_write, long long output,
                                     const ModelInfo& model);

// One model call (one tool-loop step or one streamed turn): what it cost and
// how long it took. Token counts are what the provider billed for this call.
struct ModelCallUsage {
    double model_ms = 0.0;
    double ttft_ms = -1.0;        // < 0: no first-token timing
    int input_tokens = 0;         // whole prompt incl. cache reads/writes
    int cache_read_tokens = 0;    // subset of input served from the cache
    int cache_write_tokens = 0;   // subset of input written to the cache
    int output_tokens = 0;        // incl. thinking
    int reasoning_tokens = 0;     // thinking subset of output
    std::string effort;           // wire effort sent ("off" when none)
    std::string variant;          // picker variant ("ultra"); empty if unknown
    std::string provider;         // provider id that served the call
    std::string model;            // model id (opencode.json key)
    UsageCost cost;               // at the model's prices when the call ran
};

// This call's tokens at `model`'s prices.
[[nodiscard]] UsageCost price_call(const ModelCallUsage& call, const ModelInfo& model);

// One model's share of a session ("provider/model" in SessionUsageStats).
struct ModelUsageTotals {
    int calls = 0;
    int unpriced_calls = 0;
    long long input_tokens = 0;
    long long cache_read_tokens = 0;
    long long cache_write_tokens = 0;
    long long output_tokens = 0;
    long long reasoning_tokens = 0;
    UsageCost cost;
};

// Per-session aggregate of every model call (persisted as JSON in
// sessions.usage_stats so the Stats tab survives restarts / switches).
struct SessionUsageStats {
    int model_calls = 0;
    double model_ms_total = 0.0;
    double model_ms_max = 0.0;
    double model_ms_last = 0.0;
    double ttft_ms_total = 0.0;
    int ttft_count = 0;
    double ttft_ms_last = -1.0;
    long long input_tokens = 0;
    long long cache_read_tokens = 0;
    long long cache_write_tokens = 0;
    long long output_tokens = 0;
    long long reasoning_tokens = 0;
    int last_input_tokens = 0;    // prompt of the latest call = context in use
    std::string last_effort;
    std::string last_variant;
    // Cost is fixed when each call runs, at that call's model prices, so a
    // later model switch or price edit never reprices earlier calls.
    UsageCost cost;
    int priced_calls = 0;
    int unpriced_calls = 0;       // the serving model had no cost in opencode.json
    int legacy_calls = 0;         // recorded before per-call pricing (no cost)
    std::map<std::string, ModelUsageTotals> by_model;  // "provider/model"

    void add(const ModelCallUsage& call);
    // Adds another session's totals (subagent roll-up); last_* stay ours.
    void merge(const SessionUsageStats& other);
    [[nodiscard]] bool empty() const { return model_calls == 0; }
    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] static SessionUsageStats from_json(const nlohmann::json& j);
};

// Calls of the subagents a session delegated to (its child sessions).
struct SubagentUsage {
    int sessions = 0;
    SessionUsageStats usage;
};

// Whole-session tokens at one model's prices. Only for sessions recorded
// before per-call pricing (see session_cost).
[[nodiscard]] UsageCost estimate_usage_cost(const SessionUsageStats& usage,
                                            const ModelInfo& model);

// The session cost to display. Normally the sum of per-call costs. Sessions
// whose calls all predate per-call pricing (or that only have turn totals,
// `turn_prompt_tokens` / `turn_completion_tokens`) are estimated at `current`
// model's prices and flagged estimated.
struct SessionCost {
    bool available = false;   // there is a figure to show
    bool estimated = false;   // legacy estimate at the current model's prices
    UsageCost parts;
    double total = 0.0;
    int unpriced_calls = 0;   // calls left out: model had no price
    int legacy_calls = 0;     // calls left out: recorded before per-call pricing
};
[[nodiscard]] SessionCost session_cost(const SessionUsageStats& usage,
                                       const ModelInfo* current,
                                       long long turn_prompt_tokens = 0,
                                       long long turn_completion_tokens = 0);

// API form (qcode-server /session/<id>/stats): the persisted totals plus
// derived figures (averages, cache hit, output speed) and session_cost.
[[nodiscard]] nlohmann::json usage_summary_json(const SessionUsageStats& usage,
                                                const ModelInfo* current,
                                                long long turn_prompt_tokens = 0,
                                                long long turn_completion_tokens = 0);

}  // namespace session
}  // namespace qcode
