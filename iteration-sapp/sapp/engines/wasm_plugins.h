#pragma once

#include "quickjs.h"
#include <stddef.h>
#include <stdint.h>

// CPU-only diagnostic totals. Disabled by default; detailed mode adds clocks
// around each graphics command and must be measured separately from coarse mode.
typedef struct {
  int enabled, detailed, active;
  uint64_t calls, failures, input_bytes, value_bytes, render_bytes, commands;
  uint64_t layer_commands, path_commands, paint_commands, fill_commands;
  double total_ms, plugin_ms, result_copy_ms, render_ms;
  double layer_ms, path_ms, paint_ms, fill_ms;
} wasm_plugin_profile;
extern wasm_plugin_profile wasm_profile;
double wasm_profile_now(void);
JSValue js_engine_set_plugin_profiling(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_engine_get_plugin_profile(JSContext *, JSValueConst, int, JSValueConst *);

typedef int (*wasm_plugin_render_fn)(const uint8_t *commands, size_t size);

void wasm_plugins_init(JSContext *ctx, wasm_plugin_render_fn render);
void wasm_plugins_shutdown(void);

JSValue js_engine_load_wasm(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
JSValue js_engine_call_wasm(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
