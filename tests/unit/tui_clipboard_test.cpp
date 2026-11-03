#include <clipboard.h>

#include <gtest/gtest.h>

namespace qcode {
namespace tui {
namespace {

TEST(TuiClipboardTest, Base64MatchesRfc4648Vectors) {
    EXPECT_EQ(base64_encode(""), "");
    EXPECT_EQ(base64_encode("f"), "Zg==");
    EXPECT_EQ(base64_encode("fo"), "Zm8=");
    EXPECT_EQ(base64_encode("foo"), "Zm9v");
    EXPECT_EQ(base64_encode("foob"), "Zm9vYg==");
    EXPECT_EQ(base64_encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(base64_encode("foobar"), "Zm9vYmFy");
    EXPECT_EQ(base64_encode("\xff\xfe"), "//4=");
}

TEST(TuiClipboardTest, Osc52SequenceWrapsBase64Payload) {
    EXPECT_EQ(osc52_copy_sequence("hi", false), "\033]52;c;aGk=\a");
}

TEST(TuiClipboardTest, Osc52InsideTmuxUsesPassthroughWithDoubledEscapes) {
    EXPECT_EQ(osc52_copy_sequence("hi", true),
              "\033Ptmux;\033\033]52;c;aGk=\a\033\\");
}

}  // namespace
}  // namespace tui
}  // namespace qcode
