# AGENTS.md

## Repository purpose

This repository builds the Iteration JavaScript runtime. The main application is in `iteration-sapp/` and targets WebAssembly/WebGL 2 through fips and Emscripten.

## Initial setup

From the repository root:

```sh
git submodule update --init --recursive
cd iteration-sapp
python3 fips emsdk install latest
```

Required host tools are Git, Python 3, CMake, and Ninja. Emscripten is installed locally under `fips-sdks/`; do not require or assume a global emsdk installation.

## Required verification

Run both WASM configurations after changing C/C++, CMake, Sokol integration, QuickJS, or dependencies:

```sh
cd iteration-sapp
python3 fips build sapp-webgl2-wasm-ninja-debug
python3 fips build sapp-webgl2-wasm-ninja-release
```

For asset-only changes, rebuilding the affected configuration is sufficient. Confirm that the deployed file matches the source when fips timestamp-based copying is involved.

Serve the release build for browser testing:

```sh
python3 -m http.server 8000 \
  --directory ../fips-deploy/iteration-sapp/sapp-webgl2-wasm-ninja-release
```

Then test <http://localhost:8000/iteration-sapp.html>. WebAudio autoplay warnings before user interaction are expected; page errors and failed requests are not.

## Source layout

- `iteration-sapp/sapp/`: application and runtime source
- `iteration-sapp/sapp/data/index.js`: JavaScript entry point loaded by QuickJS
- `iteration-sapp/sapp/data/iteration-assets.yml`: deployment asset allowlist
- `iteration-sapp/sapp/quickjs/`: minimal vendored QuickJS runtime
- `iteration-sapp/sapp/plugins/`: generic plugin ABI and runtime smoke-test plugin
- `iteration-sapp/libs/sokol/`: Sokol implementation translation units
- `iteration-sapp/libs/nanovg/`: existing runtime-owned NanoVG core/GL backend, now built directly with per-draw source-over/additive compositing. Keep core, public header and renderer callback signatures synchronized; do not use the old `fips-nanovg` headers.
- `sokol/`: pinned upstream Sokol headers
- `sokol-tools-bin/`: pinned shader compiler binaries
- `fips-*`: pinned build dependencies

## Generated and local-only files

Never commit:

- `fips-build/`
- `fips-deploy/`
- `fips-sdks/`
- CMake caches and generated build-system files
- object files, static libraries, WASM binaries, APKs, editor state, or Python caches

These are covered by the root `.gitignore`. If a new tool creates generated output, add an appropriate ignore rule rather than committing it.

## Dependency rules

- Keep dependency directories as Git submodules; do not vendor their generated output.
- When upgrading Sokol, update `sokol` and `sokol-tools-bin` together and migrate removed APIs in the application.
- QuickJS is intentionally vendored as only the files needed to build the embedded library. Do not add upstream tests, examples, command-line tools, generated archives, or precompiled libraries.
- Application-specific plugins and their build systems belong in the consuming application repository. Keep only the stable plugin ABI, generic host, and smoke-test plugin here.
- Do not edit files under `fips-build/`, `fips-deploy/`, or `fips-sdks/`; regenerate them.

## Storage

`sapp/engines/storage.h` implements generic asynchronous string storage: IndexedDB
with transactional backup on web, tab-local sessionStorage for reload snapshots,
and atomic POSIX files on a bounded serial worker on native. JS promises must be
resolved/freed on the main thread only. Keep request/value limits, key validation,
backup recovery and shutdown cleanup intact. Game schemas and migrations belong
to the consuming app. Run `python3 tests/storage-files.py` and the consuming
app's browser storage tests after changes. Windows storage rejects explicitly.

## Audio voices

`Runtime.playSound(soundId, volume = 1, pitch = 1, loop = false)` returns a SoLoud
voice handle. `Runtime.stopSound(handle)` stops that voice, returning whether it
was valid. `Runtime.fadeOutSound(handle, seconds = 0.5)` uses SoLoud's volume
fader and scheduled stop, not JS timers. Zero stops immediately; durations must be
finite and in [0, 3600]. Never let frame-level cleanup cancel an in-progress fade;
VM teardown still stops all voices. Register loaded sounds before invoking their callbacks, so playback
from the callback passes ID validation. VM teardown stops all voices before
freeing sounds; never persist native handles. The pinned SoLoud Wav decoder
supports WAV/Vorbis, not MP3: applications must convert unsupported sources at
build time. Ner's `verify-title-music.mjs` exercises decoded audio and native voice
lifecycle in Chromium/WebKit; audible iPhone output needs device verification.

