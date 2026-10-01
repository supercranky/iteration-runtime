/* Optional screen-space binary mask, composited after world particles and before
 * a separate NanoVG UI frame. Applications own mask generation and appearance. */
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
#define SCREEN_MASK_MAX_BYTES (16u*1024u*1024u)
#define SCREEN_MASK_TILE 64
#define SCREEN_MASK_MAX_TILES 4608
static struct {
  GLuint program, texture, vao, buffer;
  GLint size_location, cell_location, opacity_location, frame_location;
  float opacity;
  int width, height, cell, vertices, frames, frame;
} screen_mask;
static void screen_mask_release(void) {
  if(screen_mask.texture)glDeleteTextures(1,&screen_mask.texture);
  if(screen_mask.buffer)glDeleteBuffers(1,&screen_mask.buffer);
  if(screen_mask.vao)glDeleteVertexArrays(1,&screen_mask.vao);
  if(screen_mask.program)glDeleteProgram(screen_mask.program);
  memset(&screen_mask,0,sizeof(screen_mask));
}
static int screen_mask_prepare(void) {
  if(screen_mask.program)return 1;
  GLuint vs=min_layer_shader(GL_VERTEX_SHADER,
    "#version 300 es\nlayout(location=0)in vec2 position;void main(){gl_Position=vec4(position,0,1);}");
  GLuint fs=min_layer_shader(GL_FRAGMENT_SHADER,
    "#version 300 es\nprecision highp float;uniform highp sampler2DArray maskTexture;uniform int maskFrame;"
    "uniform vec2 framebufferSize;uniform float cellSize;uniform float opacity;out vec4 color;"
    "void main(){ivec2 p=ivec2(floor(vec2(gl_FragCoord.x,framebufferSize.y-gl_FragCoord.y)/cellSize));"
    "if(texelFetch(maskTexture,ivec3(p,maskFrame),0).r<0.5)discard;color=vec4(0,0,0,opacity);}");
  if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0;}
  screen_mask.program=glCreateProgram();
  glAttachShader(screen_mask.program,vs);glAttachShader(screen_mask.program,fs);
  glLinkProgram(screen_mask.program);glDeleteShader(vs);glDeleteShader(fs);
  GLint ok=0;glGetProgramiv(screen_mask.program,GL_LINK_STATUS,&ok);
  if(!ok){screen_mask_release();return 0;}
  screen_mask.size_location=glGetUniformLocation(screen_mask.program,"framebufferSize");
  screen_mask.cell_location=glGetUniformLocation(screen_mask.program,"cellSize");
  screen_mask.opacity_location=glGetUniformLocation(screen_mask.program,"opacity");
  screen_mask.frame_location=glGetUniformLocation(screen_mask.program,"maskFrame");
  glGenVertexArrays(1,&screen_mask.vao);glGenBuffers(1,&screen_mask.buffer);
  if(!screen_mask.vao||!screen_mask.buffer){screen_mask_release();return 0;}
  return 1;
}
#endif
static int screen_ui_started;
static JSValue js_screen_overlay_prepare(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;(void)argc;(void)argv;
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
  if(screen_ui_vg)return JS_UNDEFINED;
  if(screen_fonts_loaded)return JS_ThrowInternalError(ctx,"prepareScreenOverlay must precede font loading");
  if(!screen_mask_prepare())return JS_ThrowInternalError(ctx,"screen overlay GPU initialization failed");
  screen_ui_vg=nvgCreateGLES2(NVG_ANTIALIAS|NVG_STENCIL_STROKES);
  if(!screen_ui_vg){screen_mask_release();return JS_ThrowInternalError(ctx,"screen overlay UI initialization failed");}
  sg_reset_state_cache();return JS_UNDEFINED;
#else
  return JS_ThrowInternalError(ctx,"screen overlay requires GLES3");
#endif
}
static JSValue js_framebuffer_size(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;(void)argc;(void)argv;
  JSValue result=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,result,"width",JS_NewInt32(ctx,sapp_width()));
  JS_SetPropertyStr(ctx,result,"height",JS_NewInt32(ctx,sapp_height()));
  return result;
}
static JSValue js_screen_overlay_clear(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)ctx;(void)self;(void)argc;(void)argv;
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
  if(screen_mask.texture)glDeleteTextures(1,&screen_mask.texture);
  screen_mask.texture=0;screen_mask.vertices=0;screen_mask.frames=0;screen_mask.frame=0;sg_reset_state_cache();
