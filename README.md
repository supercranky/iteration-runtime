# Iteration Runtime

A small, cross-platform game runtime built with Sokol, QuickJS, NanoVG, and SoLoud. **Iteration Runtime is not a web-specific runtime.** Games use JavaScript as their scripting language, but that JavaScript runs in the runtime's own embedded [QuickJS](https://bellard.org/quickjs/) interpreter—not in a browser JavaScript engine and without depending on the DOM.

Performance-critical systems such as rendering, audio, model animation, particles, input, and asset loading are implemented in native C/C++. On native targets they run as native machine code; for the browser target the same runtime is compiled to WebAssembly and renders through WebGL 2. JavaScript remains the portable gameplay and application layer on every target.

## Features

- Embedded QuickJS interpreter with an ES module-based JavaScript API
- Native and iOS configurations plus a WebAssembly/WebGL 2 browser build
- Textured sprites, texture atlases, per-draw flipping/tinting, and layered rendering with a dedicated UI layer
- NanoVG vector graphics, gradients, strokes, source-over/additive compositing, and custom fonts
- Wrapped text with layout-stable Unicode typewriter reveals
- glTF model loading, instancing, animation, cameras, lighting, and pixel styling
- Configurable particle systems exposed directly to JavaScript
- Sound loading and playback through SoLoud
- Keyboard, mouse, touch, resize, and per-frame callbacks
- Fixed-width, fixed-height, and cover viewport modes with physical-pixel and mobile fullscreen-reference queries
- Retina-resolution rendering on web and iOS with logical-coordinate input
- Asynchronous loading for textures, text, fonts, sounds, models, and plugins
- Versioned WebAssembly plugin ABI with typed values and buffered graphics commands, including minimum-alpha lighting layers
- Optional coarse/detailed host CPU profiling for plugin calls and graphics commands
- Asynchronous string storage with persistent backups and temporary reload snapshots
- Monotonic timing, frame-duration, and frame-count APIs for animation and profiling

## Requirements

Install these tools first:

- Git
- Python 3
- CMake 3.5 or newer
- Ninja

On macOS with Homebrew:

```sh
brew install cmake ninja
```

On Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install git python3 cmake ninja-build
```

Emscripten does not need to be installed globally. The setup command below installs it into the ignored `fips-sdks/` directory.

## Clone and install

Clone the repository with its pinned dependencies:

```sh
git clone --recursive https://github.com/supercranky/iteration-runtime.git
cd iteration-runtime
```

If the repository was cloned without `--recursive`, initialize it with:

```sh
git submodule update --init --recursive
```

Install and activate the Emscripten SDK:

```sh
cd iteration-sapp
python3 fips emsdk install latest
```

The local `fips` launcher supports modern Python and modern emsdk configuration files. No global fips installation is required.

## Build WebAssembly

From `iteration-sapp/`:

```sh
# Optimized build
python3 fips build sapp-webgl2-wasm-ninja-release

# Debug build
python3 fips build sapp-webgl2-wasm-ninja-debug
```

Generated files are written outside the source tree:

```text
../fips-build/iteration-sapp/<configuration>/
../fips-deploy/iteration-sapp/<configuration>/
```

Those directories and `fips-sdks/` are intentionally ignored by Git.

## Run in a browser

WASM must be served over HTTP rather than opened through a `file://` URL:

```sh
python3 -m http.server 8000 \
  --directory ../fips-deploy/iteration-sapp/sapp-webgl2-wasm-ninja-release
```

Open <http://localhost:8000/iteration-sapp.html>. WebAudio autoplay warnings before user interaction are expected; page errors and failed asset requests are not.

## JavaScript and assets

The runtime loads `iteration-sapp/sapp/data/index.js`. Files copied into a deployment are explicitly listed in:

```text
iteration-sapp/sapp/data/iteration-assets.yml
```

After changing `index.js` or another listed asset, rebuild the selected configuration. If a replacement file retained an older timestamp and fips does not copy it, run:

```sh
touch sapp/data/index.js
python3 fips build sapp-webgl2-wasm-ninja-release
```

## Verification

After changing C/C++, CMake, Sokol, QuickJS, or dependencies, build both WASM configurations above. For asset-only changes, rebuild the affected configuration and confirm the deployed files match their sources.

Run the focused regressions from the repository root:

