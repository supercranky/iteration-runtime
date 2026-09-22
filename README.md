# Iteration Runtime

A small, cross-platform game runtime built with Sokol, QuickJS, NanoVG, and SoLoud. **Iteration Runtime is not a web-specific runtime.** Games use JavaScript as their scripting language, but that JavaScript runs in the runtime's own embedded [QuickJS](https://bellard.org/quickjs/) interpreter—not in a browser JavaScript engine and without depending on the DOM.

Performance-critical systems such as rendering, audio, model animation, particles, input, and asset loading are implemented in native C/C++. On native targets they run as native machine code; for the browser target the same runtime is compiled to WebAssembly and renders through WebGL 2. JavaScript remains the portable gameplay and application layer on every target.

## Features

- Embedded QuickJS interpreter with an ES module-based JavaScript API
- Native and iOS configurations plus a WebAssembly/WebGL 2 browser build
- Textured sprites, texture atlases, clipped drawing, and triangle rendering
- NanoVG vector graphics, gradients, strokes, custom fonts, and text layout
- glTF model loading, instancing, animation, cameras, lighting, and pixel styling
- Configurable particle systems exposed directly to JavaScript
- Sound loading and playback through SoLoud
- Keyboard, mouse, touch, resize, and per-frame callbacks
- Fixed-width, fixed-height, and cover viewport modes with pixel-size queries
- Asynchronous loading for textures, text, fonts, sounds, models, and plugins
- Versioned WebAssembly plugin ABI with typed values and buffered graphics commands
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

Open <http://localhost:8000/iteration-sapp.html>.

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

### Lifecycle, input, and display

| API | Description |
| --- | --- |
| `setOnFrame(callback)` | Sets the callback invoked once per frame. |
| `setOnResize(callback)` | Sets the window/framebuffer resize callback. |
| `setOnKeyDown(callback)` / `setOnKeyUp(callback)` | Receive `{ keyCode }` keyboard events. |
| `setOnMouseDown(callback)` / `setOnMouseUp(callback)` / `setOnMouseMove(callback)` | Receive `{ x, y }` pointer events in logical coordinates. |
| `setOnTouchStart(callback)` / `setOnTouchEnd(callback)` / `setOnTouchMove(callback)` | Receive `{ x, y, touchId }` touch events. |
| `getScreenLeft()` / `getScreenRight()` | Return the visible horizontal bounds in logical coordinates. |
| `getScreenTop()` / `getScreenBottom()` | Return the visible vertical bounds in logical coordinates. |
| `getFrameDuration()` | Returns the last frame duration in seconds. |
| `getFrameCount()` | Returns the current frame counter. |
| `getPixelSize()` | Returns one physical framebuffer pixel in logical units. |
| `now()` | Returns monotonic time in milliseconds. |

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
| `drawTextureClip(sourceX, sourceY, sourceWidth, sourceHeight, x, y, anchorX, anchorY, rotation, scale, alpha)` | Draws a pixel rectangle from the current texture. |
| `drawTexturedTriangle(x1, y1, x2, y2, x3, y3, alpha)` | Draws a triangle using the current texture. |
| `drawTriangle(x1, y1, x2, y2, x3, y3, red, green, blue, alpha)` | Draws a solid-color triangle. |
| `setClearColor(red, green, blue, alpha)` | Sets the framebuffer clear color. |
| `setSpriteLayer(layer)` | Selects a sprite layer from `0` through `130`. |
| `setSpriteForeground(enabled)` | Compatibility helper selecting the background or foreground sprite layer. |
| `flushRendering()` | Flushes submitted rendering and starts another pass. |

### Vector graphics and text

These functions map to NanoVG paths and text. Path geometry uses logical coordinates.

| API | Description |
| --- | --- |
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
| `graphicsTextBox(x, y, width, text)` | Draws wrapped text within a box. |

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

### WebAssembly plugins

| API | Description |
| --- | --- |
| `loadWasm(path, callback)` | Loads an `iteration.plugin/1` module and returns its host descriptor to the callback. |
| `callWasm(pluginId, methodId, argumentsBuffer)` | Calls a loaded plugin and returns its encoded result buffer. |

These are low-level ABI functions. See [`iteration-sapp/sapp/plugins/README.md`](iteration-sapp/sapp/plugins/README.md) and [`iteration_plugin.h`](iteration-sapp/sapp/plugins/iteration_plugin.h) for manifests, value encoding, buffered rendering, and the C plugin SDK.

## Dependency notes

- Sokol and the fips dependencies are pinned Git submodules.
- QuickJS `2026-06-04` is vendored as a minimal source set in `iteration-sapp/sapp/quickjs/sources/`.
- Build products, downloaded SDKs, deployment output, and local IDE/CMake state must not be committed.
