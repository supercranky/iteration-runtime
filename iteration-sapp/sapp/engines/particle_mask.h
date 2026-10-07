/* Direct-light masked particle quads. No intermediate images or fullscreen passes.
 * Queue during particle traversal; render after the main NanoVG lighting/UI flush,
 * at the same ordering point as the old accumulated particle image. */
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
#include <stddef.h>
#define PARTICLE_MASK_MAX_VERTICES (32768u * 6u)
#define PARTICLE_MASK_MAX_BATCHES 4096u
typedef struct { float x,y,u,v,opacity,threshold,softness; } particle_mask_vertex;
typedef struct { GLuint texture; uint32_t first,count; float tint[4]; int nearest; } particle_mask_batch;
static struct {
  GLuint program,vao,vbo,sampler,nearest_sampler,texture;
  GLint viewport_uniform,source_uniform,light_uniform,tint_uniform;
  float tint[4]; int nearest;
  particle_mask_vertex *vertices;
  particle_mask_batch batches[PARTICLE_MASK_MAX_BATCHES];
  uint32_t count,capacity,batch_count;
  float threshold,softness;
  int active,failed;
} particle_mask;

static GLuint particle_mask_shader(GLenum type,const char *source) {
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[512];glGetShaderInfoLog(shader,sizeof(log),NULL,log);SOKOL_LOG(log);glDeleteShader(shader);return 0;}
  return shader;
}
static int particle_mask_prepare(void) {
  if(particle_mask.failed)return 0;
  if(particle_mask.program)return 1;
  GLuint vs=particle_mask_shader(GL_VERTEX_SHADER,
    "#version 300 es\n"
    "layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;"
    "layout(location=2) in vec3 style;uniform vec2 viewportSize;out vec2 uv;out vec3 maskStyle;"
    "void main(){uv=texcoord;maskStyle=style;gl_Position=vec4(position.x*2.0/viewportSize.x-1.0,"
    "1.0-position.y*2.0/viewportSize.y,0.0,1.0);}");
  GLuint fs=particle_mask_shader(GL_FRAGMENT_SHADER,
    "#version 300 es\nprecision highp float;"
    "in vec2 uv;in vec3 maskStyle;uniform vec2 viewportSize;"
    "uniform sampler2D particleImage;uniform sampler2D darkness;uniform vec4 tintColor;out vec4 color;"
    "void main(){float light=1.0-texture(darkness,gl_FragCoord.xy/viewportSize).a;"
    "float visibility=smoothstep(maskStyle.y,maskStyle.y+max(maskStyle.z,0.0001),light);"
    "vec4 texel=texture(particleImage,uv);"
    "color=vec4(mix(texel.rgb,tintColor.rgb*texel.a,tintColor.a),texel.a)*(maskStyle.x*visibility);}");
  if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);particle_mask.failed=1;return 0;}
  GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
  glDeleteShader(vs);glDeleteShader(fs);
  GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
  if(!ok){char log[512];glGetProgramInfoLog(program,sizeof(log),NULL,log);SOKOL_LOG(log);glDeleteProgram(program);particle_mask.failed=1;return 0;}
  particle_mask.program=program;
  particle_mask.viewport_uniform=glGetUniformLocation(program,"viewportSize");
  particle_mask.source_uniform=glGetUniformLocation(program,"particleImage");
  particle_mask.light_uniform=glGetUniformLocation(program,"darkness");
  particle_mask.tint_uniform=glGetUniformLocation(program,"tintColor");
  glGenVertexArrays(1,&particle_mask.vao);glGenBuffers(1,&particle_mask.vbo);
  glGenSamplers(1,&particle_mask.sampler);
  glSamplerParameteri(particle_mask.sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glSamplerParameteri(particle_mask.sampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glSamplerParameteri(particle_mask.sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glSamplerParameteri(particle_mask.sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glGenSamplers(1,&particle_mask.nearest_sampler);
  glSamplerParameteri(particle_mask.nearest_sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glSamplerParameteri(particle_mask.nearest_sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glSamplerParameteri(particle_mask.nearest_sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glSamplerParameteri(particle_mask.nearest_sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  return 1;
}
static void particle_mask_shutdown(void) {
  if(particle_mask.program)glDeleteProgram(particle_mask.program);
  if(particle_mask.vao)glDeleteVertexArrays(1,&particle_mask.vao);
  if(particle_mask.vbo)glDeleteBuffers(1,&particle_mask.vbo);
  if(particle_mask.sampler)glDeleteSamplers(1,&particle_mask.sampler);
  if(particle_mask.nearest_sampler)glDeleteSamplers(1,&particle_mask.nearest_sampler);
  free(particle_mask.vertices);memset(&particle_mask,0,sizeof(particle_mask));
}
static void particle_mask_reset(void) {particle_mask.count=particle_mask.batch_count=0;particle_mask.active=0;}
static int particle_mask_begin(int texture_id,float threshold,float softness) {
  if(!min_layers_texture()||texture_id<0||texture_id>=256)return 0;
  sg_gl_image_info info=sg_gl_query_image_info(state.textures[texture_id]);
  particle_mask.texture=info.tex[info.active_slot];
  particle_mask.threshold=threshold;particle_mask.softness=softness;
  memset(particle_mask.tint,0,sizeof(particle_mask.tint));particle_mask.nearest=0;
  return particle_mask.texture!=0;
}
static void particle_mask_draw(int texture_id,int texture_width,int texture_height,
    float x,float y,float rotation,float size,float opacity) {
  (void)texture_id;
  if(!(opacity>0)||size==0||!isfinite(size)||particle_mask.count+6>PARTICLE_MASK_MAX_VERTICES)return;
  particle_mask_batch *last=particle_mask.batch_count ? &particle_mask.batches[particle_mask.batch_count-1] : NULL;
  int new_batch=!last||last->texture!=particle_mask.texture||last->nearest!=particle_mask.nearest||
    memcmp(last->tint,particle_mask.tint,sizeof(particle_mask.tint));
  if(new_batch&&particle_mask.batch_count==PARTICLE_MASK_MAX_BATCHES)return;
  if(particle_mask.count+6>particle_mask.capacity){
    uint32_t capacity=particle_mask.capacity?particle_mask.capacity*2:1536;
    if(capacity>PARTICLE_MASK_MAX_VERTICES)capacity=PARTICLE_MASK_MAX_VERTICES;
    void *next=realloc(particle_mask.vertices,(size_t)capacity*sizeof(particle_mask_vertex));
    if(!next)return;particle_mask.vertices=next;particle_mask.capacity=capacity;
  }
  if(new_batch){
    particle_mask_batch *batch=&particle_mask.batches[particle_mask.batch_count++];
    *batch=(particle_mask_batch){.texture=particle_mask.texture,.first=particle_mask.count,.nearest=particle_mask.nearest};
    memcpy(batch->tint,particle_mask.tint,sizeof(batch->tint));
  }
  float sx=convert_local_x_to_screen(x),sy=convert_local_y_to_screen(y);
  float w=scale_local_to_screen(size),h=texture_width>0?w*(float)texture_height/texture_width:w;
  float cs=cosf(rotation),sn=sinf(rotation);
  const float corners[8]={-w*.5f,-h*.5f,w*.5f,-h*.5f,w*.5f,h*.5f,-w*.5f,h*.5f};
  const float uv[8]={0,0,1,0,1,1,0,1};const int indices[6]={0,1,2,0,2,3};
  for(int i=0;i<6;i++){int j=indices[i]*2;float cx=corners[j],cy=corners[j+1];
    particle_mask.vertices[particle_mask.count++]=(particle_mask_vertex){sx+cx*cs-cy*sn,sy+cx*sn+cy*cs,
      uv[j],uv[j+1],opacity,particle_mask.threshold,particle_mask.softness};
  }
  particle_mask.batches[particle_mask.batch_count-1].count+=6;
}
static void particle_mask_end(void) { /* Queued batches are flushed together after NanoVG. */ }
static void particle_mask_present(void) {
  GLuint darkness=min_layers_texture();
  if(!particle_mask.count||!darkness||!particle_mask_prepare())return;
  glViewport(0,0,sapp_width(),sapp_height());
  glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(particle_mask.program);glBindVertexArray(particle_mask.vao);glBindBuffer(GL_ARRAY_BUFFER,particle_mask.vbo);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(particle_mask.count*sizeof(particle_mask_vertex)),particle_mask.vertices,GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(particle_mask_vertex),(void*)offsetof(particle_mask_vertex,x));
  glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(particle_mask_vertex),(void*)offsetof(particle_mask_vertex,u));
  glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(particle_mask_vertex),(void*)offsetof(particle_mask_vertex,opacity));
  glUniform2f(particle_mask.viewport_uniform,(float)sapp_width(),(float)sapp_height());
  glUniform1i(particle_mask.source_uniform,0);glUniform1i(particle_mask.light_uniform,1);
  glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,darkness);glBindSampler(1,particle_mask.sampler);
  glActiveTexture(GL_TEXTURE0);glBindSampler(0,particle_mask.sampler);
  for(uint32_t i=0;i<particle_mask.batch_count;i++){
    particle_mask_batch *batch=&particle_mask.batches[i];glBindTexture(GL_TEXTURE_2D,batch->texture);
    glBindSampler(0,batch->nearest?particle_mask.nearest_sampler:particle_mask.sampler);
    glUniform4fv(particle_mask.tint_uniform,1,batch->tint);
    glDrawArrays(GL_TRIANGLES,(GLint)batch->first,(GLsizei)batch->count);
  }
  glBindSampler(0,0);glBindSampler(1,0);glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
  sg_reset_state_cache();
}
#endif