iOS requests 4096-frame CoreAudio buffers (two queued buffers), versus the backend's
2048-frame default, to trial additional scheduling headroom for reported music
stutter. Other platforms retain automatic sizing. This increases output latency;
no underrun trace or on-device stutter improvement has yet been established.

## Per-model styles and missing normals

`models/models.c` snapshots palette, pixel-mode flags and ambient/steps per queued
model draw. Do not read those settings from final frame-global state at render
time: interleaved chest/bone instances need independent palettes. Match the shared
pixel-target AA factor to each draw before rendering. Ner `Models.create` supports
optional `palette` and `lighting` overrides, then restores global defaults.

For GLBs without NORMAL, generate flat triangle normals by splitting indexed
corners; keep explicit normals untouched. Bound primitive allocations, validate
indices before dereferencing and free temporary arrays on failures. Verify with
Ner's `verify-bone-palette.mjs` (Chromium/WebKit), `verify-model-pixels.mjs` and a
full-game smoke check. These native features require an iPhone rebuild.

## Particle rendering

`engines/particle_mask.h` batches direct-light masked quads and samples the lighting
texture per fragment; do not reintroduce per-system fullscreen particle targets.
It flushes after the main NanoVG frame and before reserved UI sprites. Preserve
premultiplied alpha, physical framebuffer coordinates and GL/Sokol state cleanup.
Native culling is render-only: zero-scale local systems skip traversal, and other
systems open a batch lazily only for visible particles. Simulation stays active.
`Runtime.drawLightMaskedSprite` also submits centered atlas quads to this pass,
with per-batch filtering/tint and per-quad UVs/opacity/mask response. Reset the queue
before the JS frame callback, not before native particle traversal, or JS-submitted
sprites disappear. Preserve mirroring, premultiplied tint, pixel snapping, bounded
batches and nearest/linear sampler cleanup. Ner's `verify-ghost-mask.mjs` checks
light/shadow visibility, atlas rendering, tint, mirroring and Retina resizing.
Run `tests/particle-culling.c` linked with `sapp/particles/particle_system.c`, the
existing `iteration-sapp/tests/particle_system_test.c`, and the consuming Ner's
`node scripts/verify-particle-mask.mjs` (also `--webkit`) after changes.

## Retina font rasterization

Fontstash scratch storage is bounded to 128 KiB per context. Do not reduce it to
64,000 bytes: stb_truetype's 28-byte WASM active edges select a 56 KB allocation,
leaving too little for large glyph outlines (Blackcastle `m` fails around 270px).
Release builds silently cache blank glyphs; debug builds can assert. Run
`tests/font-retina.c` both natively with ASAN/UBSAN and as Emscripten/Node WASM,
passing Ner's `www/blackcastle.ttf`; native-only testing misses this architecture
difference. Ner's `verify-game-over-text.mjs` checks the actual UI at DPR 1/2/3
in Chromium and WebKit. Font appearance, layout and Retina scale must not change.

## NanoVG fill batching

The GL backend batches multi-path stencil fans and AA strips into indexed
triangle lists, retaining primitive order/winding and original draws for
single-path or oversized frames. `nanovg_indices.h` excludes index 0xffff
(WebGL2 fixed restart); CPU index capacity is bounded to 196605 unsigned shorts
per context. Preserve allocation-failure fallback, buffer cleanup, GL binding
cleanup and exact blend/stencil semantics. Run `tests/nanovg-indices.c` with
ASAN/UBSAN and Ner's lighting reference comparisons (`verify-lighting.mjs`, also
`--webkit`), particles, additive graphics, Retina and persistence tests when
changing this path. Desktop draw-count reductions are not iPhone FPS proof.

## Coverage triangles

