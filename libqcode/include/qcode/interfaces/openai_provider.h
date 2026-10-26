#pragma once

#include "abstract.h"
#include <qcode/providers/openai.h>
#include <qcode/core/client.h>
#include <qcode/core/generate_options.h>

namespace qcode {

class OpenAIProviderAdapter : public IProvider {
public:
    explicit OpenAIProviderAdapter(const std::string& api_key,
                                  const std::string& base_url = "")
        : client_(base_url.empty() ? (api_key.empty() ? qcode::openai::create_client()
                                                      : qcode::openai::create_client(api_key))
                                   : qcode::openai::create_client(api_key, base_url)) {}

    std::string name() const override { return "openai"; }

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
