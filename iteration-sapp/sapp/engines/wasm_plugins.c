#include "wasm_plugins.h"
#include "../plugins/iteration_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(ITERATION_WAMR)
#include "sokol_fetch.h"
#include "util/fileutil.h"
#include "wasm_export.h"
#endif

#define ITERATION_MAX_WASM_LOADS 64
#define ITERATION_MAX_WASM_PLUGINS 32
#define ITERATION_WASM_FETCH_SIZE (16 * 1024 * 1024)

#if defined(ITERATION_NATIVE_PLUGINS)
typedef struct iteration_native_plugin_api
{
  const char *name;
  uint32_t (*abi_version)(void);
  iteration_plugin_ptr (*manifest_ptr)(void);
  uint32_t (*manifest_len)(void);
  iteration_plugin_ptr (*alloc)(uint32_t, uint32_t);
  void (*free)(iteration_plugin_ptr, uint32_t, uint32_t);
  iteration_plugin_ptr (*call)(uint32_t, iteration_plugin_ptr, uint32_t);
} iteration_native_plugin_api;
#define ITERATION_DECLARE_NATIVE_PLUGIN(name) \
  uint32_t iteration_##name##_abi_version(void); \
  iteration_plugin_ptr iteration_##name##_manifest_ptr(void); \
  uint32_t iteration_##name##_manifest_len(void); \
  iteration_plugin_ptr iteration_##name##_alloc(uint32_t, uint32_t); \
  void iteration_##name##_free(iteration_plugin_ptr, uint32_t, uint32_t); \
  iteration_plugin_ptr iteration_##name##_call(uint32_t, iteration_plugin_ptr, uint32_t)
ITERATION_DECLARE_NATIVE_PLUGIN(example);
ITERATION_DECLARE_NATIVE_PLUGIN(visibility);
static const iteration_native_plugin_api native_plugin_registry[] = {
  {"example", iteration_example_abi_version, iteration_example_manifest_ptr,
   iteration_example_manifest_len, iteration_example_alloc, iteration_example_free,
   iteration_example_call},
  {"visibility", iteration_visibility_abi_version, iteration_visibility_manifest_ptr,
   iteration_visibility_manifest_len, iteration_visibility_alloc, iteration_visibility_free,
   iteration_visibility_call}
};
#endif

typedef struct wasm_load_request
{
  int active;
  JSContext *ctx;
  JSValue callback;
} wasm_load_request;

#if defined(ITERATION_WAMR)
typedef struct wasm_native_plugin
{
  uint8_t *bytes;
  uint32_t byte_count;
  wasm_module_t module;
  wasm_module_inst_t instance;
  wasm_exec_env_t exec_env;
} wasm_native_plugin;
static wasm_native_plugin native_plugins[ITERATION_MAX_WASM_PLUGINS];
static int native_plugin_count;
#endif

static struct
{
  JSContext *ctx;
  wasm_plugin_render_fn render;
  wasm_load_request loads[ITERATION_MAX_WASM_LOADS];
  int next_request;
} wasm_state;

#if defined(__EMSCRIPTEN__)
EM_JS(void, iteration_wasm_web_load, (int request_id, const char *path), {
  if (!Module.iterationWasmPlugins) {
    Module.iterationWasmPlugins = [];
  }
  const url = UTF8ToString(path);
  const imports = { env: {} };
  const instantiate = WebAssembly.instantiateStreaming
    ? WebAssembly.instantiateStreaming(fetch(url), imports).catch(async () => {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
        return WebAssembly.instantiate(await response.arrayBuffer(), imports);
      })
    : fetch(url).then(response => {
        if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
        return response.arrayBuffer();
      }).then(bytes => WebAssembly.instantiate(bytes, imports));

  instantiate.then(result => {
    const instance = result.instance || result;
    const exports = instance.exports;
    if (!exports.memory || !exports.iteration_abi_version ||
        !exports.iteration_manifest_ptr || !exports.iteration_manifest_len ||
        !exports.iteration_alloc || !exports.iteration_free || !exports.iteration_call) {
      throw new Error('module does not implement iteration.plugin/1');
    }
    if (exports.iteration_abi_version() !== 1) {
      throw new Error('unsupported Iteration plugin ABI');
    }
    const memory = new Uint8Array(exports.memory.buffer);
    const manifestPtr = exports.iteration_manifest_ptr();
    const manifestLen = exports.iteration_manifest_len();
    if (manifestLen > 65536 || manifestPtr > memory.byteLength - manifestLen)
      throw new Error('invalid Iteration plugin manifest');
    const manifest = new TextDecoder().decode(memory.subarray(manifestPtr, manifestPtr + manifestLen));
    const handle = Module.iterationWasmPlugins.length;
    Module.iterationWasmPlugins.push({ instance, manifest, last: null });
    const text = stringToNewUTF8(manifest);
    _iteration_wasm_web_loaded(request_id, handle, text, 0);
    _free(text);
  }).catch(error => {
    const text = stringToNewUTF8(String(error && error.message || error));
    _iteration_wasm_web_loaded(request_id, -1, text, 1);
    _free(text);
  });
});

