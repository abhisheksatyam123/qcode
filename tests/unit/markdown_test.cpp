#include <qcode/ui/themes.h>
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

static std::string strip_ansi(const std::string& in) {
  std::string out;
  bool in_escape = false;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\033') {
      in_escape = true;
    } else if (in_escape && (in[i] == 'm' || in[i] == 'K' || in[i] == 'H' || in[i] == 'J')) {
      in_escape = false;
    } else if (!in_escape) {
      out.push_back(in[i]);
    }
  }
  return out;
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

  std::string clean = strip_ansi(output);
  // Verify code block contents
  EXPECT_NE(clean.find("int main() {"), std::string::npos);
  EXPECT_NE(clean.find("Hello, World!"), std::string::npos);

  // Verify table contents
  EXPECT_NE(clean.find("Column A"), std::string::npos); 
  EXPECT_NE(clean.find("Left"), std::string::npos);
  EXPECT_NE(clean.find("Center"), std::string::npos);
  EXPECT_NE(clean.find("Right"), std::string::npos);
  EXPECT_NE(clean.find("Multi-line"), std::string::npos);
  EXPECT_NE(clean.find("text"), std::string::npos);
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

TEST(ThemeTest, OpencodePalettesAllPresentAndValid) {
  const std::vector<std::string> opencode_themes = {
      "opencode", "tokyonight", "nord", "gruvbox", "catppuccin",
      "rosepine", "vesper", "one-dark", "aura", "zenburn",
      "cobalt2", "synthwave84", "osaka-jade", "matrix", "flexoki",
      "material", "ayu", "everforest", "kanagawa", "monokai",
      "github", "solarized", "dracula", "carbonfox", "catppuccin-frappe",
      "catppuccin-macchiato", "cursor", "lucent-orng", "mercury",
      "nightowl", "orng", "palenight", "vercel"
  };

  EXPECT_EQ(opencode_themes.size(), 33u);

  for (const auto& name : opencode_themes) {
    EXPECT_TRUE(is_known_theme(name)) << "Theme " << name << " should be known";
    const auto* p = find_theme_palette(name);
    ASSERT_NE(p, nullptr) << "Theme " << name << " palette missing";
    EXPECT_STREQ(p->name, name.c_str());
    EXPECT_NE(p->accent, 0u);
    EXPECT_NE(p->accent2, 0u);
    EXPECT_NE(p->success, 0u);
    EXPECT_NE(p->error, 0u);
    EXPECT_NE(p->muted, 0u);
    EXPECT_NE(p->md_text, 0u);
    EXPECT_NE(p->md_heading, 0u);
    EXPECT_NE(p->md_code, 0u);
    EXPECT_NE(p->md_link, 0u);
    EXPECT_NE(p->md_link_text, 0u);
    EXPECT_NE(p->syn_comment, 0u);
    EXPECT_NE(p->syn_keyword, 0u);
    EXPECT_NE(p->syn_function, 0u);
    EXPECT_NE(p->syn_string, 0u);
  }

  const auto entries = builtin_theme_entries();
  for (const auto& name : opencode_themes) {
    bool found = false;
    for (const auto& entry : entries) {
      if (entry.name == name) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "Theme " << name << " missing from builtin_theme_entries";
  }
}

TEST(MarkdownTest, RendersHeadingsListsLinksAndImages) {
  std::string md =
      "# Main Title\n"
      "## Subtitle\n"
      "Here is **bold text** and *italic text* and `code` inline.\n"
      "- Unordered bullet\n"
      "1. Ordered item\n"
      "[OpenCode Docs](https://opencode.ai)\n"
      "![Alt text](https://example.com/pic.png)\n"
      "> Blockquote line\n"
      "---\n";

  auto elements = render_markdown(md, "opencode");
  EXPECT_FALSE(elements.empty());
  auto element = ftxui::vbox(std::move(elements));
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  std::string output = screen.ToString();
  std::string clean = strip_ansi(output);

  EXPECT_NE(clean.find("Main Title"), std::string::npos);
  EXPECT_NE(clean.find("Subtitle"), std::string::npos);
  EXPECT_NE(clean.find("bold text"), std::string::npos);
  EXPECT_NE(clean.find("italic text"), std::string::npos);
  EXPECT_NE(clean.find("code"), std::string::npos);
  EXPECT_NE(clean.find("Unordered bullet"), std::string::npos);
  EXPECT_NE(clean.find("Ordered item"), std::string::npos);
  EXPECT_NE(clean.find("OpenCode Docs"), std::string::npos);
  EXPECT_NE(clean.find("https://opencode.ai"), std::string::npos);
  EXPECT_NE(clean.find("[image:"), std::string::npos);
  EXPECT_NE(clean.find("Blockquote line"), std::string::npos);
}

TEST(MarkdownTest, SyntaxHighlightingPythonAndBash) {
  std::string md =
      "```python\n"
      "# Python comment\n"
      "def greet(name):\n"
      "    return f\"hello {name}\"\n"
      "```\n"
      "```bash\n"
      "# shell script\n"
      "echo 'done'\n"
      "exit 0\n"
      "```\n";

  auto elements = render_markdown(md, "opencode");
  EXPECT_FALSE(elements.empty());
  auto element = ftxui::vbox(std::move(elements));
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  std::string clean = strip_ansi(screen.ToString());

  EXPECT_NE(clean.find("‹python›"), std::string::npos);
  EXPECT_NE(clean.find("def greet(name):"), std::string::npos);
  EXPECT_NE(clean.find("‹bash›"), std::string::npos);
  EXPECT_NE(clean.find("echo 'done'"), std::string::npos);
}
}
}


