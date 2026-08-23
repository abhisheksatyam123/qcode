#pragma once

namespace qcode {

struct Usage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens = 0;
  int cached_prompt_tokens = 0;
  // Tokens the provider reports as reasoning/thinking output (subset of
  // completion_tokens; e.g. completion_tokens_details.reasoning_tokens).
  int reasoning_completion_tokens = 0;

  explicit Usage(int prompt = 0, int completion = 0, int cached = 0)
      : prompt_tokens(prompt),
        completion_tokens(completion),
        total_tokens(prompt + completion),
        cached_prompt_tokens(cached) {}

  bool is_valid() const {
    return total_tokens > 0 || (prompt_tokens > 0 && completion_tokens >= 0);
  }
};

}  // namespace qcode