EM_JS(int, iteration_wasm_web_call,
      (int handle, int method, const uint8_t *input, int input_len, uint32_t *meta), {
  const plugin = Module.iterationWasmPlugins && Module.iterationWasmPlugins[handle];
  if (!plugin) return -1;
  const exports = plugin.instance.exports;
  const inputPtr = exports.iteration_alloc(input_len, 8);
  if (input_len && !inputPtr) return -2;
  let memory = new Uint8Array(exports.memory.buffer);
  if (input_len) memory.set(HEAPU8.subarray(input, input + input_len), inputPtr);
  let resultPtr;
  try {
    resultPtr = exports.iteration_call(method, inputPtr, input_len);
  } catch (error) {
    exports.iteration_free(inputPtr, input_len, 8);
    console.error(`Iteration plugin call trapped: ${error}`);
    return -3;
  }
  memory = new Uint8Array(exports.memory.buffer);
  if (resultPtr > memory.byteLength - 20) {
    exports.iteration_free(inputPtr, input_len, 8);
    return -4;
  }
  const view = new DataView(exports.memory.buffer, resultPtr, 20);
  const status = view.getInt32(0, true);
  const valuePtr = view.getUint32(4, true);
  const valueLen = view.getUint32(8, true);
  const renderPtr = view.getUint32(12, true);
  const renderLen = view.getUint32(16, true);
  if (valueLen > 16777216 || renderLen > 16777216 ||
      valuePtr > memory.byteLength - valueLen || renderPtr > memory.byteLength - renderLen) {
    exports.iteration_free(inputPtr, input_len, 8);
    return -4;
  }
  plugin.last = {
    value: memory.slice(valuePtr, valuePtr + valueLen),
    render: memory.slice(renderPtr, renderPtr + renderLen)
  };
  exports.iteration_free(inputPtr, input_len, 8);
  HEAPU32[meta >> 2] = valueLen;
  HEAPU32[(meta >> 2) + 1] = renderLen;
  return status;
});

EM_JS(void, iteration_wasm_web_copy_result,
      (int handle, uint8_t *value, uint8_t *render), {
  const plugin = Module.iterationWasmPlugins[handle];
  if (!plugin || !plugin.last) return;
  HEAPU8.set(plugin.last.value, value);
  HEAPU8.set(plugin.last.render, render);
  plugin.last = null;
});

EMSCRIPTEN_KEEPALIVE
void iteration_wasm_web_loaded(int request_id, int handle, const char *message, int failed)
{
  if (request_id < 0 || request_id >= ITERATION_MAX_WASM_LOADS)
    return;
  wasm_load_request *request = &wasm_state.loads[request_id];
  if (!request->active)
    return;

  JSContext *ctx = request->ctx;
  JSValue argument;
  if (failed)
  {
    argument = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, argument, "error", JS_NewString(ctx, message));
  }
  else
  {
    argument = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, argument, "id", JS_NewInt32(ctx, handle));
    JS_SetPropertyStr(ctx, argument, "manifest", JS_NewString(ctx, message));
  }
  JSValue result = JS_Call(ctx, request->callback, JS_UNDEFINED, 1,
                           (JSValueConst *)&argument);
  JS_FreeValue(ctx, result);
  JS_FreeValue(ctx, argument);
  JS_FreeValue(ctx, request->callback);
  memset(request, 0, sizeof(*request));
}
#elif defined(ITERATION_WAMR)
static int wamr_call_u32(wasm_native_plugin *plugin, const char *name,
                         uint32_t argc, uint32_t *argv, uint32_t *result)
{
  wasm_function_inst_t function = wasm_runtime_lookup_function(plugin->instance, name);
  if (!function || !wasm_runtime_call_wasm(plugin->exec_env, function, argc, argv))
    return 0;
  if (result) *result = argv[0];
  return 1;
}

