# Iteration plugins

Plugins implement `iteration.plugin/1` from `iteration_plugin.h`. The runtime
loads the same interface through browser WebAssembly, WAMR on iOS Debug, and a
statically compiled native implementation on iOS Release.

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

`example.c` is the smoke-test plugin. `visibility.c` is the reference
production plugin: it owns map/reveal/torch state, returns sparse reveal patches,
and emits the complete shadow/falloff command buffer. The JavaScript compatibility
wrappers preserve `visibilitySetMap()` and `visibilityDraw()` while loading it.
Its direct methods are `setMap(id, width, height, tiles, floorTile, wallTile)` and
`draw(id, originX, originY, parentX, parentY, parentScale, lightX, lightY,
maxLength, frameDuration, frameIndex, left, right, top, bottom)`.

Add production plugins to the native registry when configuring an iOS Release
build. Bundled native plugins must be known at build time because iOS cannot load
unsigned native code dynamically.
