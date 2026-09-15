#pragma once

#include "quickjs.h"
#include <stddef.h>
#include <stdint.h>

typedef int (*wasm_plugin_render_fn)(const uint8_t *commands, size_t size);

void wasm_plugins_init(JSContext *ctx, wasm_plugin_render_fn render);
void wasm_plugins_shutdown(void);

JSValue js_engine_load_wasm(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
JSValue js_engine_call_wasm(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
