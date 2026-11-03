#pragma once

#include <string>
#include <string_view>

namespace qcode {
namespace tui {

// Standard base64 (RFC 4648, with padding).
std::string base64_encode(std::string_view data);

// OSC 52 "set clipboard" sequence for `text`. Inside tmux the sequence is
// wrapped in a DCS passthrough so it reaches the outer terminal.
std::string osc52_copy_sequence(std::string_view text, bool inside_tmux);

// Copies `text` to the clipboard: writes OSC 52 to `out_sequence` (the caller
// emits it with the next frame) and, when a desktop clipboard tool is on PATH
// (pbcopy, wl-copy, xclip, xsel), also pipes the text to it on a detached
// worker so terminals without OSC 52 (GNOME Terminal, Konsole) still copy.
void copy_to_clipboard(const std::string& text, std::string& out_sequence);

// Mouse reporting escape sequences. "Button-event" tracking (DEC 1002)
// reports motion only while a button is held, which is all drag-to-select
// needs. FTXUI turns on "any-event" tracking (1003), which reports every
// pointer move and so forced a full redraw whenever the mouse crossed the
// window.
inline constexpr std::string_view kMouseTrackingOn =
    "\033[?1003l\033[?1000h\033[?1002h\033[?1015h\033[?1006h";
inline constexpr std::string_view kMouseTrackingOff =
    "\033[?1003l\033[?1002l\033[?1000l\033[?1015l\033[?1006l";

}  // namespace tui
}  // namespace qcode
