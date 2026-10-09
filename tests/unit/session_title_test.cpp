// Session titles from a small model (opencode's title agent).

#include <qcode/session/session_title.h>

#include <gtest/gtest.h>

namespace qcode {
namespace {

TEST(SessionTitleTest, DefaultTitlesAreTheOnesQcodeMadeUp) {
  EXPECT_TRUE(session_title::is_default_title("", "abc"));
  EXPECT_TRUE(session_title::is_default_title("abc", "abc"));
  EXPECT_TRUE(session_title::is_default_title("Session - gemini-3.8-flash", "abc"));
  EXPECT_FALSE(session_title::is_default_title("Fix compaction cache", "abc"));
}

TEST(SessionTitleTest, CleansModelOutputToOneShortLine) {
  EXPECT_EQ(session_title::clean_title("Fix auto-compaction threshold\n"),
            "Fix auto-compaction threshold");
  EXPECT_EQ(session_title::clean_title("<think>user wants a title</think>\n\n\"Debug SSE parser.\""),
            "Debug SSE parser");
  EXPECT_EQ(session_title::clean_title("## Title: **Port lexer to C++**"), "Port lexer to C++");
  EXPECT_EQ(session_title::clean_title("  \n  "), "");
  const std::string longer = session_title::clean_title(
      "Investigate why the release build of qcode takes two hours on a loaded machine today");
  EXPECT_LE(longer.size(), 60u);
  EXPECT_NE(longer.back(), ' ');  // cut at a word boundary
}

TEST(SessionTitleTest, CandidatesAreSmallModelThenTheSessionModel) {
  ProviderInfo zen;
  zen.id = "opencode";
  zen.name = "OpenCode Zen";
  zen.api_url = "https://opencode.ai/zen/v1";
  zen.api_key = "public";
  ModelInfo big;
  big.id = big.name = "nemotron-3-ultra-free";
  ModelInfo fast;
  fast.id = fast.name = "ling-3.1-flash-free";
  zen.models = {big, fast};
  ProviderInfo paid;
  paid.id = "anthropic";
  paid.name = "Anthropic";
  paid.api_url = "https://api.anthropic.com";
  paid.api_key = "k";
  ModelInfo haiku;
  haiku.id = haiku.name = "claude-haiku-5-5";
  paid.models = {haiku};
  const std::vector<ProviderInfo> providers{paid, zen};

  // No code picks models: nothing configured and no session model, no title.
  EXPECT_TRUE(session_title::candidates(providers, "").empty());

  auto c = session_title::candidates(providers, "anthropic/claude-haiku-5-5",
                                     "opencode:nemotron-3-ultra-free");
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0].second->id, "claude-haiku-5-5");
  EXPECT_EQ(c[1].second->id, "nemotron-3-ultra-free");

  c = session_title::candidates(providers, "", "opencode:ling-3.1-flash-free");
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].second->id, "ling-3.1-flash-free");
}

}  // namespace
}  // namespace qcode