static void wasm_native_load_complete(int request_id, int handle,
                                      const char *message, int failed)
{
  wasm_load_request *request = &wasm_state.loads[request_id];
  JSContext *ctx = request->ctx;
  JSValue argument = JS_NewObject(ctx);
  if (failed)
    JS_SetPropertyStr(ctx, argument, "error", JS_NewString(ctx, message));
  else
  {
    JS_SetPropertyStr(ctx, argument, "id", JS_NewInt32(ctx, handle));
    JS_SetPropertyStr(ctx, argument, "manifest", JS_NewString(ctx, message));
  }
  JSValue call_result = JS_Call(ctx, request->callback, JS_UNDEFINED, 1,
                                (JSValueConst *)&argument);
  JS_FreeValue(ctx, call_result);
  JS_FreeValue(ctx, argument);
  JS_FreeValue(ctx, request->callback);
  memset(request, 0, sizeof(*request));
}

static void wasm_native_fetch_callback(const sfetch_response_t *response)
{
  int request_id = *(const int *)response->user_data;
  wasm_load_request *request = &wasm_state.loads[request_id];
  if (!request->active) return;
  if (response->failed)
  {
    free((void *)response->buffer.ptr);
    wasm_native_load_complete(request_id, -1, "unable to load plugin", 1);
    return;
  }
  if (native_plugin_count >= ITERATION_MAX_WASM_PLUGINS)
  {
    free((void *)response->buffer.ptr);
    wasm_native_load_complete(request_id, -1, "too many loaded plugins", 1);
    return;
  }

  char error[256] = {0};
  wasm_native_plugin *plugin = &native_plugins[native_plugin_count];
  plugin->bytes = (uint8_t *)response->buffer.ptr;
  plugin->byte_count = (uint32_t)response->data.size;
  plugin->module = wasm_runtime_load(plugin->bytes, plugin->byte_count,
                                     error, sizeof(error));
  if (!plugin->module ||
      !(plugin->instance = wasm_runtime_instantiate(plugin->module, 64 * 1024,
                                                     64 * 1024, error, sizeof(error))) ||
      !(plugin->exec_env = wasm_runtime_create_exec_env(plugin->instance, 64 * 1024)))
  {
    if (plugin->instance) wasm_runtime_deinstantiate(plugin->instance);
    if (plugin->module) wasm_runtime_unload(plugin->module);
    free(plugin->bytes);
    memset(plugin, 0, sizeof(*plugin));
    wasm_native_load_complete(request_id, -1, error[0] ? error : "invalid plugin", 1);
    return;
  }

  uint32_t no_args[1] = {0}, version = 0, manifest_ptr = 0, manifest_len = 0;
  if (!wamr_call_u32(plugin, "iteration_abi_version", 0, no_args, &version) ||
      version != ITERATION_PLUGIN_ABI_VERSION ||
      !wamr_call_u32(plugin, "iteration_manifest_ptr", 0, no_args, &manifest_ptr) ||
      !wamr_call_u32(plugin, "iteration_manifest_len", 0, no_args, &manifest_len) ||
      manifest_len > 65536 ||
      !wasm_runtime_validate_app_addr(plugin->instance, manifest_ptr, manifest_len))
  {
    wasm_runtime_destroy_exec_env(plugin->exec_env);
    wasm_runtime_deinstantiate(plugin->instance);
    wasm_runtime_unload(plugin->module);
    free(plugin->bytes);
    memset(plugin, 0, sizeof(*plugin));
    wasm_native_load_complete(request_id, -1, "module does not implement iteration.plugin/1", 1);
    return;
  }
  const char *manifest_bytes = wasm_runtime_addr_app_to_native(plugin->instance, manifest_ptr);
  char *manifest = malloc((size_t)manifest_len + 1);
  if (!manifest)
  {
    wasm_runtime_destroy_exec_env(plugin->exec_env);
    wasm_runtime_deinstantiate(plugin->instance);
    wasm_runtime_unload(plugin->module);
    free(plugin->bytes);
    memset(plugin, 0, sizeof(*plugin));
    wasm_native_load_complete(request_id, -1, "out of memory", 1);
    return;
  }
  memcpy(manifest, manifest_bytes, manifest_len);
  manifest[manifest_len] = 0;
  int handle = native_plugin_count++;
  wasm_native_load_complete(request_id, handle, manifest, 0);
  free(manifest);
}
#endif