```sh
python3 tests/storage-files.py
python3 tests/retina-coordinates.py
cc -std=c99 -Wall -Wextra -Werror tests/plugin_paths.c -o /tmp/plugin-paths
/tmp/plugin-paths
```

Also smoke-test the served browser build for page errors and failed requests. Storage changes require the consuming application's browser storage/reload tests; the native file test alone does not verify IndexedDB or sessionStorage.

## JavaScript API reference

Runtime functions are ES module exports from `runtime`:

```js
import * as Runtime from "runtime";

Runtime.setClearColor(0.05, 0.05, 0.08, 1);
Runtime.setOnFrame(() => {
    // Update game state and submit drawing commands.
});
```

Coordinates use the runtime's logical coordinate system, centered at `(0, 0)`. Colors and alpha values use the range `0` to `1`. Angles passed to sprite drawing functions are in radians.

### Persistent string storage

`Runtime.saveData(key, string, temporary = false)`, `loadData(key, temporary = false)`
and `deleteData(key, temporary = false)` return promises. Load returns a string or
`null`; invalid arguments/full queues throw and I/O failures reject. Values are
UTF-8 strings limited to 8 MiB, with four outstanding requests. Keys are ASCII
letters/digits/`_`/`-`/`.` without a leading dot or `..`; writes allow 56 characters,
reads/deletes 64. `.tmp` is reserved and `.backup` is read/delete-only.

Web uses IndexedDB transactions to update the primary and `<key>.backup` together;
temporary storage uses tab-local sessionStorage. `Runtime.isReload()` reports
browser reload navigation (false on native). iOS uses private
`Documents/iteration-data/`; other POSIX builds use `$HOME/.iteration-data/`.
A serial pthread worker performs file I/O, flushes/fsyncs a temporary file,
rotates the old primary to backup, and atomically renames the new primary.
Promise completion is polled on the main thread. Native temporary data uses
`TMPDIR/iteration-data/`. Windows currently rejects storage operations explicitly.
Delete the backup separately when discarding a saved run.

The application owns JSON schemas/migrations and decides which settled game state
to save. Never serialize native pointers or assume shutdown callbacks complete.
Implementation: [`iteration-sapp/sapp/engines/storage.h`](iteration-sapp/sapp/engines/storage.h).

```js
await Runtime.saveData("save-slot-1", JSON.stringify({ version: 1, score: 42 }));
const saved = await Runtime.loadData("save-slot-1");
const state = saved === null ? null : JSON.parse(saved);
```

Catch storage failures and validate loaded data in the application. To recover a
previous save, explicitly load `save-slot-1.backup`; loading the primary does not
automatically fall back to its backup.

### Lifecycle, input, and display

Native iOS and web request full Retina resolution (`sapp_desc.high_dpi = true`),
using Sokol's native screen scale or the browser's `devicePixelRatio` respectively.
Web CSS canvas dimensions stay unchanged; only its backing buffer scales.
Web and native builds request `sample_count = 4` (4× MSAA), retaining full Retina
resolution. WebGL chooses its supported antialiasing sample count; the consuming
Ner's local/live WebKit checks report four samples. Earlier web no-MSAA
measurements are historical; the performance/appearance trade-off on iPhone
still needs comparison.
Web presentation uses `SAPP_COMPOSITEMODE_PREMULTIPLIED` (`alpha: true`,
`premultipliedAlpha: true`, `preserveDrawingBuffer: false`). Safari 18.6 was
significantly slower with an opaque (`alpha: false`) context at Retina sizes.
Applications clearing to alpha 1 remain visually opaque; sprite/legacy pipelines
preserve destination opacity with source-over alpha blending. Native composition
is unchanged. Do not force an opaque web context as a presumed optimization
without profiling actual Safari, not just a newer Playwright WebKit build.
Other native platforms retain their previous DPI setting.
Sprites, text, vector graphics, lighting layers and particle masks consistently
use physical framebuffer pixels internally; NanoVG frames therefore use the
framebuffer dimensions and a pixel ratio of 1, rather than applying DPI twice.
JavaScript logical screen coordinates and touch positions stay aligned, while
`getPixelSize()` describes a physical Retina pixel. Full-resolution offscreen
targets increase GPU/memory cost. Coordinate regression:
`python3 tests/retina-coordinates.py`.

