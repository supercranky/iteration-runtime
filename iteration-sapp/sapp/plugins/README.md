# Iteration plugins

Plugins implement `iteration.plugin/1` from `iteration_plugin.h`. The runtime
loads the same interface through browser WebAssembly and WAMR on iOS Debug and
Release. The host also supports statically compiled native plugins for targets
that explicitly register them.

## Required exports

- `iteration_abi_version()`
- `iteration_manifest_ptr()` / `iteration_manifest_len()`
- `iteration_alloc()` / `iteration_free()`
- `iteration_call(method, input, input_len)`

`iteration_call` returns an `iteration_plugin_result`. Its descriptor, value,
and render bytes must remain valid until the host calls `iteration_free` for
the input allocation; the host copies both outputs first. Value data uses the
packet format implemented by the JavaScript loader: an argument count followed by
`{type, byteLength, bytes}` values. Result values contain one such value without
the argument count. Current type tags are undefined (0), null (1), boolean (2),
f64 (3), UTF-8 (4), bytes (5), and f64 array (6).

## Buffered graphics

The SDK exposes native-looking inline functions such as:

```c
iteration_graphics_writer graphics;
iteration_graphics_init(&graphics, buffer, sizeof(buffer));
graphicsBeginPath(&graphics);
graphicsRect(&graphics, x, y, width, height);
graphicsFillColor(&graphics, 1, 0, 0, 1);
graphicsFill(&graphics);
result.render_len = iteration_graphics_finish(&graphics);
```

These calls only append fixed-size commands to plugin memory. They never cross
the WebAssembly/native boundary. After `iteration_call` returns, the host
validates and executes the entire command buffer through NanoVG.

WebGL2/OpenGLES3 supports generic minimum-alpha layers via `graphicsCommand`:
`ITER_RENDER_MIN_LAYER_FIRST` clears a reusable accumulator and starts a layer;
`ITER_RENDER_MIN_LAYER_NEXT` starts another transparent layer;
`ITER_RENDER_MIN_LAYER_END` flushes it using component-wise GL_MIN;
`ITER_RENDER_MIN_LAYER_PRESENT` flushes and queues the combined image in the
main NanoVG frame. Each begin/end pair must be within one command buffer;
a group can span multiple plugin calls. These full-frame layers preserve normal
NanoVG paths/gradients and are intended for black alpha masks. Metal currently
rejects these commands. Invalid buffers cancel an active layer and restore the
main context. No application-specific geometry is implemented in the host.

`Runtime.now()` exposes a monotonic millisecond timer for profiling CPU work;
it does not include asynchronous GPU completion.

## Opt-in host CPU profiling

```js
Runtime.setPluginProfiling(true);        // Reset counters; coarse timings.
// Run a representative number of frames / callWasm invocations.
Runtime.setPluginProfiling(false);       // Preserve totals.
const coarse = Runtime.getPluginProfile();
Runtime.setPluginProfiling(true, true);  // Separate detailed measurement window.
```

Profiling is disabled by default. Disabled calls do not read profiling clocks.
Counters aggregate all `callWasm` calls while enabled (including failed calls),
not individual plugins/methods. Scope the measurement window accordingly.
`getPluginProfile()` does not reset anything. Re-enabling resets all counters.
All `*_ms` fields are **cumulative milliseconds**, not per-call/frame averages:

- `total_ms`: the complete host call, including argument checks, allocation,
  execution, rendering, result copying and cleanup.
- `plugin_ms`: native/WAMR plugin invocation. On the web it measures the browser
  bridge call, which also includes its input transfer/allocation and bookkeeping;
  it is not strictly comparable to native compute time.
- `result_copy_ms`: creating the QuickJS-owned result ArrayBuffer copy.
- `render_ms`: validating/dispatching/executing the render buffer, including
  layer-balance checking and abort cleanup. This includes synchronous CPU/driver
  time, **not asynchronous GPU completion** or work deferred to the main frame.
- `layer_ms`, `path_ms`, `paint_ms`, `fill_ms`: detailed mode only. Mutually
  exclusive command-category timings *within* `render_ms`, not additional costs.
  Layers include NanoVG flushes and framebuffer/composition operations. Paths
  include move/line/rect/winding. Paint includes colors/gradient/stroke width.
  Fill includes both fill and stroke tessellation/submission. Category timing
  includes command payload validation, decoding and coordinate conversion.

`total_ms - plugin_ms - result_copy_ms - render_ms` is residual host/bridge work,
including measurement overhead. In detailed mode, `render_ms` minus the four
command categories is residual header checks, command dispatch, counting and
measurement overhead. **It is not an isolated validation cost**: validation is
interleaved with execution, and per-command clocks add nontrivial overhead.
Compare coarse and detailed runs rather than treating their totals as equivalent.
Recognized rectangle command sequences are timed as one group but retain their
original five-command counts. Use coarse measurements for optimization comparisons
so reduced diagnostic clock overhead is not mistaken for production savings.

Also returned: `enabled`, `detailed`, `calls`, `failures`, `input_bytes`,
`value_bytes`, `render_bytes`, `commands`, and `layer_commands`, `path_commands`,
`paint_commands`, `fill_commands`. Bytes count inputs passing basic argument
checks, copied values, and buffers submitted to the renderer. Command counts
include successfully dispatched commands before any failure. No command
validation or error handling is bypassed, and the plugin ABI is unchanged.

## Exact path submission fast path

The host recognizes `MoveTo(a,b), LineTo(a,d), LineTo(c,d), LineTo(c,b), Close`
only after checking all five command headers/sizes, finite coordinates and
bit-identical shared endpoints. It submits these through public `nvgRect` only
when its screen-space width/height arithmetic reconstructs the exact endpoint
bits; otherwise it uses the original point operations. No winding/fill commands
are removed. Unrecognized/malformed sequences stay on the ordinary execution
and validation path. Logical command counts are unchanged.

Dimensions/DPI are sampled once per synchronous render-buffer execution. Original
coordinate conversion formulas and float operation order are retained, including
all viewport modes. The minimum-layer shader's sampler location is cached after
linking instead of queried for each light. No framebuffer resolution, blending,
shadow geometry, AA or layer order is changed, and no ABI additions are required.

Compile the bounded decoder regression from the runtime repository root:

```sh
cc -std=c99 -Wall -Wextra -Werror tests/plugin_paths.c -o /tmp/plugin-paths
/tmp/plugin-paths
```

`example.c` is the runtime's smoke-test plugin. Production plugins belong to the
application repository that owns them and should use this directory's
`iteration_plugin.h` as their SDK. Applications are responsible for compiling,
packaging, loading, and calling their plugins.

Application-owned `.wasm` plugins can be packaged as iOS resources and run
through WAMR in both Debug and Release builds. Native plugins remain an optional
alternative and must be registered at build time because iOS cannot load
unsigned native code dynamically.
