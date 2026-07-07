#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>

namespace ai {
namespace tui {

ftxui::Element BashToolRender(const std::string& command, const std::string& output, int exit_code, bool is_running, const std::string& workdir = "");

}
}
