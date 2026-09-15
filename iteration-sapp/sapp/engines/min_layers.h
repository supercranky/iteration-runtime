/* Generic full-viewport minimum-alpha layers. Included by runtime.c after GL/NanoVG. */
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
static struct {
  NVGcontext *vg, *main;
  NVGLUframebuffer *layer, *combined;
  GLuint program, vao;
  GLint framebuffer, viewport[4];
  int image, width, height, active, group;
} min_layers;

static GLuint min_layer_shader(GLenum type, const char *source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
  GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) { glDeleteShader(shader); return 0; }
  return shader;
}
static void min_layers_targets_free(void)
{
  if (min_layers.image) nvgDeleteImage(min_layers.main, min_layers.image);
  if (min_layers.layer) nvgluDeleteFramebuffer(min_layers.layer);
  if (min_layers.combined) nvgluDeleteFramebuffer(min_layers.combined);
  min_layers.image = 0; min_layers.layer = min_layers.combined = NULL;
}
static void min_layers_shutdown(void)
{
  min_layers_targets_free();
  if (min_layers.vg) nvgDeleteGLES2(min_layers.vg);
  if (min_layers.program) glDeleteProgram(min_layers.program);
  if (min_layers.vao) glDeleteVertexArrays(1, &min_layers.vao);
  memset(&min_layers, 0, sizeof(min_layers));
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
  return min_layers.image != 0;
}
static void min_layers_restore(void)
{
  /* NanoVG sets blend factors but does not restore the blend equation. */
  glBlendEquation(GL_FUNC_ADD);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)min_layers.framebuffer);
  glViewport(min_layers.viewport[0],min_layers.viewport[1],min_layers.viewport[2],min_layers.viewport[3]);
  sg_reset_state_cache();
}
static int min_layers_begin(NVGcontext **vg, int reset)
{
  if (min_layers.active || (!reset && !min_layers.group)) return 0;
  if (reset) min_layers.group=1;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &min_layers.framebuffer);
  glGetIntegerv(GL_VIEWPORT, min_layers.viewport);
  if (!min_layers_prepare(*vg,sapp_width(),sapp_height())) { min_layers_restore(); return 0; }
  glViewport(0,0,min_layers.width,min_layers.height);
  glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  if (reset) {
    glBindFramebuffer(GL_FRAMEBUFFER,min_layers.combined->fbo);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
  }
  glBindFramebuffer(GL_FRAMEBUFFER,min_layers.layer->fbo);
  glStencilMask(0xff); glClearStencil(0); glClearColor(0,0,0,0);
  glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
  glBlendEquation(GL_FUNC_ADD);
  /* Match the existing runtime's NanoVG coordinate system exactly. */
  nvgBeginFrame(min_layers.vg,sapp_width(),sapp_height(),sapp_dpi_scale());
  *vg=min_layers.vg; min_layers.active=1; return 1;
}
static int min_layers_end(NVGcontext **vg, int present)
{
  if (!min_layers.active) return 0;
  glBindVertexArray(0);
  nvgEndFrame(min_layers.vg); *vg=min_layers.main; min_layers.active=0;
  glBindFramebuffer(GL_FRAMEBUFFER,min_layers.combined->fbo);
  glViewport(0,0,min_layers.width,min_layers.height);
  glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glUseProgram(min_layers.program); glBindVertexArray(min_layers.vao);
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,min_layers.layer->texture);
  glUniform1i(glGetUniformLocation(min_layers.program,"layer"),0);
  glEnable(GL_BLEND);glBlendEquation(GL_MIN);glBlendFunc(GL_ONE,GL_ONE);
  glDrawArrays(GL_TRIANGLES,0,3);
  min_layers_restore();
  if (present) {
    min_layers.group=0;
    nvgSave(*vg);nvgResetTransform(*vg);
    nvgBeginPath(*vg);nvgRect(*vg,0,0,sapp_width(),sapp_height());
    NVGpaint paint=nvgImagePattern(*vg,0,0,sapp_width(),sapp_height(),0,min_layers.image,1);
    // The bundled NanoVG FLIPY flag negates Y without translating by height.
    paint.xform[3]=-1; paint.xform[5]=(float)sapp_height();
    nvgFillPaint(*vg,paint);
    nvgFill(*vg);nvgRestore(*vg);
  }
  return 1;
}
static void min_layers_abort(NVGcontext **vg)
{
  if(min_layers.active){nvgCancelFrame(min_layers.vg);*vg=min_layers.main;min_layers_restore();}
  min_layers.active=0;min_layers.group=0;
}
static int min_layers_balanced(void){return !min_layers.active;}
#else
static int min_layers_begin(NVGcontext **vg,int reset){(void)vg;(void)reset;return 0;}
static int min_layers_end(NVGcontext **vg,int present){(void)vg;(void)present;return 0;}
static void min_layers_shutdown(void){}
static void min_layers_abort(NVGcontext **vg){(void)vg;}
static int min_layers_balanced(void){return 1;}
#endif