| API | Description |
| --- | --- |
| `setOnFrame(callback)` | Sets the callback invoked once per frame. |
| `setOnResize(callback)` | Sets the window/framebuffer resize callback. |
| `setOnKeyDown(callback)` / `setOnKeyUp(callback)` | Receive `{ keyCode }` keyboard events. |
| `setOnMouseDown(callback)` / `setOnMouseUp(callback)` / `setOnMouseMove(callback)` | Receive `{ x, y }` pointer events in logical coordinates. |
| `setOnTouchStart(callback)` / `setOnTouchEnd(callback)` / `setOnTouchMove(callback)` | Receive `{ x, y, touchId }` touch events. |
| `getScreenLeft()` / `getScreenRight()` | Return the visible horizontal bounds in logical coordinates. |
| `getScreenTop()` / `getScreenBottom()` | Return the visible vertical bounds in logical coordinates. |
| `getFrameDuration()` | Returns Sokol's smoothed frame duration in seconds via `sapp_frame_duration()` on web and native. |
| `getFrameCount()` | Returns the current frame counter. |
| `getPixelSize()` | Returns one physical framebuffer pixel in logical units. |
| `setSpritePixelSnapping(enabled)` / `getSpritePixelSnapping()` | Controls physical-pixel vertex rounding for runtime sprites. Defaults to enabled and resets on runtime shutdown. Does not change camera transforms, texture filtering, vector graphics or model pixel styling. Diagnostic callers must control camera snapping separately and restore the default afterward. |
| `getFullscreenPixelSize()` | Returns the reference pixel size for the full display at the current render density on coarse-pointer mobile browsers. Uses screen orientation and canvas CSS size so browser toolbars do not alter integer zoom selection. Desktop and native builds return `getPixelSize()`. Use the reference to choose magnification, but the actual pixel size to draw/snap and map input. |
| `now()` | Returns live monotonic time in milliseconds; use for profiling and deadlines. |
| `frameNow()` | Returns a timestamp latched once at frame entry. Web uses `document.timeline.currentTime` (the window rAF timestamp); native uses a `CLOCK_MONOTONIC` sample. If the browser timeline is unavailable, the live monotonic clock is sampled once instead. Before the first frame, returns `now()`. |

Only one callback of each type is retained; setting one again replaces it.

### Assets and audio

Loading is asynchronous and callback-based. Loaded textures, sounds, fonts, models, and plugins are represented by numeric runtime IDs.

| API | Description |
| --- | --- |
| `loadTexture(path, callback)` | Loads an image and calls `callback({ id, name, width, height, widthPixels, heightPixels })`. |
| `loadText(path, callback)` | Loads a UTF-8 text asset and passes its contents to the callback. |
| `loadSound(path, callback)` | Loads audio and calls `callback({ id, name })`. |
| `playSound(soundId, volume, pitch)` | Plays a loaded sound with the requested volume and playback-speed multiplier. |
| `loadFont(path, callback)` | Loads a font for NanoVG text and calls `callback({ id, name })`. |

### Sprites and triangles

Call `setTexture` before submitting textured geometry.

| API | Description |
| --- | --- |
| `setTexture(textureId, nearest = false)` | Selects the current texture and linear or nearest-neighbor sampling. |
| `drawTexture(x, y, anchorX, anchorY, rotation, scale, alpha)` | Draws the entire current texture. |
| `drawTextureClip(sourceX, sourceY, sourceWidth, sourceHeight, x, y, anchorX, anchorY, rotation, scale, alpha, flipX = false, flipY = false, tint = null)` | Draws a pixel rectangle from the current texture. Optional flips reverse texture coordinates without changing geometry, anchors, rotation, or pixel snapping. Optional `tint: {r, g, b, amount}` blends the source RGB toward a color while preserving source alpha. All four values must be finite and in `[0,1]`; `amount: 0` is unchanged, `0.5` halfway, `1` a solid-color silhouette. Tint is per draw, not persistent state. Existing calls are unchanged. Supported on GLES3 (WebGL2/iOS OpenGLES) and GLCORE; nonzero tint throws on unsupported backends. |
| `drawTexturedTriangle(x1, y1, x2, y2, x3, y3, alpha)` | Draws a triangle using the current texture. |
| `drawTriangle(x1, y1, x2, y2, x3, y3, red, green, blue, alpha)` | Draws a solid-color triangle. |
| `setClearColor(red, green, blue, alpha)` | Sets the framebuffer clear color. |
| `setSpriteLayer(layer)` | Selects a sprite layer from `0` through `131`. Layer `131` is reserved for UI sprites drawn after NanoVG panels and text. Restore layer `0` after submitting UI sprites. |
| `setSpriteForeground(enabled)` | Compatibility helper selecting the background or foreground sprite layer. |
| `flushRendering()` | Flushes submitted rendering and starts another pass. |

