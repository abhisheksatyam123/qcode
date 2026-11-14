# Mascot attach contract (Spiddy)

How to mount the Spiddy mascot into the WebUI, drive it, and supply its vision
assets. Code: `apps/webui/src/mascot.js`. Character markup and CSS: `hero.js`,
`emotions.js`, `style.css` (`html[data-hero="spiddy"]`).

## 1. Mount

The theme CSS only draws the character under `<html data-hero="spiddy">`. The host
sets that attribute; `attachMascot` does not change the theme.

```js
import { attachMascot } from './mascot.js';

document.documentElement.dataset.hero = 'spiddy';
const mascot = attachMascot(document.getElementById('slot'), {
  state: { pose: 'think', emotion: 'focused', caption: 'Reading the diff' },
  className: 'my-slot',            // optional extra class on the mount element
});
```

The mount is a `<div class="mascot-mount" data-mascot="spiddy">` appended to the
container. Attaching again to the same container destroys the previous mount first.

## 1b. 3D renderer

The same five hero themes can draw the mascot with the ported 3D chibi renderer
instead of the CSS character. It uses the original demo's five models, nine
expressions, and signature moves. The WebUI pins it at quarter of the demo size
(camera z 64) in the right gutter beside the message column, just above the
composer (`HERO3D_ANCHOR` in `app.js` reads the `#input-area` top edge and the
`#messages` right edge, so it stays outside the text while messages scroll).

```js
import { attachHero3D, hero3dForTheme, expressionForEmotion } from './hero3d.js';

const hero = attachHero3D(document.getElementById('slot'), {
  hero: hero3dForTheme('spiddy-night'), // batman
  expression: expressionForEmotion('worried'), // sad
  animation: 'batglide',
  caption: 'Monitoring the build',
  reducedMotion: false,
});
hero.moveTo({ left: 120, top: 240, width: 160, height: 150 });
hero.update({ animation: 'batarang', expression: 'happy' });
hero.destroy();
```

The controller mirrors `attachMascot`: `update(patch)`, `state`, `destroy()`, plus
`setHero`, `setExpression`, `setAnimation`, `setCaption`, `moveTo`, and
`setReducedMotion`. The three.js canvas fills its container; `moveTo` sets the
anchor as a top-left box in container pixels. The WebUI calls it with the fixed
top-right anchor so the model does not wander over the conversation. The canvas is
transparent and `pointer-events: none`, and the only DOM text is the caption bubble.

Theme mapping:

| Theme | Hero |
| --- | --- |
| `spiddy` | `spidey` |
| `spiddy-night` | `batman` |
| `spiddy-steel` | `ironman` |
| `spiddy-jade` | `hulk` |
| `spiddy-brick` | `legobatman` |

Settings → Appearance → Mascot chooses `3D chibi (WebGL)` or `Classic CSS`
(`localStorage['qcode-mascot-style']`). If three.js or WebGL is unavailable, the
WebUI logs once, stores `classic`, and keeps the CSS renderer. The 3D renderer,
the generated textures, and the original move choreography live in
`src/hero3d.js`; the vendored library is `src/vendor-three.min.js`.

### 1c. 3D behaviour in the chat

Each hero now has **24 moves**: the original twelve signature moves plus twelve
shared-but-hero-flavoured acts (`think`, `search`, `wave`, `spin`, `jump`,
`stretch`, `shrug`, `point`, `facepalm`, `taunt`, `bow`, `celebrate`). The labels
differ per hero (`🤔 Hulk Think`, `🔍 Detective Scan`, `🎉 Everything Is Awesome`).
`think` and `search` ship their own props: floating thought bubbles above the head,
and a coloured scanner lens plus a ground scan ping. Each hero poses differently
(Hulk scratches his head and sniffs; Batman crosses his arms and holds a detective
lens; Iron Man taps the arc reactor and scans with his palm; Spidey taps his temple
and crouches behind a web lens; LEGO Batman strokes his chin and scans near the
cowl). Accent colours live in `HERO3D_ACCENTS`.

`app.js` drives the renderer by chat mode (`hero3dAnimationFor` in `hero3d.js`):

| Chat mode | Animation |
| --- | --- |
| generating, no running tool | `think` (always) |
| running file/web tool (`grep`, `glob`, `ls`, `read`, `search`, `find`, `rg`, `websearch`, `webfetch`, ...) | `search` (always) |
| running other tool (`bash`, `edit`, ...) | hero's `tools` pool |
| last turn succeeded | hero's `success` pool (celebrate/cheer/hero/bow) |
| last turn failed | hero's `error` pool (facepalm/shrug/brood/rage) |
| idle | the hero's full 24-move list, one act per roam tick |

Pools are per hero in `HERO3D_MODE_POOLS`; `hero3dPoolFor(mode, themeOrHero)` exposes
them and every entry is asserted to be a real move by `test/hero3d.test.js`.
The 3D mascot is pinned, so only the act and caption change — it does not move
around the chat pane.

### 1d. Expressions and dialogue

Every roam tick advances an index, so the face is not stuck on one expression.
`hero3dExpressionFor({ mode, index })` cycles **all nine expressions**
(`neutral happy sad spideysense love angry zipped angel devil`) while idle; busy
modes rotate appropriate subsets (think/search stay focused, success uses
happy/love/angel, errors use sad/angry/zipped). The header **Anim** button also
advances the expression.