void wasm_plugins_init(JSContext *ctx, wasm_plugin_render_fn render)
{
  memset(&wasm_state, 0, sizeof(wasm_state));
  wasm_state.ctx = ctx;
  wasm_state.render = render;
#if defined(ITERATION_WAMR)
  RuntimeInitArgs args;
  memset(&args, 0, sizeof(args));
  args.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&args))
    fprintf(stderr, "Unable to initialize WAMR\n");
#endif
}

void wasm_plugins_shutdown(void)
{
  for (int i = 0; i < ITERATION_MAX_WASM_LOADS; i++)
  {
    if (wasm_state.loads[i].active)
      JS_FreeValue(wasm_state.loads[i].ctx, wasm_state.loads[i].callback);
  }
  memset(&wasm_state, 0, sizeof(wasm_state));
#if defined(ITERATION_WAMR)
  for (int i = 0; i < native_plugin_count; i++)
  {
    wasm_runtime_destroy_exec_env(native_plugins[i].exec_env);
    wasm_runtime_deinstantiate(native_plugins[i].instance);
    wasm_runtime_unload(native_plugins[i].module);
    free(native_plugins[i].bytes);
  }
  native_plugin_count = 0;
  wasm_runtime_destroy();
#endif
}

JSValue js_engine_load_wasm(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
  if (argc < 2 || !JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "loadWasm requires a path and callback");
#if defined(__EMSCRIPTEN__)
  int request_id = -1;
  for (int offset = 0; offset < ITERATION_MAX_WASM_LOADS; offset++)
  {
    int candidate = (wasm_state.next_request + offset) % ITERATION_MAX_WASM_LOADS;
    if (!wasm_state.loads[candidate].active)
    {
      request_id = candidate;
      wasm_state.next_request = (candidate + 1) % ITERATION_MAX_WASM_LOADS;
      break;
    }
  }
  if (request_id < 0)
    return JS_ThrowInternalError(ctx, "too many pending WebAssembly loads");

  const char *requested = JS_ToCString(ctx, argv[0]);
  if (!requested)
    return JS_EXCEPTION;
  size_t length = strlen(requested);
  int has_suffix = length >= 5 && strcmp(requested + length - 5, ".wasm") == 0;
  char *path = malloc(length + (has_suffix ? 1 : 6));
  if (!path)
  {
    JS_FreeCString(ctx, requested);
    return JS_ThrowOutOfMemory(ctx);
  }
  strcpy(path, requested);
  if (!has_suffix)
    strcat(path, ".wasm");

  wasm_state.loads[request_id].active = 1;
  wasm_state.loads[request_id].ctx = ctx;
  wasm_state.loads[request_id].callback = JS_DupValue(ctx, argv[1]);
  iteration_wasm_web_load(request_id, path);
  free(path);
  JS_FreeCString(ctx, requested);
  return JS_UNDEFINED;
#elif defined(ITERATION_WAMR)
  int request_id = -1;
  for (int offset = 0; offset < ITERATION_MAX_WASM_LOADS; offset++)
  {
    int candidate = (wasm_state.next_request + offset) % ITERATION_MAX_WASM_LOADS;
    if (!wasm_state.loads[candidate].active) { request_id = candidate; break; }
  }
  if (request_id < 0)
    return JS_ThrowInternalError(ctx, "too many pending WebAssembly loads");
  const char *requested = JS_ToCString(ctx, argv[0]);
  if (!requested) return JS_EXCEPTION;
  size_t length = strlen(requested);
  int has_suffix = length >= 5 && strcmp(requested + length - 5, ".wasm") == 0;
  char path[512], asset_path[512];
  int path_length = snprintf(path, sizeof(path), "%s%s", requested,
                             has_suffix ? "" : ".wasm");
  JS_FreeCString(ctx, requested);
  if (path_length < 0 || path_length >= (int)sizeof(path))
    return JS_ThrowRangeError(ctx, "WebAssembly plugin path is too long");
  uint8_t *buffer = malloc(ITERATION_WASM_FETCH_SIZE);
  if (!buffer) return JS_ThrowOutOfMemory(ctx);
  wasm_state.loads[request_id].active = 1;
  wasm_state.loads[request_id].ctx = ctx;
  wasm_state.loads[request_id].callback = JS_DupValue(ctx, argv[1]);
  sfetch_send(&(sfetch_request_t){
      .path = fileutil_get_path(path, asset_path, sizeof(asset_path)),
      .callback = wasm_native_fetch_callback,
      .buffer = {.ptr = buffer, .size = ITERATION_WASM_FETCH_SIZE},
      .user_data = {.ptr = &request_id, .size = sizeof(request_id)}});
  return JS_UNDEFINED;
#elif defined(ITERATION_NATIVE_PLUGINS)
  const char *requested = JS_ToCString(ctx, argv[0]);
  if (!requested) return JS_EXCEPTION;
  int registry_count = (int)(sizeof(native_plugin_registry) / sizeof(native_plugin_registry[0]));
  int plugin_id = -1;
  for (int i = 0; i < registry_count; i++)
  {
    size_t name_len = strlen(native_plugin_registry[i].name);
    if (strcmp(requested, native_plugin_registry[i].name) == 0 ||
        (strncmp(requested, native_plugin_registry[i].name, name_len) == 0 &&
         strcmp(requested + name_len, ".wasm") == 0))
    {
      plugin_id = i;
      break;
    }
  }
  JS_FreeCString(ctx, requested);
  if (plugin_id < 0)
    return JS_ThrowReferenceError(ctx, "native plugin is not registered");
  const iteration_native_plugin_api *plugin = &native_plugin_registry[plugin_id];
  if (plugin->abi_version() != ITERATION_PLUGIN_ABI_VERSION)
    return JS_ThrowInternalError(ctx, "native plugin ABI is unsupported");
  const char *manifest = (const char *)(uintptr_t)plugin->manifest_ptr();
  JSValue descriptor = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, descriptor, "id", JS_NewInt32(ctx, plugin_id));
  JS_SetPropertyStr(ctx, descriptor, "manifest",
                    JS_NewStringLen(ctx, manifest, plugin->manifest_len()));
  JSValue call_result = JS_Call(ctx, argv[1], JS_UNDEFINED, 1,
                                (JSValueConst *)&descriptor);
  JS_FreeValue(ctx, call_result);
  JS_FreeValue(ctx, descriptor);
  return JS_UNDEFINED;
