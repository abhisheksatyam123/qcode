#pragma once

#include "providers/openai/openai_stream.h"

namespace qcode {
namespace antigravity {

class AntigravityStreamImpl final : public openai::OpenAIStreamImpl {
 public:
  AntigravityStreamImpl()
      : OpenAIStreamImpl(openai::StreamProtocol::kGeminiEnvelope) {}
};

}  // namespace antigravity
}  // namespace qcode