#endif
  return JS_UNDEFINED;
}
static JSValue js_screen_overlay_set(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
  int w,h,cell,frames=1;size_t size=0;double opacity=1;
  if(!screen_ui_vg)return JS_ThrowInternalError(ctx,"prepareScreenOverlay must be called first");
  if((argc<4||argc>6)||JS_ToInt32(ctx,&w,argv[0])||JS_ToInt32(ctx,&h,argv[1])||JS_ToInt32(ctx,&cell,argv[3]))
    return JS_ThrowTypeError(ctx,"setScreenOverlay(width,height,ArrayBuffer,cellSize,opacity=1,frames=1)");
  if(argc>=5&&!JS_IsUndefined(argv[4])&&JS_ToFloat64(ctx,&opacity,argv[4]))return JS_EXCEPTION;
  if(argc>=6&&!JS_IsUndefined(argv[5])&&JS_ToInt32(ctx,&frames,argv[5]))return JS_EXCEPTION;
  if(frames<1||frames>16)return JS_ThrowRangeError(ctx,"screen overlay needs 1..16 frames");
  if(!isfinite(opacity)||opacity<0||opacity>1)return JS_ThrowRangeError(ctx,"screen overlay opacity must be in [0,1]");
  if(w<1||h<1||w>8192||h>8192||cell<1||cell>16||(size_t)w*h*frames>SCREEN_MASK_MAX_BYTES)
    return JS_ThrowRangeError(ctx,"screen overlay dimensions exceed limits");
  uint8_t *pixels=JS_GetArrayBuffer(ctx,&size,argv[2]);
  if(!pixels)return JS_EXCEPTION;
  if(size!=(size_t)w*h*frames||w!=(sapp_width()+cell-1)/cell||h!=(sapp_height()+cell-1)/cell)
    return JS_ThrowRangeError(ctx,"screen overlay buffer must match framebuffer grid");
  GLint max_size;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_size);
  if(w>max_size||h>max_size)return JS_ThrowRangeError(ctx,"screen overlay exceeds GPU texture limit");
  int tiles_x=(w+SCREEN_MASK_TILE-1)/SCREEN_MASK_TILE,tiles_y=(h+SCREEN_MASK_TILE-1)/SCREEN_MASK_TILE;
  if(tiles_x*tiles_y>SCREEN_MASK_MAX_TILES)return JS_ThrowRangeError(ctx,"too many screen overlay tiles");
  float *vertices=malloc((size_t)tiles_x*tiles_y*12*sizeof(float));
  if(!vertices)return JS_ThrowOutOfMemory(ctx);
  int count=0;
  for(int ty=0;ty<h;ty+=SCREEN_MASK_TILE)for(int tx=0;tx<w;tx+=SCREEN_MASK_TILE){
    int right=tx+SCREEN_MASK_TILE<w?tx+SCREEN_MASK_TILE:w;
    int bottom=ty+SCREEN_MASK_TILE<h?ty+SCREEN_MASK_TILE:h,occupied=0;
    for(int f=0;f<frames&&!occupied;f++)
      for(int y=ty;y<bottom&&!occupied;y++)for(int x=tx;x<right;x++)
        if(pixels[(size_t)f*w*h+(size_t)y*w+x]>=128){occupied=1;break;}
    if(!occupied)continue;
    float l=2.f*tx*cell/sapp_width()-1.f,r=2.f*fminf(right*cell,sapp_width())/sapp_width()-1.f;
    float t=1.f-2.f*ty*cell/sapp_height(),b=1.f-2.f*fminf(bottom*cell,sapp_height())/sapp_height();
    const float quad[]={l,t,r,t,r,b,l,t,r,b,l,b};
    memcpy(vertices+count*2,quad,sizeof(quad));count+=6;
  }
  GLuint texture=0,buffer=0;glGenTextures(1,&texture);glGenBuffers(1,&buffer);
  if(!texture||!buffer){
    if(texture)glDeleteTextures(1,&texture);if(buffer)glDeleteBuffers(1,&buffer);
    free(vertices);return JS_ThrowOutOfMemory(ctx);
  }
  GLint unpack;glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,texture);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_R8,w,h,frames,0,GL_RED,GL_UNSIGNED_BYTE,pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glBindTexture(GL_TEXTURE_2D_ARRAY,0);
  glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,(size_t)count*2*sizeof(float),vertices,GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER,0);free(vertices);
  GLenum error=glGetError();
  if(error!=GL_NO_ERROR){
    glDeleteTextures(1,&texture);glDeleteBuffers(1,&buffer);sg_reset_state_cache();
    return JS_ThrowInternalError(ctx,"screen overlay upload failed (GL %u)",(unsigned)error);
  }
  if(screen_mask.texture)glDeleteTextures(1,&screen_mask.texture);
  if(screen_mask.buffer)glDeleteBuffers(1,&screen_mask.buffer);
  screen_mask.buffer=buffer;screen_mask.texture=texture;screen_mask.vertices=count;screen_mask.cell=cell;screen_mask.opacity=(float)opacity;
  screen_mask.frames=frames;screen_mask.frame=0;
  screen_mask.width=sapp_width();screen_mask.height=sapp_height();sg_reset_state_cache();
  return JS_UNDEFINED;
