// Host-only: C++20 named modules (`import qcode.*`). Android keeps headers.
// Include the test harness before `import` so libstdc++ is not parsed twice.

#include <gtest/gtest.h>

#include <string_view>

import qcode;
import qcode.config;
import qcode.transform;

namespace {

TEST(CxxModulesTest, ConfigPathFromImport) {
  const auto path = qcode::config_path();
  EXPECT_FALSE(path.empty());
  EXPECT_NE(path.find("opencode.json"), std::string::npos);
}

TEST(CxxModulesTest, VariantHelpersFromImport) {
  qcode::ModelInfo model{};
  model.reasoning = true;
  model.reasoning_efforts = {"low", "high"};
  model.reasoning_default = "high";

  EXPECT_EQ(qcode::ProviderTransform::default_variant(model), "high");
  EXPECT_TRUE(qcode::ProviderTransform::is_allowed_variant(model, "low"));
  EXPECT_FALSE(qcode::ProviderTransform::is_allowed_variant(model, "max"));
  EXPECT_EQ(qcode::ProviderTransform::clamp_variant(model, "max"), "high");
  EXPECT_EQ(qcode::ProviderTransform::next_variant(model, "off"), "low");
}

TEST(CxxModulesTest, ChatTransportFromImport) {
  using qcode::ProviderTransform::ChatTransport;
  EXPECT_EQ(qcode::ProviderTransform::chat_transport_for("https://openrouter.ai/api/v1"),
            ChatTransport::kOpenRouter);
  EXPECT_EQ(qcode::ProviderTransform::zen_api_protocol("ox-alpha"),
            "chat_completions");
}

TEST(CxxModulesTest, StringViewableConcept) {
  static_assert(qcode::StringViewable<std::string_view>);
  static_assert(qcode::StringViewable<const char*>);
  static_assert(!qcode::StringViewable<int>);
}

}  // namespace
