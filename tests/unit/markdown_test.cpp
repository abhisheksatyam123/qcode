#include <qcode/render/markdown.h>
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

}
}