Dialogue is per hero in `HERO3D_VOICE`, with seven pools each (`greeting`,
`idle`, `think`, `tools`, `search`, `success`, `error`). `hero3dLineFor(theme,
mode, seed)` only ever returns a line from the current hero's own pool, so Batman
says Gotham lines, Iron Man says JARVIS/Stark lines, Hulk speaks in HULK caps, and
the LEGO hero talks about bricks — one hero never borrows another hero's dialogue.
The same pools back the classic CSS renderer: greetings use the `greeting` pool,
live replies go through `hero3dQuipFor(theme, clip)` (pose/emotion mapped to a
pool), and roaming captions use the active mode. Switching Mascot between 3D and
Classic therefore keeps the character's own voice.

## 2. Drive

| Call | Effect |
| --- | --- |
| `mascot.update(patch)` | Merge `patch` into the state and re-render. Returns the new state. |
| `mascot.say(text)` | Shorthand for `update({ caption: text })`. |
| `mascot.state` | Copy of the current normalized state. |
| `mascot.destroy()` | Remove the element. Later calls are no-ops. |

A patch that changes the clip (`pose`, `emotion`, `intensity`, `text`) re-picks the
motion unless the patch sets `motion` itself. A caption-only patch keeps the motion.

## 3. State

| Field | Values | Default |
| --- | --- | --- |
| `pose` | `intro think computer phone tools build` | `think` |
| `emotion` | `happy surprised worried focused proud sad angry sleepy excited confused goofy dizzy` | `focused` |
| `intensity` | `low mid high` | `mid` |
| `motion` | `idle bounce shake sway` | from the catalog (stable per text) |
| `caption` | string, whitespace collapsed, max 140 chars | pose caption |
| `bubble` | string, max 200 chars (speech bubble) | none |
| `act` | `run swing crawl thwip backflip dance sit peek hero cheer salute sneak moonwalk nap spin wave hang glide batarang brood flight unibeam smash thunderclap rage leap masterbuild rock` | none (plain pose) |
| `text` | input only, used to pick a stable motion | none |

Invalid values fall back; nothing throws. Every resolved clip goes through
`clipForEmotion`, so the combination always exists in the 864-row catalog
(12 emotions x 3 intensities x 6 poses x 4 motions). Captions and bubbles are
HTML-escaped by `hero.js`.

Pure helpers (no DOM, unit-tested):

- `normalizeMascotState(input)` returns a valid state.
- `mascotMarkup(state)` returns the HTML string.
- `mascotStateFromEvents({ text, toolEvents, pose, caption, bubble })` derives a state
  from a chat message: pose from the running tool, emotion and intensity from the
  rule classifier (`sentimentFor`). No model call.

An `act` is a whole-character routine (CSS in `style.css`, `.hero-act-<name>`). It owns
the body animation while it plays, so it replaces the `motion` bob. `hang` is an act
(arms out, on the thread); pose `intro` is the separate greeting that drops in from the
thread. Reduced-motion users get the static pose.

Roaming Spiddy (`roam.js`) picks acts from pools: idle uses the theme's own list
(`THEME_POOLS`: Spider-Man, Batman, Iron Man, Hulk and Brick Batman each have their own
set); busy modes use `ROAM_POOLS` (think: sit, nap, brood; tools: thwip, run, peek;
success: cheer, salute, hero; error: sit, peek). Reduced motion turns acts off.

The acts are original choreography on the original Spiddy parts, not a port of any
third-party character or artwork. They need no WebGL or extra library.

## 4. Vision assets (Mirage in the browser)

Mirage's in-browser path needs three files in one directory: `cactus.js`,
`cactus.wasm`, and `mirage.cact`. They are about 21 MB and are **not committed**.

```sh
scripts/fetch_mirage_wasm.sh                 # -> <repo>/build/mirage-wasm
scripts/fetch_mirage_wasm.sh --check DIR     # verify only
```

The script pins each file by size and sha256 and discards any download that does not
match. Pins:

| File | Bytes | sha256 | Source |
| --- | --- | --- | --- |
| `cactus.js` | 62247 | `707341e44776ab0eb8dfc8f6a579ed5ac41e2ffc6419a18012f61619a6dfc70c` | `Cactus-Compute/cactus@4e822c90` `releases/4.1.0/wasm/` |
| `cactus.wasm` | 1241811 | `6fb6cb41145cd51fd8af97644cd684fbdd434d4d3ce261881df58cbaef5b67f7` | same |
| `mirage.cact` | 21611043 | `4249a0f7515272ff76b9414bec17d7e7daf84b15b66c7fb4f17fce1e66f9f237` | `Cactus-Compute/mirage@76c1e9e5` |

Serving: the directory must be reachable from the page. The qcode server mounts the
WebUI directory as-is, so copying the bundle to `<webui_dir>/mirage-wasm/` serves it
without a server change. Then point the client at it:

```js
qcodeSpiddy.setMirageWasmBase('/mirage-wasm')   // localStorage['qcode-mirage-wasm-base']
```

`describeImage()` (in `mirage.js`) tries the HTTP bridge first, then WASM, then
returns `null`. Measured in headless Chromium: first call 28–30 s (weights load plus a
512 px describe), then about 17 s per describe. Single-threaded.

## 5. Not in scope here

- Wiring the mount into `app.js`. `attachMascot` is ready; the chat keeps using
  `heroMarkup` directly until a host opts in.
- Serving the bundle from the qcode server by default. Needs a decision on where
  the 21 MB lives in deployments.
