## Plan
1. Update `Tools::format_tool_call` and `Tools::format_tool_result` to return more structured information or better formatted strings if parsing is too hard, but wait, the instructions specifically said:
   "The `format_tool_call` and `format_tool_result` in `src/tui/tools.cpp` currently produce flat text. The rendering should instead parse the content and use FTXUI elements (text, paragraph, color, bold, etc.) for structured rich rendering."
   So I should update these functions to return something that `views.cpp` can render nicely, or update `views.cpp` to parse the string. Given the current structure, let's update `views.cpp` to construct the FTXUI elements directly based on the type, and keep `format_tool_call/result` for the content part.
2. In `src/tui/chat.cpp`, use a placeholder when tools are enabled to allow streaming the final assistant response.
