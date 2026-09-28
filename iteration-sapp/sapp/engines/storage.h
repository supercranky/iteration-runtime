/* Bounded asynchronous string storage. JS callbacks always run on the main thread. */
#include <stdio.h>
#include <errno.h>
#define STORAGE_LIMIT (8u * 1024u * 1024u)
#define STORAGE_SLOTS 4
static struct storage_request {
  int token, status, op, temporary, missing;
  JSContext *ctx;
  JSValue resolve, reject;
  char key[65], error[160];
  char *input, *output;
  size_t length, output_length;
} storage_requests[STORAGE_SLOTS];
static unsigned storage_token;

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE void iteration_storage_complete(int token, int failed, const char *text, int length) {
  for (int i=0;i<STORAGE_SLOTS;i++) {
    struct storage_request *r=&storage_requests[i];
    if (r->status!=1 || r->token!=token) continue;
    if (failed) { if(text&&length>0)snprintf(r->error,sizeof(r->error),"%.*s",length,text);else snprintf(r->error,sizeof(r->error),"Storage failed"); }
    else if (length<0 || (unsigned)length>STORAGE_LIMIT) snprintf(r->error,sizeof(r->error),"Stored data exceeds limit");
    else if (text) {
      r->output=malloc((size_t)length+1);
      if (!r->output) snprintf(r->error,sizeof(r->error),"Storage allocation failed");
      else { memcpy(r->output,text,(size_t)length);r->output[length]=0;r->output_length=(size_t)length; }
    } else r->missing=1;
    r->status=2;return;
  }
}
EM_JS(void, storage_web_start, (int token,int op,const char *key_ptr,const char *input,int length,int temporary), {
  const key=UTF8ToString(key_ptr);let value=null;
  let settled=false;
  const done=(failed,text)=>{
    if(settled)return;settled=true;
    if(text!==null && typeof text!=='string'){failed=1;text='Stored data is not a string';}
    if(text!==null && lengthBytesUTF8(text)>8*1024*1024){failed=1;text='Stored data exceeds limit';}
    let ptr=0,n=0;
    try { if(text!==null){n=lengthBytesUTF8(text);ptr=_malloc(n+1);if(!ptr)throw Error('Storage allocation failed');stringToUTF8(text,ptr,n+1);}
      _iteration_storage_complete(token,failed,ptr,n);
    } catch(error) {_iteration_storage_complete(token,1,0,0);} finally {if(ptr)_free(ptr);}
  };
  try {
    if(op===1)value=new TextDecoder().decode(HEAPU8.subarray(input,input+length));
    if(temporary){
      const name='iteration:'+key;
      if(op===1)sessionStorage.setItem(name,value);
      if(op===2)sessionStorage.removeItem(name);
      done(0,op===0?sessionStorage.getItem(name):null);return;
    }
    const request=indexedDB.open('iteration-data',1);
    request.onupgradeneeded=()=>{if(!request.result.objectStoreNames.contains('values'))request.result.createObjectStore('values');};
    request.onerror=()=>done(1,String(request.error));
    request.onblocked=()=>done(1,'Storage database upgrade blocked');
    request.onsuccess=()=>{
      const db=request.result;if(settled){db.close();return;}
      let tx;
      const fail=error=>{try{if(tx)tx.abort();}catch(_){}db.close();done(1,String(error));};
      try {
      tx=db.transaction('values',op===0?'readonly':'readwrite');
      const store=tx.objectStore('values');let result=null;
      tx.oncomplete=()=>{db.close();done(0,result);};
      tx.onabort=()=>{db.close();done(1,String(tx.error||'Storage transaction aborted'));};
      if(op===0){const read=store.get(key);read.onsuccess=()=>{result=read.result===undefined?null:read.result;};}
      else if(op===1){const read=store.get(key);read.onsuccess=()=>{
        try {if(read.result!==undefined)store.put(read.result,key+'.backup');store.put(value,key);}
        catch(error){fail(error);}
      };}else store.delete(key);
      }catch(error){fail(error);}
    };
  }catch(error){done(1,String(error));}
});
static void storage_lock(void){} static void storage_unlock(void){}
#elif defined(_WIN32)
static void storage_lock(void){} static void storage_unlock(void){}
#else
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
static pthread_mutex_t storage_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t storage_cond=PTHREAD_COND_INITIALIZER;
static pthread_t storage_thread;
static int storage_started,storage_stopping;
static void storage_lock(void){pthread_mutex_lock(&storage_mutex);}
static void storage_unlock(void){pthread_mutex_unlock(&storage_mutex);}
static void storage_file(struct storage_request *r) {
  char directory[PATH_MAX],path[PATH_MAX],tmp[PATH_MAX],backup[PATH_MAX];
  const char *home=getenv(r->temporary?"TMPDIR":"HOME");
  if(r->temporary&&!home)home="/tmp";
  if(!home){snprintf(r->error,sizeof(r->error),"Storage home unavailable");return;}
#if defined(__APPLE__) && TARGET_OS_IOS
  const char *suffix=r->temporary?"/iteration-data":"/Documents/iteration-data";
#else
  const char *suffix=r->temporary?"/iteration-data":"/.iteration-data";
#endif
  if(snprintf(directory,sizeof(directory),"%s%s",home,suffix)>=(int)sizeof(directory) ||
     snprintf(path,sizeof(path),"%s/%s",directory,r->key)>=(int)sizeof(path) ||
     snprintf(tmp,sizeof(tmp),"%s.tmp",path)>=(int)sizeof(tmp) ||
     snprintf(backup,sizeof(backup),"%s.backup",path)>=(int)sizeof(backup)) {
    snprintf(r->error,sizeof(r->error),"Storage path too long");return;
  }
  if(mkdir(directory,0700)&&errno!=EEXIST){snprintf(r->error,sizeof(r->error),"Storage directory: %s",strerror(errno));return;}
  if(r->op==2){if(unlink(path)&&errno!=ENOENT)snprintf(r->error,sizeof(r->error),"Storage delete: %s",strerror(errno));return;}
  FILE *file=fopen(r->op==1?tmp:path,r->op==1?"wb":"rb");
  if(!file){if(r->op==0&&errno==ENOENT)r->missing=1;else snprintf(r->error,sizeof(r->error),"Storage open: %s",strerror(errno));return;}
  if(r->op==0){
    long size;
    if(fseek(file,0,SEEK_END)||(size=ftell(file))<0||(unsigned long)size>STORAGE_LIMIT||fseek(file,0,SEEK_SET))
      snprintf(r->error,sizeof(r->error),"Invalid storage size");
    else if(!(r->output=malloc((size_t)size+1)))snprintf(r->error,sizeof(r->error),"Storage allocation failed");
    else if(fread(r->output,1,(size_t)size,file)!=(size_t)size)snprintf(r->error,sizeof(r->error),"Storage read failed");
    else {r->output[size]=0;r->output_length=(size_t)size;}
    fclose(file);return;
  }
  int ok=fwrite(r->input,1,r->length,file)==r->length;
  if(fflush(file)||fsync(fileno(file)))ok=0;
  if(fclose(file))ok=0;
  if(!ok){unlink(tmp);snprintf(r->error,sizeof(r->error),"Storage write failed");return;}
  // Keep the old primary recoverable if the final rename is interrupted.
  if(!r->temporary && rename(path,backup)&&errno!=ENOENT){unlink(tmp);snprintf(r->error,sizeof(r->error),"Storage backup failed");return;}
  if(rename(tmp,path)){unlink(tmp);snprintf(r->error,sizeof(r->error),"Storage commit failed");}
  else {int fd=open(directory,O_RDONLY);if(fd>=0){fsync(fd);close(fd);}}
}
static void *storage_worker(void *unused) {
  (void)unused;storage_lock();
  for(;;){
    struct storage_request *r=NULL;
    for(int i=0;i<STORAGE_SLOTS;i++)if(storage_requests[i].status==1 && (!r||storage_requests[i].token<r->token))r=&storage_requests[i];
    if(!r){if(storage_stopping)break;pthread_cond_wait(&storage_cond,&storage_mutex);continue;}
    r->status=3;storage_unlock();storage_file(r);storage_lock();r->status=2;
  }
  storage_unlock();return NULL;
}
#endif
static void storage_release(struct storage_request *r) {
  JS_FreeValue(r->ctx,r->resolve);JS_FreeValue(r->ctx,r->reject);
  free(r->input);free(r->output);memset(r,0,sizeof(*r));
}
static void storage_poll(void) {
  storage_lock();
  for(int i=0;i<STORAGE_SLOTS;i++){
    struct storage_request *r=&storage_requests[i];if(r->status!=2)continue;
    JSValue value;
    if(r->error[0]) { value=JS_NewError(r->ctx);JS_SetPropertyStr(r->ctx,value,"message",JS_NewString(r->ctx,r->error)); }
    else value=r->op!=0?JS_UNDEFINED:r->missing?JS_NULL:JS_NewStringLen(r->ctx,r->output,r->output_length);
    JSValue result=JS_Call(r->ctx,r->error[0]?r->reject:r->resolve,JS_UNDEFINED,1,&value);
    JS_FreeValue(r->ctx,result);JS_FreeValue(r->ctx,value);storage_release(r);
  }
  storage_unlock();
}
static void storage_shutdown(void) {
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
  if(storage_started){storage_lock();storage_stopping=1;pthread_cond_signal(&storage_cond);storage_unlock();pthread_join(storage_thread,NULL);storage_started=storage_stopping=0;}
#endif
  for(int i=0;i<STORAGE_SLOTS;i++)if(storage_requests[i].status)storage_release(&storage_requests[i]);
}
static JSValue storage_request(JSContext *ctx,int argc,JSValueConst *argv,int op) {
  size_t length=0,keylen=0;const char *key=argc&&JS_IsString(argv[0])?JS_ToCStringLen(ctx,&keylen,argv[0]):NULL;
  if(!key)return JS_ThrowTypeError(ctx,"Storage requires a key");
  int valid=keylen>0&&keylen<=(op==1?56u:64u)&&key[0]!='.'&&!strstr(key,"..");
  if(keylen>=4&&!strcmp(key+keylen-4,".tmp"))valid=0;
  if(op==1&&keylen>=7&&!strcmp(key+keylen-7,".backup"))valid=0;
  for(size_t i=0;i<keylen;i++)if(!((key[i]>='a'&&key[i]<='z')||(key[i]>='A'&&key[i]<='Z')||(key[i]>='0'&&key[i]<='9')||key[i]=='_'||key[i]=='-'||key[i]=='.'))valid=0;
  if(!valid){JS_FreeCString(ctx,key);return JS_ThrowTypeError(ctx,"Invalid storage key");}
  const char *input=op==1&&argc>1&&JS_IsString(argv[1])?JS_ToCStringLen(ctx,&length,argv[1]):NULL;
  if(op==1&&(!input||length>STORAGE_LIMIT)){JS_FreeCString(ctx,key);if(input)JS_FreeCString(ctx,input);return JS_ThrowTypeError(ctx,"Storage requires a string up to 8 MiB");}
  int temporary=argc>(op==1?2:1)?JS_ToBool(ctx,argv[op==1?2:1]):0;
  storage_lock();
  struct storage_request *r=NULL;for(int i=0;i<STORAGE_SLOTS;i++)if(!storage_requests[i].status){r=&storage_requests[i];break;}
  if(!r){storage_unlock();JS_FreeCString(ctx,key);if(input)JS_FreeCString(ctx,input);return JS_ThrowInternalError(ctx,"Storage queue full");}
  JSValue funcs[2];JSValue promise=JS_NewPromiseCapability(ctx,funcs);
  if(JS_IsException(promise)){storage_unlock();JS_FreeCString(ctx,key);if(input)JS_FreeCString(ctx,input);return promise;}
  r->ctx=ctx;r->resolve=funcs[0];r->reject=funcs[1];r->op=op;r->temporary=temporary;r->length=length;
  snprintf(r->key,sizeof(r->key),"%s",key);JS_FreeCString(ctx,key);
  if(input){r->input=malloc(length+1);if(r->input)memcpy(r->input,input,length+1);JS_FreeCString(ctx,input);}
  r->token=(int)(++storage_token&0x7fffffff);r->status=1;
  if(op==1&&!r->input){snprintf(r->error,sizeof(r->error),"Storage allocation failed");r->status=2;}
#if defined(__EMSCRIPTEN__)
  if(r->status==1)storage_web_start(r->token,op,r->key,r->input,(int)length,temporary);
#elif defined(_WIN32)
  snprintf(r->error,sizeof(r->error),"Storage is not implemented on Windows");r->status=2;
#else
  if(!storage_started){
    if(pthread_create(&storage_thread,NULL,storage_worker,NULL)){snprintf(r->error,sizeof(r->error),"Storage worker unavailable");r->status=2;}
    else storage_started=1;
  }
  pthread_cond_signal(&storage_cond);
#endif
  storage_unlock();return promise;
}
static JSValue js_storage_load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;return storage_request(ctx,argc,argv,0);}
static JSValue js_storage_save(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;return storage_request(ctx,argc,argv,1);}
static JSValue js_storage_delete(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;return storage_request(ctx,argc,argv,2);}
static JSValue js_storage_is_reload(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;(void)argc;(void)argv;
#if defined(__EMSCRIPTEN__)
  return JS_NewBool(ctx,EM_ASM_INT({return performance.getEntriesByType('navigation')[0]?.type==='reload'?1:0;}));
#else
  return JS_FALSE;
#endif
}