Plugin opcode 19 is a generic black-alpha triangle mask with two affine distance
fields mapped through unit-disk coverage. `engines/coverage_triangles.h` draws
bounded batches (1536 vertices) with max/replace blending and logical scissor
bounds. It is accepted only before NanoVG commands in a fresh minimum-alpha
layer; do not reorder deferred paths or bypass this guard. Geometry belongs to
the application plugin. `plugins/coverage_validation.h` validates packet shape,
finite values and opacity before GPU access. Run `tests/coverage-command.c` under
ASAN/UBSAN and Ner's penumbra, particles, additive, Retina and persistence browser
tests after changes, including `verify-penumbra-sprite-state.mjs` for deferred
terrain with multiple lights. Restore the scissor **rectangle**, not just its
enable bit: `sg_reset_state_cache()` enables scissoring without resetting the box.
Restore blend equation/scissor/bindings and Sokol state;
release GPU objects at shutdown. Unsupported backends reject this optional opcode.

## Minimum-alpha first layer

`min_layers.h` renders the first layer straight into the cleared accumulator;
subsequent layers use the scratch target and MIN blend. Preserve black-mask
semantics by zeroing RGB in the final image paint, not with an extra masked clear.
The accumulator's internal RGB is not meaningful: consumers (including particles)
must sample alpha only. Keep stencil initialization and GL state restoration for
both first and subsequent layers. Ner's `verify-light-layer-first.mjs` compares
old/new runtime pixels, including colored, unbounded and multi-layer masks;
`docs/light-layer-first.md` records the qualified desktop benchmark results.

## Reduced-resolution minimum-alpha layers

`Runtime.setLightLayerResolution(1 | 0.5 | 0.25)` selects the linear size of both
lighting targets, lazily at the next group. Full remains the default; shutdown
resets it. Keep plugin/NanoVG coordinates full-frame, scale coverage scissors to
actual target dimensions and preserve full-frame mask UVs for particles. Only
lighting rasterization changes; world/UI/particle framebuffers must remain native
resolution. A reduced-target MSAA/subpixel-coverage trial was rolled back after
a reported production black screen despite passing local browser tests. Keep the
single-sample framebuffer path until that failure is diagnosed on the affected
device. Reduced modes now rasterize layers into a reusable full-resolution scratch
framebuffer and box-filter 2×2/4×4 samples straight into the reduced accumulator
using an ordinary MIN-blended shader (no MSAA/blits or extra reduced scratch pass). Keep Full unchanged and coverage scissors tied
to the actual raster target. Release scratch on resize/Full/shutdown; shader failure
falls back to the old coarse path. Include the extra raster/downsample cost in
profiling and do not describe Quarter as reduced-resolution shadow rasterization.
Reduced analytic coverage has an optional four-subpixel fallback shader;
Full keeps the original shader and optional compile/link failure falls back to
it (cache failure, clean up both programs on shutdown). Ner's
`RESAMPLE_FAILURE=1 FILTER_FAILURE=1 node scripts/verify-light-resolution.mjs`
exercises both shader fallbacks.
No plugin ABI change is required. The setter rejects invalid scales
and returns false on unsupported backends or during an active group. Run Ner's
`verify-light-resolution.mjs`, `verify-light-resolution-game.mjs`, reduced-scale
particle/scissor tests and full-resolution pixel-reference comparisons in both
browser engines. Reduced pixels are intentionally not parity-equivalent; do not
claim a phone speedup from pixel-count reductions alone.

## Conservative layer bounds and transient attachments

`Runtime.setLightLayerBounds(left, top, right, bottom)` supplies a logical-screen
influence hint; no arguments clears it. The caller must guarantee that regions
outside it cannot lower accumulator alpha. First-layer rendering stays full-frame;
subsequent composition scissors include a two-output-pixel guard band. Geometry
rasterization and full clears remain unchanged. Always restore the prior scissor
rectangle before Sokol cache reset. Attachment invalidation is restricted to old
contents before clear and dead stencil after rasterization; never invalidate the
accumulator color consumed by particles/presentation. Verify pixel-reference
comparisons at Full/Quarter, colored/unbounded masks, particle masks and deferred
sprite scissor state in Chromium and WebKit after changing this path.

## Optional binary screen overlay

