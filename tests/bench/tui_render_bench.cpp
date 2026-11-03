// Headless TUI frame benchmark.
//
// Builds a chat history of N markdown-heavy messages, then renders the chat
// tab the way the interactive loop does (render_view -> Render -> ToString)
// while the last assistant message grows by one token per frame, the way it
// does while a model streams. Reports per-frame time and peak RSS.
//
//   qcode_tui_bench [messages=400] [frames=300] [width=140] [height=45]
//
// Run it before and after a TUI change to see what the change bought.

#include <views.h>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::string sample_markdown(int i) {
    std::string s = "## Step " + std::to_string(i) + "\n\n";
    s += "Here is a paragraph explaining **what** changed and *why* it "
         "matters. It is long enough to wrap across several terminal rows so "
         "the layout engine has real work to do on every frame.\n\n";
    s += "- first bullet with `inline code`\n- second bullet\n- third bullet "
         "that is noticeably longer than the others and wraps\n\n";
    s += "```cpp\nint main() {\n  for (int i = 0; i < 10; ++i) {\n"
         "    std::printf(\"%d\\n\", i);\n  }\n  return 0;\n}\n```\n\n";
    s += "| col a | col b |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n";
    return s;
}

long current_rss_kb() {
    long pages = 0;
    long resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

long peak_rss_kb() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

}  // namespace

int main(int argc, char** argv) {
    const int messages = argc > 1 ? std::atoi(argv[1]) : 400;
    const int frames = argc > 2 ? std::atoi(argv[2]) : 300;
    const int width = argc > 3 ? std::atoi(argv[3]) : 140;
    const int height = argc > 4 ? std::atoi(argv[4]) : 45;
    ftxui::Terminal::SetFallbackSize({width, height});

    qcode::ChatState state;
    *state.session_id = "bench-session";
    for (int i = 0; i < messages; ++i) {
        if (i % 2 == 0) {
            state.messages_history->push_back(
                qcode::Message::user("Question number " + std::to_string(i) +
                                     ": please explain the next step."));
        } else {
            state.messages_history->push_back(
                qcode::Message::assistant(sample_markdown(i)));
        }
    }
    state.messages_history->push_back(qcode::Message::assistant(""));
    state.is_generating->store(true);

    qcode::ProviderInfo provider;
    provider.name = "bench";
    provider.id = "bench";
    qcode::ModelInfo model;
    model.name = "bench-model";
    model.id = "bench-model";
    model.context_window = 200000;
    provider.models.push_back(model);
    const std::vector<qcode::ProviderInfo> providers{provider};

    std::vector<std::string> tabs{"Chat", "Files", "Stats", "Subagents"};
    auto tab_toggle = ftxui::Toggle(&tabs, &state.tab_selected);
    std::string prompt;
    auto input = ftxui::Input(&prompt, "Ask anything...");
    const std::vector<qcode::SlashCommand> slash;

    auto frame = [&] {
        auto doc = qcode::tui::render_view(
            state, providers, 0, 0, true, prompt, false, 0, slash,
            false, 0, {}, "", false, 0, {}, "", false, 0, {}, "", false, 0, {},
            "", false, tab_toggle, state.scroll_line, input);
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                            ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, doc);
        return screen.ToString().size();
    };

    using clock = std::chrono::steady_clock;
    // Warm-up: fills the per-message cache like a session that is open.
    const auto w0 = clock::now();
    frame();
    const double first_ms =
        std::chrono::duration<double, std::milli>(clock::now() - w0).count();
    for (int i = 0; i < 2; ++i) frame();

    std::vector<double> ms;
    ms.reserve(frames);
    size_t bytes = 0;
    for (int f = 0; f < frames; ++f) {
        auto& last = state.messages_history->back();
        last = qcode::Message::assistant(last.get_text() + "token" +
                                         std::to_string(f) + " ");
        const auto t0 = clock::now();
        bytes += frame();
        ms.push_back(std::chrono::duration<double, std::milli>(clock::now() - t0)
                         .count());
    }
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double v : ms) sum += v;
    std::printf(
        "messages=%d frames=%d size=%dx%d  avg=%.3fms p50=%.3fms p95=%.3fms "
        "max=%.3fms  first=%.1fms  out=%zuB/frame  rss=%ldKB peak_rss=%ldKB\n",
        messages, frames, width, height, sum / ms.size(), ms[ms.size() / 2],
        ms[ms.size() * 95 / 100], ms.back(), first_ms, bytes / ms.size(), current_rss_kb(),
        peak_rss_kb());
    return 0;
}