### Best-effort GPU profiling

`setGpuProfiling(enabled)` opts into sampled rendering diagnostics (off by
default). `getGpuProfile()` returns cached status, six pass timings/draw counts,
framebuffer dimensions, sample count and result ages. `isGpuProfileSample()` and
`getRenderWork()` expose sampled cumulative `[sprite/primitive, vector, plugin]`
submission counters for application-engine attribution without splitting batches.

Web timers require `EXT_disjoint_timer_query_webgl2`; the current native GLES
host reports timing unavailable but supplies counters/configuration. Queries are
non-overlapping and asynchronous: one frame in 20, up to four pending batches of
24 reusable queries. Unready results are never read, overflow/disjoint samples
are discarded, and disabling/context loss cleans up queries. Sokol's previous
stats-enabled state is restored on disable. Pass sums exclude browser compositor/
presentation and uninstrumented gaps; they are not per-engine GPU times. See the
consuming Ner's `docs/gpu-profiling.md` and `verify-gpu-profile.mjs`. Simulated test
results are not real GPU measurements.

### Optional binary screen overlay (GLES3)

Prepare before loading fonts; applications generate the mask, not the runtime.

| API | Description |
| --- | --- |
| `prepareScreenOverlay()` | Creates an optional UI NanoVG context and mask pipeline. Rejects unsupported backends or preparation after fonts have loaded. Subsequent font loads are mirrored into both contexts using the same source bytes. |
| `getFramebufferSize()` | Returns `{ width, height }` in physical framebuffer pixels. |
| `setScreenOverlay(width, height, buffer, cellSize, opacity = 1, frames = 1)` | Uploads 1–16 frame-major R8 masks to a texture array from an exact-sized `ArrayBuffer` (`width * height * frames` bytes). Values ≥128 draw black at the requested opacity; other values discard. Opacity must be finite and in `[0,1]`; zero skips drawing. `cellSize` is an integer from 1–16 physical pixels. Dimensions must equal `ceil(framebufferSize / cellSize)`, each ≤8192, with at most 16 MiB of pixels across all frames. Failed updates retain the previous mask. Upload resets the selected frame to 0. |
| `setScreenOverlayFrame(index)` | Selects a cached frame using an integer index in `[0, frames)`. No allocation or texture upload; the next draw updates a uniform. Rejects invalid indices or selection without an uploaded bank. |
| `clearScreenOverlay()` | Releases the mask texture and disables overlay drawing; the prepared UI context remains available. |
| `beginScreenUI()` | Once per frame, switch subsequent vector/text commands to the UI context. Idempotent; reject inside an unfinished lighting layer. UI vector state begins at NanoVG defaults rather than inheriting world styles. |

Render order: world sprites/models and NanoVG → masked particles → overlay → UI
NanoVG → reserved UI sprite layer 131. With no preparation the existing render
path remains unchanged. Overlay geometry excludes 64×64-texel tiles empty in every cached frame and
uses one draw (premultiplied blending for partial opacity, disabled at opacity 1).
The mask is byte-per-texel, not bit-packed;
there is no scene-color copy or extra fullscreen render target. Resized masks
must be reuploaded; stale-size masks are skipped. GPU objects/UI context are
released at shutdown. See the consuming Ner's `docs/vignette.md` and
`verify-vignette.mjs` for pixel, particle, UI, Retina and resize tests.

### Vector graphics and text

These functions map to NanoVG paths and text. Path geometry uses logical coordinates.

