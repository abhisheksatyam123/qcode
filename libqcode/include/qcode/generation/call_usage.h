#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/bus_port.h>
#include <qcode/core/generate_options.h>
#include <qcode/session/usage_stats.h>

#include <string>

namespace qcode {

// Usage key for a call: the opencode.json model id, else the wire id.
[[nodiscard]] std::string usage_model_id(const ModelInfo* model_info,
                                         const std::string& wire_model);

// Usage of one finished model call made with `request` (wire effort, variant
// and model as sent). Thinking tokens fall back to reasoning chars / 4 when
// the provider reports none.
[[nodiscard]] session::ModelCallUsage model_call_usage(
    const GenerateOptions& request, const GenerateResult& result,
    const std::string& provider_id, const ModelInfo* model_info,
    double model_ms);

// One finished model call: priced at the serving model's opencode.json rates
// as it runs (a later model switch never reprices it), persisted to the
// session's usage stats, then published as StepLatency when `bus` is set
// (live mirrors: TUI Stats tab, WebUI). Persist first: the mirror reloads
// from the DB on session switches and must not miss (or double count) it.
void record_model_call(bus::BusPort* bus, const std::string& session_id,
                       const ModelInfo* model_info, session::ModelCallUsage call,
                       int step, bool streamed, bool ok);

}  // namespace qcode
