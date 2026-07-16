## Systems
- Goal: Fix the double "Error: Error:" prefix in stored session messages and investigate "Error from provider (Console): Upstream request failed".
- Findings:
  - `libqcode/src/store/store.cpp` receives `ErrorOccurred` events and prepends `"Error: "` to the message before appending it to the chat history and saving to SQLite.
  - `libqcode/src/chat/chat_bus.cpp` already prepends `"Error: "` to error messages (e.g. `err_str = "Error: " + gen_result.error_message()`), causing a duplicate `"Error: Error: "` prefix in the saved chat history.
  - The message `"Error from provider (Console): Upstream request failed"` is returned by the OpenCode Zen API (or the OpenRouter proxy) when the downstream model provider (Console) is unreachable or fails.

## Tasks
- [x] Fix `libqcode/src/store/store.cpp` to avoid prepending `"Error: "` if the event message already starts with `"Error: "` or `"Exception: "`.
- [x] Rebuild and run the C++ unit tests to verify changes.