| API | Description |
| --- | --- |
| `graphicsSave()` / `graphicsRestore()` | Saves/restores NanoVG drawing state, including compositing. Pair these around temporary effects (use `try/finally`). |
| `graphicsCompositeOperation(mode)` | Sets `"source-over"` (default) or `"lighter"`/`"additive"`. Additive blending adds premultiplied source color to the destination, so fill alpha controls strength. The operation is captured per queued fill/stroke/text call. Unsupported modes throw. |
| `graphicsCircle(x, y, radius)` | Adds a circle in logical coordinates. Requires finite coordinates and a nonnegative radius. Use `graphicsFillColor` and `graphicsFill` for a translucent filled circle. |
| `graphicsBeginPath()` / `graphicsClosePath()` | Starts or closes a vector path. |
| `graphicsRect(x, y, width, height)` | Adds a rectangle to the current path. |
| `graphicsRoundedRect(x, y, width, height, radius)` | Adds a rounded rectangle. |
| `graphicsPolygon(points)` | Adds a polygon from an array of coordinate pairs. |
| `graphicsFillColor(red, green, blue, alpha)` | Sets the solid fill color. |
| `graphicsRadialGradient(centerX, centerY, innerRadius, outerRadius, innerR, innerG, innerB, innerA, outerR, outerG, outerB, outerA)` | Sets a radial-gradient fill. |
| `graphicsHole()` / `graphicsSolid()` | Marks the current path winding as a hole or solid. |
| `graphicsFill()` | Fills the current path. |
| `graphicsStrokeColor(red, green, blue, alpha)` | Sets the stroke color. |
| `graphicsStrokeWidth(width)` | Sets the stroke width. |
| `graphicsStroke()` | Strokes the current path. |
| `graphicsFontSize(size)` | Sets the text size. |
| `graphicsFontFace(name)` | Selects a font by the name passed to `loadFont`. |
| `graphicsTextAlign(flags)` | Sets NanoVG text-alignment flags. |
| `graphicsText(x, y, text)` | Draws a single line of text. |
| `graphicsTextBox(x, y, width, text, visibleCharacters?)` | Draws wrapped text within a box (logical units). Optional nonnegative Unicode code-point count reveals a prefix while retaining line breaks from the full text, for stable typewriter animation. |

### glTF models

Models are loaded as shared assets, then instantiated so each instance can have independent animation state.

| API | Description |
| --- | --- |
| `loadModel(path, callback)` | Loads a `.glb` model. The callback receives `{ id, animations }` or `{ error }`. |
| `unloadModel(assetId)` | Unloads an unused model asset. Destroy its instances first. |
| `createModelInstance(assetId)` | Creates an instance and returns its ID. |
| `destroyModelInstance(instanceId)` | Destroys an instance. |
| `drawModel(instanceId, x, y, height, scale, rotation, pixelSize = 1, lights = null, spriteLayer = 0)` | Queues an instance for drawing. `lights` is a flat array of up to four 9-value light records. |
| `playModelAnimation(instanceId, name, speed = 1, loop = false, onComplete)` | Starts a named glTF animation. |
| `stopModelAnimation(instanceId)` | Pauses the active animation. |
| `setModelAnimationTime(instanceId, seconds)` | Seeks the active animation. |
| `setModelCamera(pitch, yaw)` | Sets model camera rotation in degrees. |
| `setModelLighting(ambient, steps)` | Sets ambient intensity and directional-light quantization steps. |
| `setModelPixelStyle(enabled, palette, outline, antialias = false, dither = 0)` | Configures pixel rendering. `palette` is a flat list of RGB byte triplets. |
| `setWorldSpriteLastLayer(layer)` | Sets the final sprite layer composited among queued models. |

A model light record is `[x, height, z, innerRadius, outerRadius, red, green, blue, intensity]`.

### Particles

The `particles` export creates native particle systems:

```js
import { particles } from "runtime";

const dust = particles.create({
    maxParticles: 128,
    render: { texture: "dust.png" },
    emission: { rate: 8 },
    lifetime: [1, 2],
    velocity: { x: [-10, 10], y: [-20, -5] },
    size: { range: [4, 8], overLife: [[0, 0], [0.2, 1], [1, 0]] }
});

dust.setPosition(0, 100);
dust.start();
```

`particles.create(config)` accepts `maxParticles`, `seed`, `space` (`"local"` or `"world"`), `area`, `emission`, `spawn`, `lifetime`, `velocity`, `speed`, `size`, `opacity`, `rotation`, `angularVelocity`, `modifiers`, and `render`. Numeric values may be constants, `[min, max]` ranges, or `{ overLife: [[time, value], ...] }` curves where supported. The returned system provides:

