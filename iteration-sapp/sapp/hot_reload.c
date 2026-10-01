#include "hot_reload.h"
#include "util/fileutil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
EM_JS(void, hot_reload_web_start, (), {
  if (Module.hotReload) return;
  const option = new URLSearchParams(location.search).get('hotReload');
  const h = Module.hotReload = {status:'Disabled', pending:null, base:null};
  if (!option) return;
  h.base = option === '1' ? location.origin : (option.endsWith('/') ? option.slice(0,-1) : option);
  h.status = 'Connecting';
  let last = null;
  try { last = sessionStorage.getItem('iteration.hotRevision'); } catch (_) {}
  async function poll() {
    try {
      const response = await fetch(h.base + '/__hot/manifest', {cache:'no-store', signal:AbortSignal.timeout(5000)});
      if (!response.ok) throw Error('Manifest unavailable');
      const manifest = await response.json();
      if (manifest.protocol !== 1 || typeof manifest.id !== 'string') throw Error('Invalid manifest');
      if (last && last !== manifest.id) h.pending = manifest.id;
      last = manifest.id;
      h.status = h.pending ? 'Waiting for safe state' : 'Connected';
    } catch (_) { h.status = 'Disconnected — retrying'; }
    setTimeout(poll, 1000);
  }
  poll();
});
void hot_reload_start(void) {hot_reload_web_start();}
EM_JS(int, hot_reload_web_pending, (), { return !!Module.hotReload?.pending; });
int hot_reload_pending(void) {return hot_reload_web_pending();}
EM_JS(int, hot_reload_web_commit, (const char *state), {
  const h = Module.hotReload;
  try {
    sessionStorage.setItem('iteration.hotState', UTF8ToString(state));
    sessionStorage.setItem('iteration.hotRevision', h.pending);
    location.reload();
    h.pending = null;
    return 2; // Suspend the old VM while the browser navigates.
  } catch (_) { h.status = 'Error: reload state could not be stored'; }
  return 0;
});
int hot_reload_commit(const char *state) {return hot_reload_web_commit(state);}
EM_JS(char *, hot_reload_web_take_state, (), {
  try {
    const value = sessionStorage.getItem('iteration.hotState');
    sessionStorage.removeItem('iteration.hotState');
    return value === null ? 0 : stringToNewUTF8(value);
  } catch (_) { return 0; }
});
char *hot_reload_take_state(void) {return hot_reload_web_take_state();}
EM_JS(char *, hot_reload_web_status, (), { return stringToNewUTF8(Module.hotReload?.status || 'Disabled'); });
const char *hot_reload_status(void) {
  static char text[128]; char *value=hot_reload_web_status();
  snprintf(text,sizeof(text),"%s",value?value:"Disabled");free(value);return text;
}
const char *hot_reload_asset_path(const char *name,char *buffer,size_t size) {return fileutil_get_path(name,buffer,size);}
#elif !(defined(__APPLE__) && TARGET_OS_IOS)
void hot_reload_start(void) {}
int hot_reload_pending(void) {return 0;}
int hot_reload_commit(const char *state) {(void)state;return 0;}
const char *hot_reload_status(void) {return "Disabled";}
char *hot_reload_take_state(void) {return NULL;}
const char *hot_reload_asset_path(const char *name,char *buffer,size_t size) {return fileutil_get_path(name,buffer,size);}
#endif

static JSValue save_callback=JS_UNDEFINED;
static JSValue set_save(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;
  if(argc!=1||!JS_IsFunction(ctx,argv[0]))return JS_ThrowTypeError(ctx,"Expected hot reload save callback");
  JS_FreeValue(ctx,save_callback);save_callback=JS_DupValue(ctx,argv[0]);return JS_UNDEFINED;
}
static JSValue get_status(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;(void)argc;(void)argv;return JS_NewString(ctx,hot_reload_status());
}
static JSValue take_state(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;(void)argc;(void)argv;char *text=hot_reload_take_state();
  JSValue result=text?JS_NewString(ctx,text):JS_NULL;free(text);return result;
}
void hot_reload_install(JSContext *ctx) {
  JSValue global=JS_GetGlobalObject(ctx),api=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,api,"setSaveState",JS_NewCFunction(ctx,set_save,"setSaveState",1));
  JS_SetPropertyStr(ctx,api,"status",JS_NewCFunction(ctx,get_status,"status",0));
  JS_SetPropertyStr(ctx,api,"takeState",JS_NewCFunction(ctx,take_state,"takeState",0));
  JS_SetPropertyStr(ctx,global,"__runtimeHotReload",api);JS_FreeValue(ctx,global);
}
void hot_reload_forget(JSContext *ctx) {JS_FreeValue(ctx,save_callback);save_callback=JS_UNDEFINED;}
int hot_reload_tick(JSContext *ctx) {
  if(!hot_reload_pending()||!JS_IsFunction(ctx,save_callback))return 0;
  JSValue result=JS_Call(ctx,save_callback,JS_UNDEFINED,0,NULL);
  if(JS_IsException(result)) {
    JSValue error=JS_GetException(ctx);const char *text=JS_ToCString(ctx,error);
    fprintf(stderr,"Hot reload save failed: %s\n",text?text:"exception");
    JS_FreeCString(ctx,text);JS_FreeValue(ctx,error);
  }
  int reset=0;
  if(JS_IsString(result)) {
    size_t length;const char *text=JS_ToCStringLen(ctx,&length,result);
    if(text&&length<=8*1024*1024)reset=hot_reload_commit(text);
    JS_FreeCString(ctx,text);
  }
  JS_FreeValue(ctx,result);return reset;
}
