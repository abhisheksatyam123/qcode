#include "ai/antigravity.h"
#include "ai/types/generate_options.h"
#include "providers/antigravity/antigravity_request_builder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace ai {
namespace antigravity {
namespace {

TEST(AntigravityClientTest, CreateClientIsValidAndNamed) {
  Client client = create_client(
      "dummy-token",
      "https://daily-cloudcode-pa.googleapis.com/v1internal");
  EXPECT_TRUE(client.is_valid());  // api_key non-empty
  EXPECT_EQ(client.provider_name(), "antigravity");
  EXPECT_EQ(client.default_model(), "gemini-3-flash");
}

TEST(AntigravityClientTest, RequestBuilderProducesAntigravityEnvelope) {
  AntigravityRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "gemini-3-flash";
  opts.prompt = "Hello";

  nlohmann::json req = builder.build_request_json(opts);

  // The Antigravity wire format wraps the Gemini request in a Vertex envelope
  // (not the raw OpenAI shape): "request" key + project/identity headers.
  ASSERT_TRUE(req.contains("request")) << req.dump();
  EXPECT_EQ(req["userAgent"].get<std::string>(), "antigravity");
  EXPECT_EQ(req["requestType"].get<std::string>(), "agent");
  EXPECT_EQ(req["model"].get<std::string>(),
            "gemini-3-flash-agent");  // gemini-3-flash -> *-agent mapping
  ASSERT_TRUE(req["request"].contains("contents"));
}

}  // namespace
}  // namespace antigravity
}  // namespace ai
