# Gemini web bridge (Hydrogen / Argon)

Gemini 4's web modes — **4 Hydrogen**, **4 Argon**, **4 Carbon** — have no public API
and are **not** served by the Antigravity / CloudCode endpoint
(`v1internal:fetchAvailableModels` lists 19 models, none of them `gemini-4-*`;
`generateContent` returns `404 NOT_FOUND`). The only way to use them is to drive
`gemini.google.com` in a logged-in browser.

This repo ships two tools for that:

| File | Purpose |
| --- | --- |
| `tools/gemini_web_task.py` | One prompt, one reply. CLI, headless Firefox. |
| `tools/gemini_web_bridge.py` | OpenAI-compatible HTTP server (`/v1/models`, `/v1/chat/completions`, SSE). |

Both reuse a copy of a logged-in Firefox profile (`~/qcode-browser/ff-profile`) and a
standalone `geckodriver` (`~/qcode-browser/geckodriver`).

## One-shot CLI

```bash
python3 tools/gemini_web_task.py "review this diff for off-by-one bugs: ..."   # Hydrogen
python3 tools/gemini_web_task.py --mode argon "write a regex for hex colors"   # Argon
git diff | python3 tools/gemini_web_task.py - --json                          # stdin + JSON
```

Exit codes: `0` ok, `2` not logged in, `3` mode locked (Carbon), `4` no reply.

## Bridge (used by qcode / opencode)

```bash
python3 tools/gemini_web_bridge.py --port 9099
curl -s localhost:9099/v1/chat/completions -H 'content-type: application/json' \
  -d '{"model":"gemini-4-hydrogen","messages":[{"role":"user","content":"hi"}]}'
```

Provider entry (already merged into `~/.config/opencode/opencode.json`, see
`tools/gemini-web.provider.json`):

```json
"gemini-web": {
  "name": "Gemini Web (browser bridge)",
  "npm": "@ai-sdk/openai-compatible",
  "options": { "baseURL": "http://127.0.0.1:9099/v1", "apiKey": "local-bridge" },
  "models": { "gemini-4-hydrogen": { ... }, "gemini-4-argon": { ... } }
}
```

Then:

```bash
qcode prompt --provider gemini-web --model gemini-4-hydrogen "summarise README.md"
```

`qcode` logs a `Model '…' taking 60s` warning while the browser works; it is
informational (the reply still arrives). Measured latency: Hydrogen ~35–45 s,
Argon ~150 s, one request at a time.

## Operational notes

- **One browser, one request.** The bridge serialises with a lock; parallel calls queue.
- **Warm session.** The browser stays open between bridge requests; each request opens a
  fresh chat, so the caller must resend full history (qcode does).
- **No tools.** The web models are chat-only; the provider sets `tool_call: false`.
  Agentic qcode turns will answer in text without calling tools.
- **Session expiry.** If the bridge exits with "not logged in", refresh the profile copy:
  `cookies.sqlite`, `cookies.sqlite-wal`, `cert9.db`, `key4.db`, `pkcs11.txt` from
  `~/snap/firefox/common/.mozilla/firefox/iekgk4l8.default`.
- **Terms of service.** This is UI automation of a consumer product, not an API. Fine for
  personal use; do not build a product on it.
- **Carbon is gated** on Google AI Pro (shows "Unlock 4 Carbon — ₹6,500/month"); the
  runner detects the dialog and exits `3`.
