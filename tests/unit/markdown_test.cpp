#include <qcode/ui/markdown.h>
#include <qcode/ui/message_render.h>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>
#include <stdio.h>

namespace qcode {
namespace test {

TEST(MarkdownTest, TableRenderingMultiLine) {
  std::string markdown =
      "| Header 1 | Header 2 |\n"
      "| --- | --- |\n"
      "| Line 1<br>Line 2<br />Line 3 | Another cell |";
  auto elements = render_markdown(markdown);
  auto element = ftxui::vbox(std::move(elements));
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  std::string output = screen.ToString();
  printf("Rendered table output (width 120):\n%s\n", output.c_str());
  
  // Verify that all 3 lines are present in the output
  EXPECT_NE(output.find("Line 1"), std::string::npos);
  EXPECT_NE(output.find("Line 2"), std::string::npos);
  EXPECT_NE(output.find("Line 3"), std::string::npos);
}

TEST(MarkdownTest, CodeBlockAndTableRendering) {
  std::string markdown =
      "Here is a code block:\n"
      "```cpp\n"
      "int main() {\n"
      "    std::cout << \"Hello, World!\" << std::endl;\n"
      "    return 0;\n"
      "}\n"
      "```\n"
      "\n"
      "And here is a markdown table:\n\n"
      "| Column A | Column B | Column C |\n"
      "| :--- | :---: | ---: |\n"
      "| Left | Center | Right |\n"
      "| Multi-line<br>text | Another cell | Value |\n";

  auto elements = render_markdown(markdown);
  auto element = ftxui::vbox(std::move(elements));
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  std::string output = screen.ToString();
  printf("Rendered Markdown output (width 80):\n%s\n", output.c_str());

  // Verify code block contents
  EXPECT_NE(output.find("int main() {"), std::string::npos);
  EXPECT_NE(output.find("Hello, World!"), std::string::npos);

  // Verify table contents
  EXPECT_NE(output.find("Column A"), std::string::npos); 
  EXPECT_NE(output.find("Left"), std::string::npos);
  EXPECT_NE(output.find("Center"), std::string::npos);
  EXPECT_NE(output.find("Right"), std::string::npos);
  EXPECT_NE(output.find("Multi-line"), std::string::npos);
  EXPECT_NE(output.find("text"), std::string::npos);
}

TEST(MessageRenderTest, ToolCallAndResultRendering) {
  qcode::ToolCallContentPart call_part(
      "call_123", "bash",
      nlohmann::json{
          {"command", "python3 -c \"print(1 << int('16'))\""},
          {"description", "Run bitwise shift in python"}});

  qcode::Message assistant_msg(qcode::kMessageRoleAssistant, {call_part});

  qcode::ToolResultContentPart result_part(
      "call_123",
      nlohmann::json{
          {"output", "65536\n"},
          {"metadata", {{"exit", 0}}}},
      false, 15.0);

  qcode::Message result_msg(qcode::kMessageRoleUser, {result_part});

  qcode::ChatState state;
  state.tool_collapse_state =
      std::make_shared<std::unordered_map<std::string, bool>>();
  (*state.tool_collapse_state)["call_123"] = true;
  std::vector<qcode::ProviderInfo> providers;

  // 1. Verify collapsed single-line view
  auto element_collapsed = qcode::render_message(
      assistant_msg, state, providers, -1, -1, "orange", &result_msg);
  auto screen_collapsed = ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fit(element_collapsed));
  ftxui::Render(screen_collapsed, element_collapsed);
  std::string output_collapsed = screen_collapsed.ToString();

  EXPECT_NE(output_collapsed.find("Run bitwise shift in python"), std::string::npos);
  EXPECT_NE(output_collapsed.find("✓"), std::string::npos);

  // 2. Verify expanded view with output
  (*state.tool_collapse_state)["call_123"] = false;
  auto element_expanded = qcode::render_message(
      assistant_msg, state, providers, -1, -1, "orange", &result_msg);
  auto screen_expanded = ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fit(element_expanded));
  ftxui::Render(screen_expanded, element_expanded);
  std::string output_expanded = screen_expanded.ToString();

  EXPECT_NE(output_expanded.find("Run bitwise shift in python"), std::string::npos);
  EXPECT_NE(output_expanded.find("python3 -c"), std::string::npos);
  EXPECT_NE(output_expanded.find("65536"), std::string::npos);
  EXPECT_NE(output_expanded.find("✓"), std::string::npos);
}

TEST(MessageRenderTest, UserMessageSingleRendering) {
  qcode::Message user_msg = qcode::Message::user("hi");
  qcode::ChatState state;
  std::vector<qcode::ProviderInfo> providers;

  auto element = qcode::render_message(user_msg, state, providers, -1, -1, "orange");
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  std::string output = screen.ToString();

  size_t count = 0;
  size_t pos = 0;
  while ((pos = output.find("hi", pos)) != std::string::npos) {
    count++;
    pos += 2;
  }
  EXPECT_EQ(count, 1u) << "User prompt text should be rendered exactly once, but output was:\n" << output;
}

TEST(MessageRenderTest, LongSystemJsonDoesNotRenderOneGlyphPerRow) {
  const std::string blob(400, 'x');
  qcode::Message sys = qcode::Message::system("Error: " + blob);
  qcode::ChatState state;
  std::vector<qcode::ProviderInfo> providers;
  auto element = qcode::render_message(sys, state, providers, -1, -1, "orange");
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                      ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  EXPECT_LT(screen.dimy(), 30) << screen.ToString();
}

}
}