#else
  return JS_ThrowInternalError(ctx, "WebAssembly plugins are not enabled for this platform");
#endif
}

JSValue js_engine_call_wasm(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
  int32_t handle, method;
  size_t input_len = 0;
  uint8_t *input;
  if (argc < 3 || JS_ToInt32(ctx, &handle, argv[0]) ||
      JS_ToInt32(ctx, &method, argv[1]) ||
      !(input = JS_GetArrayBuffer(ctx, &input_len, argv[2])))
    return JS_ThrowTypeError(ctx, "callWasm requires a handle, method and ArrayBuffer");
  if (input_len > ITERATION_WASM_FETCH_SIZE)
    return JS_ThrowRangeError(ctx, "WebAssembly plugin input is too large");
#if defined(__EMSCRIPTEN__)
  uint32_t meta[2] = {0, 0};
  int status = iteration_wasm_web_call(handle, method, input, (int)input_len, meta);
  if (status != 0)
    return JS_ThrowInternalError(ctx, "WebAssembly plugin call failed (%d)", status);
  if (meta[0] > ITERATION_WASM_FETCH_SIZE || meta[1] > ITERATION_WASM_FETCH_SIZE)
    return JS_ThrowRangeError(ctx, "WebAssembly plugin result is too large");

  uint8_t *value = meta[0] ? malloc(meta[0]) : NULL;
  uint8_t *render = meta[1] ? malloc(meta[1]) : NULL;
  if ((meta[0] && !value) || (meta[1] && !render))
  {
    free(value);
    free(render);
    return JS_ThrowOutOfMemory(ctx);
  }
  iteration_wasm_web_copy_result(handle, value, render);
  if (meta[1] && wasm_state.render && !wasm_state.render(render, meta[1]))
  {
    free(value);
    free(render);
    return JS_ThrowInternalError(ctx, "plugin returned invalid render commands");
  }
  JSValue result = JS_NewArrayBufferCopy(ctx, value, meta[0]);
  free(value);
  free(render);
  return result;
#elif defined(ITERATION_WAMR)
  if (handle < 0 || handle >= native_plugin_count)
    return JS_ThrowRangeError(ctx, "invalid WebAssembly plugin handle");
  wasm_native_plugin *plugin = &native_plugins[handle];
  uint32_t alloc_args[3] = {(uint32_t)input_len, 8, 0}, input_ptr = 0;
  if (!wamr_call_u32(plugin, "iteration_alloc", 2, alloc_args, &input_ptr) ||
      (input_len && (!input_ptr || !wasm_runtime_validate_app_addr(
                                       plugin->instance, input_ptr, (uint32_t)input_len))))
    return JS_ThrowInternalError(ctx, "plugin allocation failed");
  if (input_len)
    memcpy(wasm_runtime_addr_app_to_native(plugin->instance, input_ptr), input, input_len);
  uint32_t call_args[3] = {(uint32_t)method, input_ptr, (uint32_t)input_len};
  uint32_t result_ptr = 0;
  int called = wamr_call_u32(plugin, "iteration_call", 3, call_args, &result_ptr);
  uint32_t free_args[3] = {input_ptr, (uint32_t)input_len, 8};
  if (!called || !wasm_runtime_validate_app_addr(plugin->instance, result_ptr, 20))
  {
    wamr_call_u32(plugin, "iteration_free", 3, free_args, NULL);
    return JS_ThrowInternalError(ctx, "WebAssembly plugin call failed");
  }
  const uint32_t *raw = wasm_runtime_addr_app_to_native(plugin->instance, result_ptr);
  int32_t status = (int32_t)raw[0];
  uint32_t value_ptr = raw[1], value_len = raw[2];
  uint32_t render_ptr = raw[3], render_len = raw[4];
  if (status || value_len > ITERATION_WASM_FETCH_SIZE ||
      render_len > ITERATION_WASM_FETCH_SIZE ||
      (value_len && !wasm_runtime_validate_app_addr(plugin->instance, value_ptr, value_len)) ||
      (render_len && !wasm_runtime_validate_app_addr(plugin->instance, render_ptr, render_len)))
  {
    wamr_call_u32(plugin, "iteration_free", 3, free_args, NULL);
    return JS_ThrowInternalError(ctx, "WebAssembly plugin returned an error (%d)", status);
  }
  const uint8_t *render = render_len
      ? wasm_runtime_addr_app_to_native(plugin->instance, render_ptr) : NULL;
  if (render_len && wasm_state.render && !wasm_state.render(render, render_len))
  {
    wamr_call_u32(plugin, "iteration_free", 3, free_args, NULL);
    return JS_ThrowInternalError(ctx, "plugin returned invalid render commands");
  }
  const uint8_t *value = value_len
      ? wasm_runtime_addr_app_to_native(plugin->instance, value_ptr) : NULL;
  JSValue result = JS_NewArrayBufferCopy(ctx, value, value_len);
  wamr_call_u32(plugin, "iteration_free", 3, free_args, NULL);
  return result;
#elif defined(ITERATION_NATIVE_PLUGINS)
  int registry_count = (int)(sizeof(native_plugin_registry) / sizeof(native_plugin_registry[0]));
  if (handle < 0 || handle >= registry_count)
    return JS_ThrowRangeError(ctx, "invalid native plugin handle");
  const iteration_native_plugin_api *plugin = &native_plugin_registry[handle];
  iteration_plugin_ptr input_ptr = plugin->alloc((uint32_t)input_len, 8);
  if (input_len && !input_ptr)
    return JS_ThrowOutOfMemory(ctx);
  if (input_len) memcpy((void *)(uintptr_t)input_ptr, input, input_len);
  iteration_plugin_ptr result_ptr = plugin->call(
      (uint32_t)method, input_ptr, (uint32_t)input_len);
  const iteration_plugin_result *native_result =
      (const iteration_plugin_result *)(uintptr_t)result_ptr;
  if (!native_result || native_result->status ||
      native_result->value_len > ITERATION_WASM_FETCH_SIZE ||
      native_result->render_len > ITERATION_WASM_FETCH_SIZE)
  {
    int status = native_result ? native_result->status : -1;
    plugin->free(input_ptr, (uint32_t)input_len, 8);
    return JS_ThrowInternalError(ctx, "native plugin returned an error (%d)", status);
  }
  const uint8_t *value = (const uint8_t *)(uintptr_t)native_result->value_ptr;
  const uint8_t *render = (const uint8_t *)(uintptr_t)native_result->render_ptr;
  if (native_result->render_len && wasm_state.render &&
      !wasm_state.render(render, native_result->render_len))
  {
    plugin->free(input_ptr, (uint32_t)input_len, 8);
    return JS_ThrowInternalError(ctx, "plugin returned invalid render commands");
  }
  JSValue result = JS_NewArrayBufferCopy(ctx, value, native_result->value_len);
  plugin->free(input_ptr, (uint32_t)input_len, 8);
  return result;
#else
  return JS_ThrowInternalError(ctx, "WebAssembly plugins are not enabled for this platform");
#endif
}