`engines/screen_overlay.h` exposes a bounded R8 mask upload and explicit world/UI
boundary on GLES3. Masks are an R8 texture array with 1–16 cached frames and a
16 MiB total byte limit. `setScreenOverlayFrame` only selects a validated index;
never reupload masks during animation. Cull tiles only if empty in every frame.
Prepare before loading fonts: the second NanoVG context borrows
the same font source bytes and owns its own glyph atlas. Finish world NanoVG and
masked particles before the overlay; finish UI NanoVG before sprite layer 131.
Do not move particles above the overlay or duplicate world draws. Empty mask tiles
are omitted; clear fragments discard. Optional finite opacity [0,1] uses
premultiplied blending below 1, skips drawing at 0, and preserves the original
no-blend path at the default 1.
Preserve the scissor rectangle and Sokol state, retain old masks on failed uploads,
skip stale-size masks and release GPU/UI resources at shutdown. Ner's
`verify-vignette.mjs` (Chromium and WebKit/DPR3) tests binary pixels, masked dust,
UI/fonts, resize, upload counts and blend state. No iPhone speedup is established.

## GPU diagnostics

`engines/gpu_profile.h` adds opt-in sampled counters and WebGL timer queries.
Preserve non-overlapping actual-submission scopes, bounded 96-query storage,
ready-only reads, disjoint/context-loss invalidation and cleanup on disable.
Never wait for GPU results or split NanoVG/Sokol batches for attribution. The
native GLES backend reports timing unavailable, not CPU estimates. Count raw
runtime/NanoVG GL calls plus public Sokol draw counters without double-counting.
Per-engine work counters describe submissions, not draws or GPU time. Ner's
`test-gpu-timer-pool.mjs` tests the real EM_JS lifecycle with a simulated driver;
`verify-gpu-profile.mjs` verifies browser draw accounting and opt-in lifecycle.

## Animation frame clock

`Runtime.frameNow()` is latched at `engine_frame()` entry. Web reads the public
`document.timeline.currentTime`, equal to the window rAF callback timestamp;
native samples `CLOCK_MONOTONIC`. Missing/null browser timelines fall back to a
single monotonic sample, with nondecreasing timestamps. Keep `Runtime.now()` live
for profiling/deadlines. Do not replace this with an accumulated/smoothed frame
duration, add a competing rAF loop, or read private Sokol timing fields. Ner's
`verify-frame-clock.mjs` checks real callback timestamps under injected scheduling
jitter, intra-frame consistency and fallback behavior in Chromium/WebKit.

## Local hot reload

`sapp/hot_reload.c` provides the generic `__runtimeHotReload` save/status/handoff
bridge and opt-in web transport. `hot_reload_ios.m` polls an application-supplied
`hot-reload.json`, stages SHA-256-verified complete writable generations, and
atomically activates them. The optional `ITERATION_HOT_RELOAD_CONFIG` build hook
copies the application's config; no server address is a runtime default.
iOS Release defines `ITERATION_HOT_RELOAD_NO_WASM`: hot reload remains enabled,
but staging skips WASM and asset resolution pins WASM to the installed bundle,
ignoring additions/deletions and stale overrides. Native static-plugin builds
also enforce this policy. Debug status reports `WASM locked`. Application Support
overrides survive relaunch but are keyed to the installed config/entry point and
WASM update policy. Ner's `test-hot-reload-ios.mjs --release` checks this policy;
`verify-wasm2c.mjs` verifies repeated static-plugin teardown/reinitialization.

All asset requests must use `asset_fetch` and `hot_reload_asset_path`, including
models/plugins. Never reset from inside a callback: drain asset requests, capture
application state, then stop native subsystems/workers before discarding the VM.
`runtime_vm.h` owns a per-VM allocation domain because legacy JS bindings retain
references; every external/native resource needs explicit shutdown before its
bulk release. Register QuickJS class prototypes per VM but class IDs only once
per process. Particle finalizers must not destroy systems after native shutdown.
Ner's `test-hot-reload-ios.mjs` tests the actual Foundation transport without a
phone; `verify-hot-reload.mjs --in-place` exercises repeated C VM/GPU resets using
a temporary web transport override. Run normal browser reload tests as well.

## Coding notes

- Use public Sokol APIs. Current Sokol uses image views, separate samplers, `sg_begin_pass()`, `sglue_environment()`, and `sglue_swapchain()`.
- Use public QuickJS APIs; do not depend on internal array layouts.
- Add runtime assets to `iteration-assets.yml`; merely placing a file in `sapp/data/` does not deploy it.
