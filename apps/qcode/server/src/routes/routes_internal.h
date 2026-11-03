#pragma once

#include "server_routes.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <initializer_list>
#include <memory>
#include <string>
#include <iterator>
#include <utility>
#include <vector>

namespace qcode {
namespace server {

// Request-body validation. Handlers call these at the top, so a bad body is a
// 400 before any work starts. Anything that throws later is a server bug (500).

inline void send_error(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(nlohmann::json({{"error", message}}).dump(), "application/json");
}

// Parses a body that must be a JSON object. An empty body counts as {} when
// allow_empty is set. On failure, sets a 400 and returns false.
inline bool parse_object_body(const std::string& raw, httplib::Response& res,
                              nlohmann::json& out, bool allow_empty = false) {
    if (raw.empty() && allow_empty) {
        out = nlohmann::json::object();
        return true;
    }
    nlohmann::json parsed = nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        send_error(res, 400, "invalid JSON");
        return false;
    }
    if (!parsed.is_object()) {
        send_error(res, 400, "request body must be a JSON object");
        return false;
    }
    // A null field means "absent", so drop it before handlers call value().
    for (auto it = parsed.begin(); it != parsed.end();) {
        it = it->is_null() ? parsed.erase(it) : std::next(it);
    }
    out = std::move(parsed);
    return true;
}

// Session-scoped paths answer a malformed id with 400 and an unknown one with
// 404. Returns true when it wrote a rejection. Must run in the pre-routing hook.
bool rejected_session_path(const httplib::Request& req, httplib::Response& res);

enum class FieldType { String, Number, Boolean, Array };

// Each listed field, when present and not null, must have the given JSON type.
inline bool check_fields(const nlohmann::json& body, httplib::Response& res,
                         std::initializer_list<std::pair<const char*, FieldType>> fields) {
    for (const auto& [name, type] : fields) {
        const auto it = body.find(name);
        if (it == body.end() || it->is_null()) continue;
        bool ok = false;
        switch (type) {
            case FieldType::String:  ok = it->is_string(); break;
            case FieldType::Number:  ok = it->is_number(); break;
            case FieldType::Boolean: ok = it->is_boolean(); break;
            case FieldType::Array:   ok = it->is_array(); break;
        }
        if (!ok) {
            send_error(res, 400, std::string("field '") + name + "' has the wrong type");
            return false;
        }
    }
    return true;
}

// A provider/model resolved against the configured providers.
struct ResolvedModel {
    const qcode::ProviderInfo* provider = nullptr;
    const qcode::ModelInfo* model = nullptr;  // null when the provider lists no models
};

// Resolve `provider` / `model` by id or name. An empty provider means the
// first configured one; an empty model the provider's first model. A *named*
// model that is not in the provider's catalog yields a null model and a 400
// on `res` listing that provider's model ids. A provider that is named but
// not configured yields a null provider and a 400 on `res` listing the
// configured provider ids.
ResolvedModel resolve_provider_model(const std::vector<qcode::ProviderInfo>& providers,
                                     const std::string& provider,
                                     const std::string& model,
                                     httplib::Response& res);

void register_system_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void register_session_routes(
    httplib::Server& svr,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void register_session_ops_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers);

void register_fs_routes(
    httplib::Server& svr);

}  // namespace server
}  // namespace qcode
