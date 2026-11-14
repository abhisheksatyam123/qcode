# Cactus + Spiddy: what is set up, and how to plug integration code in

This page is the handoff state for running Cactus models locally and wiring them into the
WebUI hero (Spiddy). It is current as of this setup.

## What is installed

| Item | Location | Notes |
| --- | --- | --- |
| Python venv | `~/tmp/cactus/venv` | Python 3.13, pip 26.3 bootstrapped with `get-pip.py` |
| Cactus runtime | `cactus-compute 4.2.0` | `~/tmp/cactus/venv/bin/python -m pip install -U cactus-compute` |
| Mirage weights | `~/.cache/cactus/models/mirage/…/mirage.cact` | downloaded on first `cactus.Mirage()`; ~9 s cold load |
| Engine | `~/.cache/cactus` + `~/.cache/huggingface/hub/models--Cactus-Compute--cactus` | pinned by the package |

The Cactus package ships three models:

| Model | Class | Use |
| --- | --- | --- |
| `Cactus-Compute/mirage` | `cactus.Mirage()` | **vision**: scene, regions, OCR text, relations, reasoning |
| `Cactus-Compute/needle3` | `cactus.Needle()` / `cactus.Cactus()` | text agent (tools, extraction, completion) |
| `Cactus-Compute/whistle` | `cactus.Whistle()` | speech to text |

**Mirage cannot do sentiment.** It is an image-text-to-text model; there is no text-only
path. Spiddy's animation label therefore comes from:

1. `apps/webui/src/emotions.js` — rule classifier (default, instant, offline), or
2. `apps/webui/src/sentiment-llm.js` — optional small **text** model via
   `tools/spiddy_sentiment_bridge.py` (llama.cpp or transformers), see
   `docs/spiddy-sentiment.md`.

## Run the Mirage bridge

```sh
~/tmp/cactus/venv/bin/python tools/cactus_mirage_bridge.py --port 9102 --warm
```

- `--warm` loads Mirage before serving, so the first request is not a cold start.
- CPU inference is ~9–13 s per image; the server serialises calls internally.
- `GET /health` → `{"ok": true, "mirage": "ready", "model": "Cactus-Compute/mirage"}`
- `POST /describe` → `{"ok": true, "model": "mirage", "took_ms": 8931, "summary": "…", "result": {scene, objects, regions, text, relations, events, reasoning}}`

`/describe` accepts an image as a data URL (`data:image/png;base64,…`) or raw base64.
CORS is open for `http://127.0.0.1:*`, so the WebUI can call it directly. Requests are
capped at 8 MB.

## Two ways to run Mirage in the WebUI

### 1. In the browser (WASM), no server — verified

The Cactus engine ships a WASM build that exports the same `cactus_describe` as the native
library, so Mirage runs entirely in the page:

| Asset | Size | Source |
| --- | --- | --- |
| `cactus.js` | 61 KB | `Cactus-Compute/cactus` release 4.1.0, `wasm/cactus.js` |
| `cactus.wasm` | 1.2 MB | same release, `wasm/cactus.wasm` |
| `mirage.cact` | 20.6 MB | `Cactus-Compute/mirage` |

Measured in headless Chromium (single-threaded WASM, CPU):

| Step | Time |
| --- | --- |
| engine boot (`createCactus()`) | 0.1 s |
| weights fetch (20.6 MB) + `cactus_load` | ~2 s / 0.3 s |
| `cactus_describe` at 512 px | 16.7 s |
| first call end to end (load + describe) | 28–30 s |

Serving: put the three files in one directory and point the client at it. The qcode server
serves them as static assets like any other WebUI file.

```js
qcodeSpiddy.setMirageWasmBase('/mirage-wasm')   // directory holding the three files
```

```js
import { describeImage, setMirageWasmBase, loadMirageWasm, mirageWasmReady } from './mirage.js';

setMirageWasmBase('/mirage-wasm');
await loadMirageWasm();            // optional warm-up; cached for the page
await describeImage(dataUrl);      // -> {summary, result, tookMs, model: 'mirage-wasm'}
```

Notes:

- `describeImage()` prefers the HTTP bridge when an endpoint is set, otherwise the WASM
  engine when an asset base is set, otherwise returns `null` (no request, no error).
- The engine is single-threaded in WASM; keep `maxWidth` at 512 unless you can wait.
- OCK text is a little rougher in WASM than native (same model, different numeric path),
  e.g. "rle-based clp" instead of "rule-based clip". Scene/regions/reasoning are stable.
- Weights are 20.6 MB, so serve them with compression and a long cache header, or keep
  the bridge if you do not want that download on first use.

### 2. Through the local Python bridge (native, faster)

See the next section. Native `describe` is ~9–13 s per 760×560 image, about 2× faster than
WASM, and the page stays light.

## Frontend contract

`apps/webui/src/mirage.js` is the client. It never throws and never blocks rendering.

```js
import { describeImage, getMirageEndpoint, setMirageEndpoint, summarizeMirage } from './mirage.js';

setMirageEndpoint('http://127.0.0.1:9102/describe');   // localStorage['qcode-mirage-endpoint']
getMirageEndpoint();                                   // '' means off
await describeImage(dataUrl);                          // {summary, result, tookMs, model} | null
summarizeMirage(payload);                              // compact line from summary or raw result
```

Or from the browser console on the WebUI:

```js
qcodeSpiddy.setMirageEndpoint('http://127.0.0.1:9102/describe')
qcodeSpiddy.getMirageEndpoint()
```

With no endpoint, `describeImage()` returns `null` before any network call. Timeouts
(45 s default), HTTP errors, invalid JSON, `ok: false`, oversize images, and non-data-URL
inputs all return `null`; callers show a fallback message.

### Where it appears in the UI

Attached images in a message get a **👁 Spiddy look** button. Clicking it renders Spiddy's
line from `personality.js` (`look` voice) and then the Mirage summary in a `.spiddy-look`
card. The summary text is HTML-escaped (`esc()` in `app.js`).

Relevant files:

- `apps/webui/src/mirage.js` — client
- `apps/webui/src/app.js` — `runSpiddyLook()`, `renderMessageAttachments()`, console helper
- `apps/webui/src/style.css` — `.msg-attach-figure`, `.msg-attach-look`, `.spiddy-look`
- `tools/cactus_mirage_bridge.py` — local server

## Adding more Spiddy + Cactus behaviour

Drop new modules in `apps/webui/src/` and import them from `app.js`. Keep the rules the
existing code follows:

1. Pure logic in its own module (no DOM), so `node --test` can import it.
2. Optional network features must degrade to the current behaviour when the bridge is off.
3. Escape every model-produced string before it reaches `innerHTML`.
4. Add a test in `apps/webui/test/webui_integration.test.js`; the suite runs with
   `node --test apps/webui/test/*.test.js`.

If you want text generation (Needle) in the frontend, mirror the Mirage shape: a bridge
endpoint plus a client module that returns `null` on any failure. `cactus.Needle()` exposes
`run(query, max_steps, max_new_tokens)`, `complete()`, `extract()`, and `embed()`; weights
download on first use.

## Verified

- Mirage `describe()` on a real screenshot: scene + regions + OCR + reasoning, ~9–13 s.
- Bridge `/health` and `/describe` over HTTP with a data URL: 200, `took_ms` reported.
- Chromium end to end: button → `mirage.js` → bridge → 8.9 s → Spiddy card, no console errors.
- WebUI suite: 92 tests pass (client parsing, timeout, 500, oversize, escaping, wiring).
