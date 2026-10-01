/* Generic analytic coverage triangles. Map/occluder construction belongs to
 * application plugins. Fixed scratch storage, one bounded upload per command. */
#include "../plugins/coverage_validation.h"
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
static struct { GLuint program, vao, buffer; } coverage_gpu;
static iteration_coverage_vertex coverage_scratch[ITER_COVERAGE_MAX_VERTICES];
static void coverage_shutdown(void) {
  if(coverage_gpu.program)glDeleteProgram(coverage_gpu.program);
  if(coverage_gpu.vao)glDeleteVertexArrays(1,&coverage_gpu.vao);
  if(coverage_gpu.buffer)glDeleteBuffers(1,&coverage_gpu.buffer);
  memset(&coverage_gpu,0,sizeof(coverage_gpu));
}
static int coverage_prepare(void) {
  if(coverage_gpu.program)return 1;
  GLuint vs=min_layer_shader(GL_VERTEX_SHADER,
    "#version 300 es\nlayout(location=0)in vec2 position;layout(location=1)in vec3 a;"
    "layout(location=2)in vec3 b;layout(location=3)in float opacity;"
    "out vec3 va;out vec3 vb;out float alpha;void main(){"
    "gl_Position=vec4(position,0,1);va=a;vb=b;alpha=opacity;}");
  GLuint fs=min_layer_shader(GL_FRAGMENT_SHADER,
    "#version 300 es\nprecision highp float;in vec3 va;in vec3 vb;in float alpha;out vec4 color;"
    "float cdf(vec3 v){float s=v.x*inversesqrt(max(dot(v.yz,v.yz),1e-10));"
    "if(s<=-1.)return 0.;if(s>=1.)return 1.;"
    "return .5+(asin(s)+s*sqrt(max(0.,1.-s*s)))/3.141592653589793;}"
    "void main(){float c=clamp(cdf(va)+cdf(vb)-1.,0.,1.);color=vec4(0,0,0,c*alpha);}");
  if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0;}
  coverage_gpu.program=glCreateProgram();
  glAttachShader(coverage_gpu.program,vs);glAttachShader(coverage_gpu.program,fs);
  glLinkProgram(coverage_gpu.program);glDeleteShader(vs);glDeleteShader(fs);
  GLint ok=0;glGetProgramiv(coverage_gpu.program,GL_LINK_STATUS,&ok);
  if(!ok){coverage_shutdown();return 0;}
  glGenVertexArrays(1,&coverage_gpu.vao);glGenBuffers(1,&coverage_gpu.buffer);
  if(!coverage_gpu.vao||!coverage_gpu.buffer){coverage_shutdown();return 0;}
  return 1;
}
static int coverage_draw(const uint8_t *bytes, size_t size, float w, float h) {
  iteration_coverage_command c;
  if(!min_layers_coverage_allowed()||!iteration_validate_coverage(bytes,size,&c)||w<=0||h<=0)return 0;
  float x0=convert_local_x_to_screen_dimensions(c.left,w,h),x1=convert_local_x_to_screen_dimensions(c.right,w,h);
  float y0=convert_local_y_to_screen_dimensions(c.top,w,h),y1=convert_local_y_to_screen_dimensions(c.bottom,w,h);
  if(!isfinite(x0)||!isfinite(x1)||!isfinite(y0)||!isfinite(y1))return 0;
  for(uint32_t i=0;i<c.count;i++){
    float v[9];memcpy(v,bytes+sizeof(c)+i*sizeof(iteration_coverage_vertex),sizeof(v));
    v[0]=2*convert_local_x_to_screen_dimensions(v[0],w,h)/w-1;
    v[1]=1-2*convert_local_y_to_screen_dimensions(v[1],w,h)/h;
    if(!isfinite(v[0])||!isfinite(v[1]))return 0;
    memcpy(&coverage_scratch[i],v,sizeof(v));
  }
  int left=(int)floorf(fmaxf(0,fminf(w,fminf(x0,x1))));
  int right=(int)ceilf(fmaxf(0,fminf(w,fmaxf(x0,x1))));
  int top=(int)floorf(fmaxf(0,fminf(h,fminf(y0,y1))));
  int bottom=(int)ceilf(fmaxf(0,fminf(h,fmaxf(y0,y1))));
  if(left>=right||top>=bottom)return 1;
  if(!coverage_prepare())return 0;
  GLint previous_scissor[4];glGetIntegerv(GL_SCISSOR_BOX,previous_scissor);
  glBindVertexArray(coverage_gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,coverage_gpu.buffer);
  glBufferData(GL_ARRAY_BUFFER,c.count*sizeof(iteration_coverage_vertex),coverage_scratch,GL_STREAM_DRAW);
  const GLint sizes[4]={2,3,3,1};const size_t offsets[4]={0,2*sizeof(float),5*sizeof(float),8*sizeof(float)};
  for(int i=0;i<4;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(iteration_coverage_vertex),(const void*)offsets[i]);}
  glUseProgram(coverage_gpu.program);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_SCISSOR_TEST);glScissor(left,(int)h-bottom,right-left,bottom-top);
  if(c.blend==ITER_COVERAGE_MAX){glEnable(GL_BLEND);glBlendEquation(GL_MAX);glBlendFunc(GL_ONE,GL_ONE);}
  else glDisable(GL_BLEND);
  glDrawArrays(GL_TRIANGLES,0,(GLsizei)c.count);
  glBlendEquation(GL_FUNC_ADD);glDisable(GL_SCISSOR_TEST);
  /* Sokol's reset re-enables scissoring without resetting its box. Restore the
   * incoming box or deferred terrain draws inherit the last light's bounds. */
  glScissor(previous_scissor[0],previous_scissor[1],previous_scissor[2],previous_scissor[3]);
  glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);sg_reset_state_cache();
  return 1;
}
#else
static void coverage_shutdown(void){}
static int coverage_draw(const uint8_t *bytes,size_t size,float w,float h){(void)bytes;(void)size;(void)w;(void)h;return 0;}
#endif
