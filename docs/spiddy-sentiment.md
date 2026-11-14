# Spiddy sentiment: rule-based default, optional small-LLM refinement

Spiddy (the WebUI hero) picks its animation from an 864-row catalog in
`apps/webui/src/emotions.js` (pose x emotion x intensity x motion). Two classifiers can
choose the emotion:

1. **Rules (default, always on).** `sentimentFor()` in `emotions.js` maps the reply text and
   tool outcomes to an emotion. This is instant, offline, and never blocks rendering.
2. **A small text LLM (optional).** If an endpoint is configured, `sentiment-llm.js` asks it
   for a label, validates that label against the catalog vocabulary, and re-renders the hero
   in place. The rule-based clip appears first; the refined clip replaces it when it arrives.

Either way the animation is selected by `clipForEmotion()`, which only returns rows that
exist in `CATALOG`. A model cannot invent a clip that is not in the DB.

## Why not Cactus-Compute/mirage

`Cactus-Compute/mirage` is a **vision** model, not a text model:

- `pipeline_tag: image-text-to-text`
- tags: object detection, zero-shot detection, image segmentation, image classification,
  visual question answering, document question answering
- its README: "A vision model for image understanding and structured extraction"

It takes an image and returns descriptions, boxes, segmentation, or OCR. It has no text-only
sentiment path, so it cannot classify a chat reply. Do not wire it into this feature.

If you want vision, it is a reasonable model for screenshots, diagrams, or UI inspection.

## Run the bridge

```sh
# Wiring test, no ML dependencies (keyword rules, not a model)
python3 tools/spiddy_sentiment_bridge.py --backend mock --port 9101

# A real small text model via llama.cpp
pip install llama-cpp-python
python3 tools/spiddy_sentiment_bridge.py --backend llama \
  --model ./qwen2.5-0.5b-instruct-q4_k_m.gguf --port 9101

# Or a zero-shot NLI model via transformers
pip install transformers torch
python3 tools/spiddy_sentiment_bridge.py --backend transformers \
  --model cross-encoder/nli-deberta-v3-xsmall --port 9101
```

`POST /classify` takes `{"text": "..."}` and answers
`{"emotion": "goofy", "intensity": "mid", "confidence": 0.8, "source": "llm"}`.
`GET /health` reports the active backend. CORS is open so the WebUI can call it.

## Point the WebUI at it

In the browser console on the qcode WebUI:

```js
qcodeSpiddy.setSentimentEndpoint('http://127.0.0.1:9101/classify')
qcodeSpiddy.getSentimentEndpoint()
qcodeSpiddy.setSentimentEndpoint('')   // back to rules only
```

The endpoint is stored in `localStorage['qcode-sentiment-endpoint']`.

## Failure behaviour

- No endpoint: rules only, zero network traffic.
- Timeout (1.5 s), non-200, invalid JSON, or a label outside the catalog vocabulary: the
  rule-based clip stays. Nothing throws and nothing blocks the stream.
- Results are cached per reply text (200 entries), so re-renders do not re-query.

## Prompt contract

The bridge asks for two words, emotion then intensity, from a fixed list:

`happy surprised worried focused proud sad angry sleepy excited confused goofy dizzy`
plus `low mid high`. Parsing tolerates JSON, `label: intensity`, ANSI-wrapped output, and
`labels`/`scores` pairs.

## Privacy

With a local endpoint, reply text never leaves the machine. Point this only at a local
bridge; the WebUI has no cloud sentiment path.