#else
  (void)argc;(void)argv;return JS_ThrowInternalError(ctx,"screen overlay requires GLES3");
#endif
}
static JSValue js_screen_overlay_frame(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
  double frame;
  if(argc<1||JS_ToFloat64(ctx,&frame,argv[0]))return JS_ThrowTypeError(ctx,"expected frame index");
  if(!isfinite(frame)||frame<0||frame>=screen_mask.frames||floor(frame)!=frame)
    return JS_ThrowRangeError(ctx,"screen overlay frame out of range");
  screen_mask.frame=(int)frame;return JS_UNDEFINED;
#else
  (void)argc;(void)argv;return JS_ThrowInternalError(ctx,"screen overlay requires GLES3");
#endif
}
static JSValue js_screen_ui_begin(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;(void)argc;(void)argv;
  if(!screen_ui_vg)return JS_ThrowInternalError(ctx,"prepareScreenOverlay must be called first");
  if(screen_ui_started)return JS_UNDEFINED;
  if(!min_layers_balanced()||state.vg!=screen_world_vg)return JS_ThrowInternalError(ctx,"cannot begin UI inside a lighting layer");
  screen_ui_started=1;state.vg=screen_ui_vg;
  nvgBeginFrame(screen_ui_vg,sapp_width(),sapp_height(),1.f);
  return JS_UNDEFINED;
}
static void screen_overlay_present(void) {
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
  if(!screen_mask.texture||!screen_mask.vertices||screen_mask.opacity<=0||screen_mask.width!=sapp_width()||screen_mask.height!=sapp_height())return;
  GLint scissor[4];glGetIntegerv(GL_SCISSOR_BOX,scissor);
  glViewport(0,0,sapp_width(),sapp_height());glDisable(GL_SCISSOR_TEST);
  if(screen_mask.opacity<1){glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
  else glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glUseProgram(screen_mask.program);glUniform2f(screen_mask.size_location,sapp_width(),sapp_height());
  glUniform1f(screen_mask.cell_location,screen_mask.cell);
  glUniform1f(screen_mask.opacity_location,screen_mask.opacity);
  glUniform1i(screen_mask.frame_location,screen_mask.frame);
  glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D_ARRAY,screen_mask.texture);
  glBindVertexArray(screen_mask.vao);glBindBuffer(GL_ARRAY_BUFFER,screen_mask.buffer);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);
  glDrawArrays(GL_TRIANGLES,0,screen_mask.vertices);
  glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);glBindTexture(GL_TEXTURE_2D_ARRAY,0);glUseProgram(0);
  glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);sg_reset_state_cache();
#endif
}
static void screen_overlay_shutdown(void) {
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
  screen_mask_release();
  if(screen_ui_vg)nvgDeleteGLES2(screen_ui_vg);
#endif
  screen_ui_vg=NULL;screen_ui_started=0;
}
