#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace qcode {

struct ModelInfo;

namespace session {

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
    std::string last_effort;
    std::string last_variant;

    void add(const ModelCallUsage& call);
    [[nodiscard]] bool empty() const { return model_calls == 0; }
    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] static SessionUsageStats from_json(const nlohmann::json& j);
};

// Session cost at the model's opencode.json prices (USD per 1M tokens:
// cost.input/output/cache_read/cache_write). Cache reads/writes without a
// configured price are billed at the input price. priced=false when the
// config has no input/output price for the model.
struct UsageCost {
    bool priced = false;
    double input = 0.0;        // uncached input
    double cache_read = 0.0;
    double cache_write = 0.0;
    double output = 0.0;
    [[nodiscard]] double total() const { return input + cache_read + cache_write + output; }
};
[[nodiscard]] UsageCost estimate_usage_cost(const SessionUsageStats& usage,
                                            const ModelInfo& model);

}  // namespace session
}  // namespace qcode
