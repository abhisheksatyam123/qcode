#pragma once

#include "abstract.h"
#include <qcode/providers/cursor.h>
#include <qcode/core/client.h>
#include <qcode/core/generate_options.h>

namespace qcode {

class CursorProviderAdapter : public IProvider {
public:
    explicit CursorProviderAdapter(const std::string& access_token,
                                  const std::string& session_token = "")
        : client_(session_token.empty() ? qcode::cursor::create_client(access_token)
                                       : qcode::cursor::create_client(access_token, session_token)) {}

    std::string name() const override { return "cursor"; }

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
