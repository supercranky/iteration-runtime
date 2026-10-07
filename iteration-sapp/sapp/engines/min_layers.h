/* Generic viewport minimum-alpha layers with optional reduced resolution. Included by runtime.c after GL/NanoVG. */
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
static struct {
  NVGcontext *vg, *main;
  NVGLUframebuffer *layer, *combined, *fine;
  GLuint program, downsample_program, vao;
  GLint framebuffer, viewport[4], sampler, downsample_sampler, downsample_step, downsample_count;
  int fine_width, fine_height, downsample_failed;
  int bounded, crop[4];
  float bounds[4];
  GLint scissor[4];
  int image, width, height, active, group, first;
  uint64_t completed_frame;
  int completed, coverage_allowed;
} min_layers;
static float min_layers_resolution = 1.0f;

static int min_layers_set_resolution(float scale)
{
  if (min_layers.active || min_layers.group ||
      !(scale == 1.0f || scale == 0.5f || scale == 0.25f)) return 0;
  /* Resize lazily at the next group; never release a mask used this frame. */
  min_layers_resolution = scale;
  return 1;
}

/* Optional conservative influence rectangle, in full-frame pixels. The caller
 * guarantees later layers cannot reduce alpha outside it. First stays full. */
static int min_layers_set_bounds(const float *bounds)
{
  if (min_layers.active) return 0;
  min_layers.bounded = bounds != NULL;
  if (bounds) memcpy(min_layers.bounds,bounds,sizeof(min_layers.bounds));
  return 1;
}
static void min_layers_composition_scissor(void)
{
  glEnable(GL_SCISSOR_TEST);
  glScissor(min_layers.crop[0],min_layers.height-min_layers.crop[3],
    min_layers.crop[2]-min_layers.crop[0],min_layers.crop[3]-min_layers.crop[1]);
}
static GLuint min_layer_shader(GLenum type, const char *source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
  GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) { glDeleteShader(shader); return 0; }
  return shader;
}
/* Ordinary single-sample supersampling: no MSAA formats or framebuffer blits.
 * A failed optional program falls back to the original reduced raster path. */
