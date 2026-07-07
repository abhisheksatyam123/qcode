#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ai/types/generate_options.h"
#include "providers/openai/openai_request_builder.h"
#include "providers/openai/openai_response_parser.h"

namespace ai {
namespace test {

TEST(OpenAIGeminiTranslationTest, RequestBuilderToolTranslation) {
  openai::OpenAIRequestBuilder builder;
  
  GenerateOptions options;
  options.model = "gemini-3-flash";
  options.prompt = "Hello";
  
  // Add a tool
  Tool weather_tool("Get current weather", create_object_schema({{"location", "string"}}));
  options.tools["weather"] = weather_tool;
  options.tool_choice = ToolChoice::specific("weather");
  
  auto request_json = builder.build_request_json(options);
  
  // Verify envelope and request structure
  ASSERT_TRUE(request_json.contains("request"));
  auto gemini_req = request_json["request"];
  
  // Verify tools translation
  ASSERT_TRUE(gemini_req.contains("tools"));
  auto decls = gemini_req["tools"][0]["functionDeclarations"];
  ASSERT_EQ(decls.size(), 1);
  EXPECT_EQ(decls[0]["name"], "weather");
  EXPECT_EQ(decls[0]["description"], "Get current weather");
  
  // Verify tool_choice to toolConfig translation
  ASSERT_TRUE(gemini_req.contains("toolConfig"));
  auto config = gemini_req["toolConfig"]["functionCallingConfig"];
  EXPECT_EQ(config["mode"], "ANY");
  ASSERT_TRUE(config.contains("allowedFunctionNames"));
  EXPECT_EQ(config["allowedFunctionNames"][0], "weather");
}

TEST(OpenAIGeminiTranslationTest, RequestBuilderToolChoiceAuto) {
  openai::OpenAIRequestBuilder builder;
  
  GenerateOptions options;
  options.model = "gemini-3-flash";
  options.prompt = "Hello";
  
  Tool weather_tool("Get current weather", create_object_schema({{"location", "string"}}));
  options.tools["weather"] = weather_tool;
  options.tool_choice = ToolChoice::auto_choice();
  
  auto request_json = builder.build_request_json(options);
  auto gemini_req = request_json["request"];
  
  ASSERT_TRUE(gemini_req.contains("toolConfig"));
  auto config = gemini_req["toolConfig"]["functionCallingConfig"];
  EXPECT_EQ(config["mode"], "AUTO");
}

TEST(OpenAIGeminiTranslationTest, RequestBuilderToolChoiceNone) {
  openai::OpenAIRequestBuilder builder;
  
  GenerateOptions options;
  options.model = "gemini-3-flash";
  options.prompt = "Hello";
  
  Tool weather_tool("Get current weather", create_object_schema({{"location", "string"}}));
  options.tools["weather"] = weather_tool;
  options.tool_choice = ToolChoice::none();
  
  auto request_json = builder.build_request_json(options);
  auto gemini_req = request_json["request"];
  
  ASSERT_TRUE(gemini_req.contains("toolConfig"));
  auto config = gemini_req["toolConfig"]["functionCallingConfig"];
  EXPECT_EQ(config["mode"], "NONE");
}

TEST(OpenAIGeminiTranslationTest, ResponseParserGeminiFunctionCall) {
  openai::OpenAIResponseParser parser;
  
  // Construct a mocked Gemini response with function calls
  nlohmann::json gemini_response = {
    {"candidates", {
      {
        {"content", {
          {"role", "model"},
          {"parts", {
            {
              {"functionCall", {
                {"name", "calculator"},
                {"args", {
                  {"operation", "add"},
                  {"a", 10.0},
                  {"b", 5.0}
                }}
              }}
            }
          }}
        }},
        {"finishReason", "FUNCTION_CALL"}
      }
    }},
    {"responseId", "test-response-id"},
    {"modelVersion", "gemini-3-flash-model"}
  };
  
  // Wrap in envelope under "response" key if needed, or pass directly
  nlohmann::json response_env = {
    {"response", gemini_response}
  };
  
  auto result = parser.parse_success_completion_response(response_env);
  
  EXPECT_EQ(result.id, "test-response-id");
  EXPECT_EQ(result.model, "gemini-3-flash-model");
  EXPECT_EQ(result.finish_reason, kFinishReasonToolCalls);
  
  ASSERT_EQ(result.tool_calls.size(), 1);
  EXPECT_EQ(result.tool_calls[0].tool_name, "calculator");
  EXPECT_EQ(result.tool_calls[0].arguments["operation"], "add");
  EXPECT_EQ(result.tool_calls[0].arguments["a"], 10.0);
}

TEST(OpenAIGeminiTranslationTest, ResponseParserGeminiPluralFunctionCalls) {
  openai::OpenAIResponseParser parser;
  
  nlohmann::json gemini_response = {
    {"candidates", {
      {
        {"content", {
          {"role", "model"},
          {"parts", {
            {
              {"functionCalls", {
                {
                  {"name", "calculator"},
                  {"args", {{"operation", "add"}, {"a", 10.0}, {"b", 5.0}}}
                },
                {
                  {"name", "bash"},
                  {"args", {{"command", "ls"}}}
                }
              }}
            }
          }}
        }},
        {"finishReason", "FUNCTION_CALL"}
      }
    }},
    {"responseId", "test-response-id"},
    {"modelVersion", "gemini-3-flash-model"}
  };
  
  nlohmann::json response_env = {
    {"response", gemini_response}
  };
  
  auto result = parser.parse_success_completion_response(response_env);
  
  ASSERT_EQ(result.tool_calls.size(), 2);
  EXPECT_EQ(result.tool_calls[0].tool_name, "calculator");
  EXPECT_EQ(result.tool_calls[1].tool_name, "bash");
  EXPECT_EQ(result.tool_calls[1].arguments["command"], "ls");
}

TEST(OpenAIGeminiTranslationTest, RequestBuilderToolResponseMerging) {
  openai::OpenAIRequestBuilder builder;
  
  GenerateOptions options;
  options.model = "gemini-3-flash";
  
  // Create a tool result message containing two tool results (simulates two parallel tool runs finishing)
  std::vector<ToolResultContentPart> results = {
    ToolResultContentPart("call_calculator_0", {{"result", 15.0}}),
    ToolResultContentPart("call_bash_1", {{"output", "hello\n"}})
  };
  options.messages.push_back(Message::tool_results(results));
  
  auto request_json = builder.build_request_json(options);
  
  ASSERT_TRUE(request_json.contains("request"));
  auto gemini_req = request_json["request"];
  
  ASSERT_TRUE(gemini_req.contains("contents"));
  auto contents = gemini_req["contents"];
  
  // We expect exactly ONE message turn in contents (the two tool responses merged)
  ASSERT_EQ(contents.size(), 1);
  EXPECT_EQ(contents[0]["role"], "user");
  
  auto parts = contents[0]["parts"];
  // We expect exactly TWO parts in the single turn
  ASSERT_EQ(parts.size(), 2);
  
  EXPECT_TRUE(parts[0].contains("functionResponse"));
  EXPECT_EQ(parts[0]["functionResponse"]["name"], "calculator");
  EXPECT_EQ(parts[0]["functionResponse"]["response"]["result"], 15.0);
  
  EXPECT_TRUE(parts[1].contains("functionResponse"));
  EXPECT_EQ(parts[1]["functionResponse"]["name"], "bash");
  EXPECT_EQ(parts[1]["functionResponse"]["response"]["output"], "hello\n");
}

TEST(OpenAIGeminiTranslationTest, RequestBuilderConsecutiveUserMessagesMerging) {
  openai::OpenAIRequestBuilder builder;
  
  GenerateOptions options;
  options.model = "gemini-3-flash";
  
  options.messages.push_back(Message::user("First message"));
  options.messages.push_back(Message::user("Second message"));
  
  auto request_json = builder.build_request_json(options);
  auto gemini_req = request_json["request"];
  auto contents = gemini_req["contents"];
  
  // We expect exactly ONE message turn in contents (the two user messages merged)
  ASSERT_EQ(contents.size(), 1);
  EXPECT_EQ(contents[0]["role"], "user");
  
  auto parts = contents[0]["parts"];
  ASSERT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0]["text"], "First message");
  EXPECT_EQ(parts[1]["text"], "Second message");
}

} // namespace test
} // namespace ai
