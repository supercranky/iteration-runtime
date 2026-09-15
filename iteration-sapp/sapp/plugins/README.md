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

`example.c` is the runtime's smoke-test plugin. Production plugins belong to the
application repository that owns them and should use this directory's
`iteration_plugin.h` as their SDK. Applications are responsible for compiling,
packaging, loading, and calling their plugins.

Application-owned `.wasm` plugins can be packaged as iOS resources and run
through WAMR in both Debug and Release builds. Native plugins remain an optional
alternative and must be registered at build time because iOS cannot load
unsigned native code dynamically.
