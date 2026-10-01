/* Opt-in sampled GPU diagnostics. Timings never overlap, block, or split batches. */
#define GPU_PROFILE_PASSES 6
static struct {
  int enabled, sampling, active, sample_frame, width, height, samples, sg_was_enabled;
  uint32_t tick, raw, raw_start, sg_start, frame_sg_start, draws, pass_draws[GPU_PROFILE_PASSES], last_pass_draws[GPU_PROFILE_PASSES];
  uint32_t work[3];
} gpu_profile={.active=-1};
static const char *gpu_pass_names[GPU_PROFILE_PASSES]={"Light layers","Sprites/models","World graphics","Particles","Vignette","UI"};
#if defined(__EMSCRIPTEN__)
EM_JS(int, gpu_timer_enable, (int enabled), {
  if (!enabled) { if(Module.iterationGpuProfile)Module.iterationGpuProfile.dispose(); return 0; }
  if(Module.iterationGpuProfile)Module.iterationGpuProfile.dispose();
  var gl=Module.canvas.getContext('webgl2');
  var p=Module.iterationGpuProfile={gl:gl,ext:gl.getExtension('EXT_disjoint_timer_query_webgl2'),
    pool:[],pending:[],current:null,active:null,latest:null,serial:0,status:1,completed:0,skipped:0};
  p.recycle=function(batch){for(var item of batch.items)this.pool.push(item.query);};
  p.dispose=function(){
    if(this.active&&!gl.isContextLost())gl.endQuery(this.ext.TIME_ELAPSED_EXT);
    var queries=this.pool.slice();
    for(var batch of this.pending)for(var item of batch.items)queries.push(item.query);
    if(this.current)for(var item of this.current.items)queries.push(item.query);
    for(var q of queries)gl.deleteQuery(q);
    this.pool=[];this.pending=[];this.current=null;this.active=null;this.latest=null;
  };
  p.poll=function(){
    if(gl.isContextLost()){this.dispose();this.status=4;return;}
    if(this.status===4){this.ext=gl.getExtension('EXT_disjoint_timer_query_webgl2');this.status=1;}
    if(!this.ext){this.status=0;return;}
    this.disjoint=gl.getParameter(this.ext.GPU_DISJOINT_EXT);
    if(this.disjoint){
      for(var batch of this.pending)this.recycle(batch);
      this.pending=[];this.latest=null;this.status=3;return;
    }
    while(this.pending.length){
      var batch=this.pending[0];
      if(!batch.items.every(function(item){return gl.getQueryParameter(item.query,gl.QUERY_RESULT_AVAILABLE);}))break;
      var times=[0,0,0,0,0,0];
      for(var item of batch.items)times[item.pass]+=gl.getQueryParameter(item.query,gl.QUERY_RESULT)/1000000;
      this.latest={times:times,serial:batch.serial};this.completed++;this.status=2;
      this.recycle(batch);this.pending.shift();
    }
  };
  p.status=p.ext?1:0;
  return p.ext?1:0;
});
EM_JS(void, gpu_timer_frame_begin, (int sample), {
  var p=Module.iterationGpuProfile;if(!p)return;
  p.serial++;
  if(p.serial%5===0||sample)p.poll();
  p.current=null;
  if(sample&&p.ext&&!p.disjoint&&p.status!==4&&!p.gl.isContextLost()){
    if(p.pending.length>=4){p.skipped++;return;}
    p.current={serial:p.serial,items:[],bad:false};
  }
});
EM_JS(void, gpu_timer_begin, (int pass), {
  var p=Module.iterationGpuProfile;if(!p||!p.current||p.current.bad)return;
  if(p.active||p.current.items.length>=24||p.gl.getQuery(p.ext.TIME_ELAPSED_EXT,p.gl.CURRENT_QUERY)){
    p.current.bad=true;p.skipped++;return;
  }
  var query=p.pool.pop()||p.gl.createQuery();
  if(!query){p.current.bad=true;p.skipped++;return;}
  p.current.items.push({pass:pass,query:query});p.active=query;
  p.gl.beginQuery(p.ext.TIME_ELAPSED_EXT,query);
});
EM_JS(void, gpu_timer_end, (), {
  var p=Module.iterationGpuProfile;if(p&&p.active){p.gl.endQuery(p.ext.TIME_ELAPSED_EXT);p.active=null;}
});
EM_JS(void, gpu_timer_frame_end, (), {
  var p=Module.iterationGpuProfile;if(!p||!p.current)return;
  if(p.active){p.gl.endQuery(p.ext.TIME_ELAPSED_EXT);p.active=null;p.current.bad=true;}
  if(p.current.bad||!p.current.items.length)p.recycle(p.current);else p.pending.push(p.current);
  p.current=null;
});
EM_JS(void, gpu_timer_snapshot, (double *out), {
  var p=Module.iterationGpuProfile,base=out>>3;
  HEAPF64[base]=p?p.status:0;HEAPF64[base+1]=p?p.completed:0;
  HEAPF64[base+2]=p&&p.latest?p.serial-p.latest.serial:-1;
  HEAPF64[base+3]=p?p.skipped:0;
  for(var i=0;i<6;i++)HEAPF64[base+4+i]=p&&p.latest?p.latest.times[i]:-1;
});
#else
static int gpu_timer_enable(int enabled){(void)enabled;return 0;}
static void gpu_timer_frame_begin(int sample){(void)sample;}
static void gpu_timer_begin(int pass){(void)pass;}
static void gpu_timer_end(void){}
static void gpu_timer_frame_end(void){}
static void gpu_timer_snapshot(double *out){for(int i=0;i<10;i++)out[i]=i>=4?-1:0;out[2]=-1;}
#endif
static uint32_t gpu_sg_draws(void){sg_stats stats=sg_query_stats();return stats.cur_frame.num_draw+stats.cur_frame.num_draw_ex;}
static void gpu_profile_begin(int pass){
  if(!gpu_profile.sampling)return;
  gpu_profile.active=pass;gpu_profile.raw_start=gpu_profile.raw;gpu_profile.sg_start=gpu_sg_draws();gpu_timer_begin(pass);
}
static void gpu_profile_end(void){
  if(!gpu_profile.sampling||gpu_profile.active<0)return;
  gpu_timer_end();gpu_profile.pass_draws[gpu_profile.active]+=gpu_profile.raw-gpu_profile.raw_start+gpu_sg_draws()-gpu_profile.sg_start;
  gpu_profile.active=-1;
}
static void gpu_profile_frame_begin(void){
  if(!gpu_profile.enabled)return;
  gpu_profile.sampling=(gpu_profile.tick++%20)==0;
  gpu_timer_frame_begin(gpu_profile.sampling);
  if(!gpu_profile.sampling)return;
  gpu_profile.raw=0;gpu_profile.frame_sg_start=gpu_sg_draws();gpu_profile.active=-1;
  memset(gpu_profile.work,0,sizeof(gpu_profile.work));memset(gpu_profile.pass_draws,0,sizeof(gpu_profile.pass_draws));
  if(gpu_profile.width!=sapp_width()||gpu_profile.height!=sapp_height()){
    gpu_profile.width=sapp_width();gpu_profile.height=sapp_height();
#if !defined(SOKOL_METAL)
    glGetIntegerv(GL_SAMPLES,&gpu_profile.samples);
#else
    gpu_profile.samples=sapp_sample_count();
#endif
  }
}
static void gpu_profile_frame_end(void){
  if(!gpu_profile.enabled)return;
  gpu_profile_end();gpu_timer_frame_end();
  if(gpu_profile.sampling){gpu_profile.draws=gpu_profile.raw+gpu_sg_draws()-gpu_profile.frame_sg_start;gpu_profile.sample_frame=(int)sapp_frame_count();memcpy(gpu_profile.last_pass_draws,gpu_profile.pass_draws,sizeof(gpu_profile.pass_draws));}
}
static JSValue js_gpu_profile_enable(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;int enabled=argc>0?JS_ToBool(ctx,argv[0]):0;if(enabled<0)return JS_EXCEPTION;
  if(enabled==gpu_profile.enabled)return JS_UNDEFINED;
  gpu_timer_enable(enabled);
  if(enabled){
    gpu_profile.sg_was_enabled=sg_stats_enabled();sg_enable_stats();gpu_profile.tick=0;gpu_profile.width=0;
    gpu_profile.draws=0;gpu_profile.sample_frame=(int)sapp_frame_count();
    memset(gpu_profile.last_pass_draws,0,sizeof(gpu_profile.last_pass_draws));
  }
  else if(!gpu_profile.sg_was_enabled)sg_disable_stats();
  gpu_profile.enabled=enabled;gpu_profile.sampling=0;gpu_profile.active=-1;return JS_UNDEFINED;
}
static JSValue js_gpu_profile_get(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;(void)argc;(void)argv;double info[10];gpu_timer_snapshot(info);
  JSValue result=JS_NewObject(ctx),passes=JS_NewArray(ctx);
  const char *status=!gpu_profile.enabled?"Off":info[0]==0?"Unavailable":info[0]==1?"Warming up":info[0]==2?"Ready":info[0]==3?"Disjoint (discarded)":"Context lost";
  JS_SetPropertyStr(ctx,result,"status",JS_NewString(ctx,status));
  JS_SetPropertyStr(ctx,result,"width",JS_NewInt32(ctx,gpu_profile.width));JS_SetPropertyStr(ctx,result,"height",JS_NewInt32(ctx,gpu_profile.height));
  JS_SetPropertyStr(ctx,result,"samples",JS_NewInt32(ctx,gpu_profile.samples));JS_SetPropertyStr(ctx,result,"draws",JS_NewUint32(ctx,gpu_profile.draws));
  JS_SetPropertyStr(ctx,result,"completed",JS_NewFloat64(ctx,info[1]));JS_SetPropertyStr(ctx,result,"ageFrames",JS_NewFloat64(ctx,info[2]));
  JS_SetPropertyStr(ctx,result,"skipped",JS_NewFloat64(ctx,info[3]));
  JS_SetPropertyStr(ctx,result,"drawFrame",JS_NewInt32(ctx,gpu_profile.sample_frame));
  JS_SetPropertyStr(ctx,result,"drawAgeFrames",JS_NewInt32(ctx,(int)sapp_frame_count()-gpu_profile.sample_frame));
  for(int i=0;i<GPU_PROFILE_PASSES;i++){
    JSValue pass=JS_NewObject(ctx);JS_SetPropertyStr(ctx,pass,"name",JS_NewString(ctx,gpu_pass_names[i]));
    JS_SetPropertyStr(ctx,pass,"ms",info[4+i]<0?JS_NULL:JS_NewFloat64(ctx,info[4+i]));
    JS_SetPropertyStr(ctx,pass,"draws",JS_NewUint32(ctx,gpu_profile.last_pass_draws[i]));JS_SetPropertyUint32(ctx,passes,i,pass);
  }
  JS_SetPropertyStr(ctx,result,"passes",passes);return result;
}
static JSValue js_gpu_profile_sampling(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;(void)argc;(void)argv;return JS_NewBool(ctx,gpu_profile.sampling);}
static JSValue js_render_work(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;(void)argc;(void)argv;JSValue result=JS_NewArray(ctx);
  for(int i=0;i<3;i++)JS_SetPropertyUint32(ctx,result,i,JS_NewUint32(ctx,gpu_profile.work[i]));return result;
}
static void gpu_profile_shutdown(void){gpu_timer_enable(0);if(gpu_profile.enabled&&!gpu_profile.sg_was_enabled)sg_disable_stats();memset(&gpu_profile,0,sizeof(gpu_profile));gpu_profile.active=-1;}
#if !defined(SOKOL_METAL)
/* Count this translation unit's NanoVG/plugin/raw GL draws. Sokol's public
 * counters cover models and sokol_gl in other translation units. */
static void gpu_counted_draw_arrays(GLenum mode,GLint first,GLsizei count){if(gpu_profile.sampling)gpu_profile.raw++;glDrawArrays(mode,first,count);}
static void gpu_counted_draw_elements(GLenum mode,GLsizei count,GLenum type,const void *indices){if(gpu_profile.sampling)gpu_profile.raw++;glDrawElements(mode,count,type,indices);}
#define glDrawArrays gpu_counted_draw_arrays
#define glDrawElements gpu_counted_draw_elements
#endif
