## Systems

**OpenCode-style TUI — BUILD COMPLETE ✅ — LAYOUT CLEANED UP**

### Layout (Current)

```
┌─────────────────────────────────────────────────────────────┐
│ QCODE   Chat │ Stats │ Files          model  │ 123 tok │ ⠋  │  ← single strip: tabs + model + tokens + spinner
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  messages...                                                │
│                                                             │
│ ❯ input here______________                                   │  ← clean input bar: no model badge, no spinner
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

- **No duplicate spinner** — only one spinner in the header strip
- **No model badge in input bar** — moved to header (compact)
- **No separate footer** — everything in one header strip
- **Pure text area** — chat body has only messages and clean input

### Architecture

| Layer | Files | Status |
|---|---|---|
| Bus | `bus/event.h`, `port.h`, `impl.h/cpp` | ✅ Thread-safe pub/sub |
| Contract | `contract/event.h`, `identity.h` | ✅ 13 typed events |
| Store | `store.h/cpp` | ✅ Reactive state + toasts |
| Chat | `chat_bus.h/cpp` | ✅ Bus-aware generation |
| Views | `views.cpp` | ✅ Clean layout, header strip |
| App | `main.cpp` | ✅ Refactored, bus-wired |

### Key Visual Fixes
- [x] Duplicate spinner removed (was in body + footer → now only in header)
- [x] Model badge removed from input bar (now in header strip)
- [x] Footer removed (merged into header strip)
- [x] Token count shown compactly in header (`123 tok`)
- [x] Spinner shown compactly in header (`⠋ gen...`)
- [x] Clean prompt bar: just `❯ input`

## Tasks (all complete)
- [x] Message bus (bus/event.h, port.h, impl.h/cpp)
- [x] Contract layer (13 event types + identity)
- [x] Store layer (reactive + toast)
- [x] Bus-aware chat generation
- [x] Refactored main.cpp with bus+store
- [x] Toast overlay rendering
- [x] Build succeeds
- [x] UI cleanup: single header strip, no duplicate spinner, no footer