- `start()`, `stop()`, `pause()`, `resume()`, and `destroy()`
- `emit(count = 1)`
- `setPosition(x, y)`, `setRotation(radians)`, and `setScale(scale)`
- `setEmissionRate(rate)`

On GLES3/WebGL2, `render.mask: { source: "directLight", threshold: 0.45,
softness: 0.15 }` samples the current completed minimum-alpha lighting texture
inside the particle fragment shader. Visible quads are batched by adjacent
texture, with no particle render targets or fullscreen masking passes. The
renderer flushes after NanoVG and before reserved UI sprites. Masking is per
particle (not group opacity), so overlapping particles at a partial mask can be
slightly brighter than the former per-system image implementation. No completed
lighting buffer means masked particles are not drawn. Other backends retain
their unmasked fallback.

Scale-zero local systems skip rendering entirely; viewport culling includes
particle rotation, oscillation, size and texture aspect. Fully offscreen systems
never start a draw batch. This is **render-only** culling: simulation and emission
continue unless explicitly paused. Masked rendering has bounded storage for
32,768 visible quads and 4,096 consecutive texture batches; excess quads/batches
are omitted. CPU/GPU buffers and shader objects are released at shutdown.
`tests/particle-culling.c` tests native culling; the Ner consuming app includes
pixel/blending/Retina checks in `scripts/verify-particle-mask.mjs`.

### WebAssembly plugins

| API | Description |
| --- | --- |
| `loadWasm(path, callback)` | Loads an `iteration.plugin/1` module and returns its host descriptor to the callback. |
| `callWasm(pluginId, methodId, argumentsBuffer)` | Calls a loaded plugin and returns its encoded result buffer. |
| `setPluginProfiling(enabled, detailed = false)` | Enables and resets CPU profiling, or disables it while preserving totals. Detailed mode times graphics commands/groups and adds measurement overhead. |
| `getPluginProfile()` | Returns cumulative plugin/graphics timing, byte and command counters without resetting them. See the plugin SDK documentation for stage boundaries. |

Browser plugins execute through WebAssembly; iOS Debug and Release use WAMR. Statically registered native plugins are an optional alternative. Application-specific plugins and their build/packaging steps belong in the consuming application repository; this repository keeps the generic host, ABI, and smoke-test plugin.

Profiling is disabled by default. Timings are cumulative CPU milliseconds, not GPU completion times or per-frame averages. Re-enabling profiling resets counters; use separate coarse and detailed measurement windows because per-command clocks add overhead.

These are low-level ABI functions. See [`iteration-sapp/sapp/plugins/README.md`](iteration-sapp/sapp/plugins/README.md) and [`iteration_plugin.h`](iteration-sapp/sapp/plugins/iteration_plugin.h) for manifests, value encoding, buffered rendering, and the C plugin SDK.

### Coverage-triangle plugin primitive

`ITER_RENDER_COVERAGE_TRIANGLES` (opcode 19) adds bounded black-alpha mask meshes
on GLES3. Each vertex supplies a position, two affine signed-distance fields and
opacity; the shader maps the fields through unit-disk cap coverage. Packets select
maximum or replacement blending and logical clip bounds. Application-specific
occluder construction stays in the plugin. The command is restricted to a fresh
minimum-alpha layer before any deferred NanoVG path commands. Payload validation,
1536-vertex limits and GL/Sokol cleanup are mandatory; see
`sapp/plugins/iteration_plugin.h` under `iteration-sapp/` and
`tests/coverage-command.c`. Existing plugins and rendering paths are unchanged.

## Dependency notes

- Sokol and the fips dependencies are pinned Git submodules.
- QuickJS `2026-06-04` is vendored as a minimal source set in `iteration-sapp/sapp/quickjs/sources/`.
- NanoVG builds from the existing runtime-owned `iteration-sapp/libs/nanovg/` sources. Its source-over/additive state is saved in `NVGstate`, forwarded to renderer callbacks, and captured per GL draw call. Do not mix these headers with the old `fips-nanovg` callback ABI; that historical submodule is no longer imported by the build. No GL state is changed prematurely when JavaScript selects a composite mode.
- Build products, downloaded SDKs, deployment output, and local IDE/CMake state must not be committed.
