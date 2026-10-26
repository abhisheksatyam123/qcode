#pragma once

#include "abstract.h"
#include <qcode/providers/anthropic.h>
#include <qcode/core/client.h>
#include <qcode/core/generate_options.h>

namespace qcode {

class AnthropicProviderAdapter : public IProvider {
public:
    explicit AnthropicProviderAdapter(const std::string& api_key,
                                     const std::string& base_url = "")
        : client_(base_url.empty() ? (api_key.empty() ? qcode::anthropic::create_client()
                                                      : qcode::anthropic::create_client(api_key))
                                   : qcode::anthropic::create_client(api_key, base_url)) {}

    std::string name() const override { return "anthropic"; }

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
