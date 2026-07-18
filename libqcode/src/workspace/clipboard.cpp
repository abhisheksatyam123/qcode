#include <qcode/workspace/clipboard.h>

#include <cstdio>
#include <string>

namespace qcode {
namespace tui {

void copy_to_clipboard(const std::string& text) {
    // Try xclip
    FILE* pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (!pipe) {
        // Try xsel
        pipe = popen("xsel --clipboard --input 2>/dev/null", "w");
    }
    if (pipe) {
        fwrite(text.c_str(), 1, text.length(), pipe);
        pclose(pipe);
    }
}

} // namespace tui
} // namespace qcode