static int min_layers_downsample_prepare(void)
{
  if (min_layers.downsample_program) return 1;
  if (min_layers.downsample_failed) return 0;
  min_layers.downsample_failed = 1;
  GLuint vs = min_layer_shader(GL_VERTEX_SHADER,
    "#version 300 es\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}");
  GLuint fs = min_layer_shader(GL_FRAGMENT_SHADER,
    "#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D layer;"
    "uniform vec2 sampleStep;uniform int sampleCount;out vec4 color;"
    "void main(){vec4 sum=vec4(0);float n=float(sampleCount);"
    "for(int y=0;y<4;y++)for(int x=0;x<4;x++)if(x<sampleCount&&y<sampleCount)"
    "sum+=texture(layer,uv+(vec2(float(x),float(y))+.5-n*.5)*sampleStep);"
    "color=sum/(n*n);}");
  if (!vs || !fs) { if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); return 0; }
  GLuint program = glCreateProgram();
  glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
  glDeleteShader(vs);glDeleteShader(fs);
  GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
  if (!ok) { glDeleteProgram(program); return 0; }
  min_layers.downsample_program=program;
  min_layers.downsample_sampler=glGetUniformLocation(program,"layer");
  min_layers.downsample_step=glGetUniformLocation(program,"sampleStep");
  min_layers.downsample_count=glGetUniformLocation(program,"sampleCount");
  return 1;
}
static float min_layers_raster_scale(void) { return min_layers.fine ? 1.0f : min_layers_resolution; }
static void min_layers_targets_free(void)
{
  if (min_layers.image) nvgDeleteImage(min_layers.main, min_layers.image);
  if (min_layers.layer) nvgluDeleteFramebuffer(min_layers.layer);
  if (min_layers.combined) nvgluDeleteFramebuffer(min_layers.combined);
  if (min_layers.fine) nvgluDeleteFramebuffer(min_layers.fine);
  min_layers.image = 0; min_layers.layer = min_layers.combined = min_layers.fine = NULL;
  min_layers.fine_width = min_layers.fine_height = 0;
}
static void min_layers_shutdown(void)
{
  min_layers_targets_free();
  if (min_layers.vg) nvgDeleteGLES2(min_layers.vg);
  if (min_layers.program) glDeleteProgram(min_layers.program);
  if (min_layers.downsample_program) glDeleteProgram(min_layers.downsample_program);
  if (min_layers.vao) glDeleteVertexArrays(1, &min_layers.vao);
  memset(&min_layers, 0, sizeof(min_layers));
  min_layers_resolution = 1.0f;
}
static int min_layers_prepare(NVGcontext *main, int width, int height)
{
  min_layers.main = main;
  if (!min_layers.vg) min_layers.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  if (!min_layers.vg) return 0;
  if (!min_layers.program) {
    GLuint vs = min_layer_shader(GL_VERTEX_SHADER,
      "#version 300 es\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}");
    GLuint fs = min_layer_shader(GL_FRAGMENT_SHADER,
      "#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D layer;out vec4 color;void main(){color=texture(layer,uv);}");
    if (!vs || !fs) { if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); return 0; }
    min_layers.program = glCreateProgram();
    glAttachShader(min_layers.program, vs); glAttachShader(min_layers.program, fs);
    glLinkProgram(min_layers.program); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(min_layers.program, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(min_layers.program); min_layers.program=0; return 0; }
    min_layers.sampler = glGetUniformLocation(min_layers.program,"layer");
    glGenVertexArrays(1, &min_layers.vao);
  }
  if (!min_layers.layer || width != min_layers.width || height != min_layers.height) {
    min_layers_targets_free();
    min_layers.layer = nvgluCreateFramebuffer(min_layers.vg, width, height, 0);
    min_layers.combined = nvgluCreateFramebuffer(min_layers.vg, width, height, 0);
    if (!min_layers.layer || !min_layers.combined) return 0;
    min_layers.image = nvglCreateImageFromHandleGLES2(main, min_layers.combined->texture,
      width, height, NVG_IMAGE_NODELETE | NVG_IMAGE_PREMULTIPLIED);
    min_layers.width=width; min_layers.height=height;
  }
  const int fine_width=sapp_width(),fine_height=sapp_height();
  if (min_layers.fine && (min_layers_resolution == 1.0f ||
      min_layers.fine_width != fine_width || min_layers.fine_height != fine_height)) {
    nvgluDeleteFramebuffer(min_layers.fine);min_layers.fine=NULL;
  }
  if (min_layers_resolution < 1.0f && !min_layers.fine && min_layers_downsample_prepare()) {
    min_layers.fine=nvgluCreateFramebuffer(min_layers.vg,fine_width,fine_height,0);
    min_layers.fine_width=fine_width;min_layers.fine_height=fine_height;
  }
  return min_layers.image != 0;
}
static void min_layers_restore(void)
{
  /* NanoVG sets blend factors but does not restore the blend equation. */
  glBlendEquation(GL_FUNC_ADD);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)min_layers.framebuffer);
  glViewport(min_layers.viewport[0],min_layers.viewport[1],min_layers.viewport[2],min_layers.viewport[3]);
  glScissor(min_layers.scissor[0],min_layers.scissor[1],min_layers.scissor[2],min_layers.scissor[3]);
  sg_reset_state_cache();
}
static int min_layers_begin(NVGcontext **vg, int reset)
{
  if (min_layers.active || (!reset && !min_layers.group)) return 0;
  if (reset) { min_layers.group=1; min_layers.completed=0; }
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &min_layers.framebuffer);
  glGetIntegerv(GL_VIEWPORT, min_layers.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, min_layers.scissor);
  const int width = (int)fmaxf(1, ceilf(sapp_width() * min_layers_resolution));
  const int height = (int)fmaxf(1, ceilf(sapp_height() * min_layers_resolution));
  if (!min_layers_prepare(*vg,width,height)) { min_layers_restore(); return 0; }
  min_layers.crop[0]=min_layers.crop[1]=0;
  min_layers.crop[2]=width;min_layers.crop[3]=height;
  if (!reset && min_layers.bounded) {
    const float sx=(float)width/sapp_width(),sy=(float)height/sapp_height();
    /* Extra destination pixels cover AA and the downsampling footprint. */
    min_layers.crop[0]=(int)fmaxf(0,fminf(width,floorf(min_layers.bounds[0]*sx)-2));
    min_layers.crop[1]=(int)fmaxf(0,fminf(height,floorf(min_layers.bounds[1]*sy)-2));
    min_layers.crop[2]=(int)fmaxf(0,fminf(width,ceilf(min_layers.bounds[2]*sx)+2));
    min_layers.crop[3]=(int)fmaxf(0,fminf(height,ceilf(min_layers.bounds[3]*sy)+2));
  }
  gpu_profile_begin(0);
  glViewport(0,0,min_layers.fine ? min_layers.fine_width : min_layers.width,
    min_layers.fine ? min_layers.fine_height : min_layers.height);
  glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  /* min(1, firstAlpha) == firstAlpha. Render the first layer directly into
     the accumulator: no scratch copy, destination read, or second clear. */
  min_layers.first = reset;
  glBindFramebuffer(GL_FRAMEBUFFER,min_layers.fine ? min_layers.fine->fbo :
    reset ? min_layers.combined->fbo : min_layers.layer->fbo);
  /* Previous scratch contents are dead. Tell tile GPUs before clearing, so
     attachment loads can be omitted. The first accumulator is also replaced. */
  const GLenum discarded[2]={GL_COLOR_ATTACHMENT0,GL_STENCIL_ATTACHMENT};
  glInvalidateFramebuffer(GL_FRAMEBUFFER,2,discarded);
  glStencilMask(0xff); glClearStencil(0); glClearColor(0,0,0,0);
  glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
  glBlendEquation(GL_FUNC_ADD);
  /* Match the existing runtime's NanoVG coordinate system exactly. */
  /* Preserve full-frame coordinates; only the raster target/AA density shrinks.
     Coverage triangles use the same NDC and independently scaled scissor. */
  nvgBeginFrame(min_layers.vg,sapp_width(),sapp_height(),min_layers_raster_scale());
  *vg=min_layers.vg; min_layers.active=1; min_layers.coverage_allowed=1; return 1;
}
static int min_layers_end(NVGcontext **vg, int present)
{
  if (!min_layers.active) return 0;
  glBindVertexArray(0);
  nvgEndFrame(min_layers.vg); *vg=min_layers.main; min_layers.active=0;
  /* No later pass reads the stencil attachment. Preserve color for sampling. */
  const GLenum discarded=GL_STENCIL_ATTACHMENT;
  glInvalidateFramebuffer(GL_FRAMEBUFFER,1,&discarded);
  if (min_layers.fine) {
    /* Box-filter all 2x2/4x4 source samples, rather than just bilinear lookup
       at one coarse pixel center. Dust consumes this same filtered mask. */
    // Downsample straight into the accumulator: one draw and target switch,
    // rather than writing a reduced scratch image and sampling it again.
    glBindFramebuffer(GL_FRAMEBUFFER,min_layers.combined->fbo);
    glViewport(0,0,min_layers.width,min_layers.height);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    if (min_layers.first) { glDisable(GL_BLEND);glBlendEquation(GL_FUNC_ADD); }
    else { glEnable(GL_BLEND);glBlendEquation(GL_MIN);glBlendFunc(GL_ONE,GL_ONE); }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    min_layers_composition_scissor();
    glUseProgram(min_layers.downsample_program);glBindVertexArray(min_layers.vao);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,min_layers.fine->texture);
    const int samples=min_layers_resolution == 0.25f ? 4 : 2;
    glUniform1i(min_layers.downsample_sampler,0);
    glUniform1i(min_layers.downsample_count,samples);
    glUniform2f(min_layers.downsample_step,1.0f/(min_layers.width*samples),1.0f/(min_layers.height*samples));
    glDrawArrays(GL_TRIANGLES,0,3);
  }
  if (!min_layers.first && !min_layers.fine) {
    glBindFramebuffer(GL_FRAMEBUFFER,min_layers.combined->fbo);
    glViewport(0,0,min_layers.width,min_layers.height);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    min_layers_composition_scissor();
    glUseProgram(min_layers.program); glBindVertexArray(min_layers.vao);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,min_layers.layer->texture);
    glUniform1i(min_layers.sampler,0);
    glEnable(GL_BLEND);glBlendEquation(GL_MIN);glBlendFunc(GL_ONE,GL_ONE);
    glDrawArrays(GL_TRIANGLES,0,3);
  }
  min_layers_restore();
  gpu_profile_end();
  if (present) {
    min_layers.group=0;
    min_layers.completed=1;
    min_layers.completed_frame=sapp_frame_count();
    nvgSave(*vg);nvgResetTransform(*vg);
    nvgBeginPath(*vg);nvgRect(*vg,0,0,sapp_width(),sapp_height());
    NVGpaint paint=nvgImagePattern(*vg,0,0,sapp_width(),sapp_height(),0,min_layers.image,1);
    /* The old accumulator started at RGB=0, so GL_MIN always produced black,
       even for colored layer commands. Preserve that at presentation; only
       alpha is meaningful to consumers (including masked particles). */
    paint.innerColor.r=paint.innerColor.g=paint.innerColor.b=0;
    paint.outerColor.r=paint.outerColor.g=paint.outerColor.b=0;
    // The bundled NanoVG FLIPY flag negates Y without translating by height.
    paint.xform[3]=-1; paint.xform[5]=(float)sapp_height();
    nvgFillPaint(*vg,paint);
    nvgFill(*vg);nvgRestore(*vg);
  }
  return 1;
}
static void min_layers_abort(NVGcontext **vg)
{
  if(min_layers.active){nvgCancelFrame(min_layers.vg);*vg=min_layers.main;min_layers_restore();gpu_profile_end();}
  min_layers.active=0;min_layers.group=0;
}
static int min_layers_balanced(void){return !min_layers.active;}
static int min_layers_coverage_allowed(void){return min_layers.active && min_layers.coverage_allowed;}
static void min_layers_close_coverage(void){min_layers.coverage_allowed=0;}
static uint32_t min_layers_texture(void){return min_layers.completed && min_layers.completed_frame==sapp_frame_count() && min_layers.combined ? (uint32_t)min_layers.combined->texture : 0;}
#else
static int min_layers_set_bounds(const float *bounds){(void)bounds;return 0;}
static int min_layers_set_resolution(float scale){(void)scale;return 0;}
static int min_layers_begin(NVGcontext **vg,int reset){(void)vg;(void)reset;return 0;}
static int min_layers_end(NVGcontext **vg,int present){(void)vg;(void)present;return 0;}
static void min_layers_shutdown(void){}
static void min_layers_abort(NVGcontext **vg){(void)vg;}
static int min_layers_balanced(void){return 1;}
static int min_layers_coverage_allowed(void){return 0;}
static void min_layers_close_coverage(void){}
static uint32_t min_layers_texture(void){return 0;}
#endif
