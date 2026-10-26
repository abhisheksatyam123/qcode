#pragma once

// Adapter that makes the existing Antigravity client conform to the generic
// IProvider interface defined in abstract.h. This allows the rest of the code
// base to depend on IProvider rather than on concrete provider classes.

#include "abstract.h"
#include <qcode/providers/antigravity.h>
#include <qcode/core/client.h>
#include <qcode/core/generate_options.h>

namespace qcode {

class AntigravityProviderAdapter : public IProvider {
public:
    explicit AntigravityProviderAdapter(const std::string& api_key,
                                       const std::string& base_url = "")
        : client_(base_url.empty() ? qcode::antigravity::create_client(api_key)
                                   : qcode::antigravity::create_client(api_key, base_url)) {}

    // IProvider -----------------------------------------------------------
    std::string name() const override { return "antigravity"; }

    // Perform a simple request using the underlying Antigravity client.
    std::string request(const std::string& prompt) override {
        qcode::GenerateOptions opts;
        opts.prompt = prompt;
        auto result = client_.generate_text(opts);
        return result.text;
    }

private:
    qcode::Client client_;
};

} // namespace qcode
