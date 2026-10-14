/* Optional depth-tested 2.5D scene. Coordinates are logical screen-space ground
 * coordinates plus height; lighting UVs remain in the unprojected ground plane.
 * The application owns map geometry, discovery, camera ownership and UI. */
#if defined(SOKOL_GLES3) && !defined(SOKOL_METAL)
#define WORLD_VERTEX_LIMIT (65536u * 6u)
#define WORLD_BATCH_LIMIT 4096u
typedef struct { float x,y,z,u,v,lx,ly,alpha,shade,threshold,softness,tr,tg,tb,ta,unlit; } WorldVertex;
typedef struct { GLuint texture; unsigned first,count; int translucent; } WorldBatch;
static struct {
  int enabled,failed; float tilt,distance,light_extent,yaw,pivot_x,pivot_y;
  GLuint program,vao,vbo,sampler;
  GLint projection,extent,image,darkness,has_light,radial_light,radial_brightness;
  float radial[4],brightness[2];
  WorldVertex *vertices; unsigned count,capacity,batch_count;
  WorldBatch batches[WORLD_BATCH_LIMIT];
} world_scene;
static int world_scene_prepare(void) {
  if(world_scene.failed)return 0;
  if(world_scene.program)return 1;
  GLuint vs=particle_mask_shader(GL_VERTEX_SHADER,
    "#version 300 es\nlayout(location=0) in vec3 position;layout(location=1) in vec2 texcoord;"
    "layout(location=2) in vec4 style;layout(location=3) in vec2 mask;layout(location=4) in vec4 tint;layout(location=5) in float unlit;uniform mat4 projection;uniform vec2 extent;"
    "out vec2 uv;out vec2 lightUV;out vec2 lightGround;out vec2 material;out vec2 maskStyle;out vec4 tintColor;out float unlitSurface;"
    "void main(){gl_Position=projection*vec4(position.x,-position.y,position.z,1.0);"
    "uv=texcoord;lightGround=style.xy;lightUV=vec2(style.x/extent.x+0.5,0.5-style.y/extent.y);material=style.zw;maskStyle=mask;tintColor=tint;unlitSurface=unlit;}");
  GLuint fs=particle_mask_shader(GL_FRAGMENT_SHADER,
    "#version 300 es\nprecision highp float;in vec2 uv;in vec2 lightUV;in vec2 lightGround;in vec2 material;in vec2 maskStyle;in vec4 tintColor;in float unlitSurface;"
    "uniform sampler2D image;uniform sampler2D darkness;uniform bool hasLight;uniform vec4 radialLight;uniform vec2 radialBrightness;out vec4 color;"
    "void main(){vec4 texel=texture(image,uv);if(texel.a<0.08)discard;"
    "float light=hasLight&&unlitSurface<0.5?1.0-texture(darkness,lightUV).a:1.0;"
    "if(hasLight&&unlitSurface<0.5&&(any(lessThan(lightUV,vec2(0)))||any(greaterThan(lightUV,vec2(1)))))light=0.0;"
    "if(unlitSurface>1.5)light=mix(radialBrightness.x,radialBrightness.y,smoothstep(radialLight.z,max(radialLight.z+0.001,radialLight.w),distance(lightGround,radialLight.xy)));"
    "float visibility=maskStyle.x>=0.0?smoothstep(maskStyle.x,maskStyle.x+max(maskStyle.y,0.0001),light):1.0;"
    "vec3 rgb=mix(texel.rgb,tintColor.rgb*texel.a,tintColor.a);"
    "color=vec4(rgb*light*material.y,texel.a)*material.x*visibility;}");
  if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);world_scene.failed=1;return 0;}
  GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
  glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){glDeleteProgram(p);world_scene.failed=1;return 0;}
  world_scene.program=p;
  world_scene.projection=glGetUniformLocation(p,"projection");world_scene.extent=glGetUniformLocation(p,"extent");
  world_scene.image=glGetUniformLocation(p,"image");world_scene.darkness=glGetUniformLocation(p,"darkness");
  world_scene.has_light=glGetUniformLocation(p,"hasLight");
  world_scene.radial_light=glGetUniformLocation(p,"radialLight");
  world_scene.radial_brightness=glGetUniformLocation(p,"radialBrightness");
  glGenVertexArrays(1,&world_scene.vao);glGenBuffers(1,&world_scene.vbo);glGenSamplers(1,&world_scene.sampler);
  glSamplerParameteri(world_scene.sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glSamplerParameteri(world_scene.sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glSamplerParameteri(world_scene.sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glSamplerParameteri(world_scene.sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  return 1;
}
int engine_world_enabled(void) {return world_scene.enabled;}
float engine_world_light_extent(void){return world_scene.enabled?world_scene.light_extent:1;}
uint32_t engine_world_light_texture(int *width,int *height) {
  min_layers_dimensions(width,height);return min_layers_texture();
}
void engine_world_projection(float *m) {
  memset(m,0,16*sizeof(float));
  float c=cosf(world_scene.tilt),s=sinf(world_scene.tilt),d=world_scene.distance;
  float aspect=(float)sapp_width()/fmaxf(1,sapp_height());
  m[0]=1.0f/(500*aspect);m[5]=c/500;m[9]=s/500;
  m[6]=s/d;m[10]=-c/d;m[7]=s/d;m[11]=-c/d;m[15]=1;
  if(world_scene.yaw!=0) {
    // Clockwise ground-plane rotation around a logical ground pivot. The
    // projection consumes Y-up coordinates; lighting remains in world space.
    float cy=cosf(world_scene.yaw),sy=sinf(world_scene.yaw);
    float tx=(1-cy)*world_scene.pivot_x+sy*world_scene.pivot_y;
    float ty=sy*world_scene.pivot_x-(1-cy)*world_scene.pivot_y;
    for(int row=0;row<4;row++) {
      float a=m[row],b=m[4+row];
      m[row]=a*cy-b*sy;m[4+row]=a*sy+b*cy;
      m[12+row]+=a*tx+b*ty;
    }
  }
}
static void world_scene_reset(void) {world_scene.count=world_scene.batch_count=0;}
static void world_scene_shutdown(void) {
  if(world_scene.program)glDeleteProgram(world_scene.program);
  if(world_scene.vao)glDeleteVertexArrays(1,&world_scene.vao);
  if(world_scene.vbo)glDeleteBuffers(1,&world_scene.vbo);
  if(world_scene.sampler)glDeleteSamplers(1,&world_scene.sampler);
  free(world_scene.vertices);memset(&world_scene,0,sizeof(world_scene));
}
static int world_scene_quad(int texture,const float *v,float sx,float sy,float sw,float sh,float alpha,float shade,int translucent,int flip) {
  if(!world_scene.enabled||texture<0||texture>=256||world_scene.count+6>WORLD_VERTEX_LIMIT)return 0;
  float tw=state.texture_sizes[texture].widthTexture,th=state.texture_sizes[texture].heightTexture;
  if(tw<=0||th<=0)return 0;
  sg_gl_image_info info=sg_gl_query_image_info(state.textures[texture]);GLuint image=info.tex[info.active_slot];
  if(!image)return 0;
  WorldBatch *last=world_scene.batch_count?&world_scene.batches[world_scene.batch_count-1]:NULL;
  int new_batch=!last||last->texture!=image||last->translucent!=translucent;
  if(new_batch&&world_scene.batch_count==WORLD_BATCH_LIMIT)return 0;
  if(world_scene.count+6>world_scene.capacity){
    unsigned capacity=world_scene.capacity?world_scene.capacity*2:1536;if(capacity>WORLD_VERTEX_LIMIT)capacity=WORLD_VERTEX_LIMIT;
    void *next=realloc(world_scene.vertices,capacity*sizeof(WorldVertex));if(!next)return 0;
    world_scene.vertices=next;world_scene.capacity=capacity;
  }
  if(new_batch)world_scene.batches[world_scene.batch_count++]=(WorldBatch){image,world_scene.count,0,translucent};
  const int indices[6]={0,1,2,0,2,3};const float uv[8]={0,0,1,0,1,1,0,1};
  for(int i=0;i<6;i++){int j=indices[i];const float *p=v+j*5;
    world_scene.vertices[world_scene.count++]=(WorldVertex){p[0],p[1],p[2],
      (sx+(flip?1-uv[j*2]:uv[j*2])*sw)/tw,(sy+uv[j*2+1]*sh)/th,p[3],p[4],alpha,shade,-1,0};}
  world_scene.batches[world_scene.batch_count-1].count+=6;return 1;
}
static void world_scene_present(int translucent) {
  if(!world_scene.enabled||!world_scene_prepare())return;
  if(translucent==2) {
    int found=0;for(unsigned i=0;i<world_scene.batch_count;i++)if(world_scene.batches[i].translucent==2){found=1;break;}
    if(!found)return;
  }
  GLint scissor[4];glGetIntegerv(GL_SCISSOR_BOX,scissor);
  glViewport(0,0,sapp_width(),sapp_height());glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  if(translucent==2)glDisable(GL_DEPTH_TEST);else glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
  if(!translucent){glClearColor(0,0,0,1);glClearDepthf(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
  glDepthMask(translucent?GL_FALSE:GL_TRUE);
  glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(world_scene.program);glBindVertexArray(world_scene.vao);glBindBuffer(GL_ARRAY_BUFFER,world_scene.vbo);
  glBufferData(GL_ARRAY_BUFFER,world_scene.count*sizeof(WorldVertex),world_scene.vertices,GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),(void*)offsetof(WorldVertex,x));
  glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),(void*)offsetof(WorldVertex,u));
  glEnableVertexAttribArray(2);glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),(void*)offsetof(WorldVertex,lx));
  glEnableVertexAttribArray(3);glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),(void*)offsetof(WorldVertex,threshold));
  glEnableVertexAttribArray(4);glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),(void*)offsetof(WorldVertex,tr));
  glEnableVertexAttribArray(5);glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,sizeof(WorldVertex),(void*)offsetof(WorldVertex,unlit));
  float matrix[16];engine_world_projection(matrix);glUniformMatrix4fv(world_scene.projection,1,GL_FALSE,matrix);
  glUniform2f(world_scene.extent,1000.0f*sapp_width()/fmaxf(1,sapp_height())*world_scene.light_extent,1000*world_scene.light_extent);
  glUniform4fv(world_scene.radial_light,1,world_scene.radial);
  glUniform2fv(world_scene.radial_brightness,1,world_scene.brightness);
  GLuint light=min_layers_texture();glUniform1i(world_scene.has_light,light!=0);
  glUniform1i(world_scene.image,0);glUniform1i(world_scene.darkness,1);
  glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,light);glBindSampler(1,0);
  glActiveTexture(GL_TEXTURE0);glBindSampler(0,world_scene.sampler);
  for(unsigned i=0;i<world_scene.batch_count;i++){WorldBatch *b=&world_scene.batches[i];
    if(b->translucent!=translucent)continue;glBindTexture(GL_TEXTURE_2D,b->texture);glDrawArrays(GL_TRIANGLES,b->first,b->count);}
  glDepthMask(GL_TRUE);glDisable(GL_DEPTH_TEST);glBindSampler(0,0);glBindSampler(1,0);
  glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);sg_reset_state_cache();
}
static JSValue js_world_camera(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;int enabled=argc?JS_ToBool(ctx,argv[0]):0;if(enabled<0)return JS_EXCEPTION;
  double tilt=35,distance=1600,extent=1;
  if(argc>1&&JS_ToFloat64(ctx,&tilt,argv[1]))return JS_EXCEPTION;
  if(argc>2&&JS_ToFloat64(ctx,&distance,argv[2]))return JS_EXCEPTION;
  if(argc>3&&JS_ToFloat64(ctx,&extent,argv[3]))return JS_EXCEPTION;
  if(!isfinite(extent)||extent<1||extent>4)return JS_ThrowRangeError(ctx,"invalid world light extent");
  if(!isfinite(tilt)||tilt<0||tilt>65||!isfinite(distance)||distance<800||distance>10000)
    return JS_ThrowRangeError(ctx,"invalid world camera");
  if(enabled&&!world_scene_prepare())return JS_FALSE;
  world_scene.enabled=enabled;world_scene.tilt=tilt*0.017453292519943295;world_scene.distance=distance;world_scene.light_extent=extent;
  return JS_TRUE;
}
static JSValue js_world_camera_yaw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;double values[3]={0};
  for(int i=0;i<3;i++) {
    if(argc>i&&JS_ToFloat64(ctx,&values[i],argv[i]))return JS_EXCEPTION;
    if(!isfinite(values[i])||fabs(values[i])>(i?1e7:360))return JS_ThrowRangeError(ctx,"invalid world camera yaw/pivot");
  }
  world_scene.yaw=values[0]*0.017453292519943295;
  world_scene.pivot_x=values[1];world_scene.pivot_y=values[2];
  return JS_TRUE;
}
static JSValue js_world_radial_light(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;double values[6];
  if(argc<6)return JS_ThrowTypeError(ctx,"world radial light requires x, y, inner, outer, core and ambient brightness");
  for(int i=0;i<6;i++){
    if(JS_ToFloat64(ctx,&values[i],argv[i]))return JS_EXCEPTION;
    if(!isfinite(values[i])||fabs(values[i])>1e7)return JS_ThrowRangeError(ctx,"invalid radial light");
  }
  if(values[2]<0||values[3]<values[2]||values[4]<0||values[4]>1||values[5]<0||values[5]>1)
    return JS_ThrowRangeError(ctx,"invalid radial light range");
  for(int i=0;i<4;i++)world_scene.radial[i]=values[i];
  for(int i=0;i<2;i++)world_scene.brightness[i]=values[i+4];
  return JS_TRUE;
}
static JSValue js_world_quad(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
  if(gpu_profile.sampling)gpu_profile.work[0]++;
  (void)self;int texture;if(argc<6)return JS_ThrowTypeError(ctx,"drawWorldQuad requires texture, source rectangle and 20 vertex values");
  if(JS_ToInt32(ctx,&texture,argv[0]))return JS_EXCEPTION;
  if(texture<0||texture>=256)return JS_ThrowRangeError(ctx,"invalid world texture");
  double args[6]={0,0,0,0,1,1};
  for(int i=0;i<4;i++)if(JS_ToFloat64(ctx,&args[i],argv[i+1]))return JS_EXCEPTION;
  for(int i=4;i<6;i++)if(argc>i+2&&JS_ToFloat64(ctx,&args[i],argv[i+2]))return JS_EXCEPTION;
  for(int i=0;i<6;i++)if(!isfinite(args[i])||fabs(args[i])>1e7)return JS_ThrowRangeError(ctx,"invalid world material");
  if(args[0]<0||args[1]<0||args[2]<=0||args[3]<=0||args[4]<0||args[4]>1||args[5]<0||args[5]>2)
    return JS_ThrowRangeError(ctx,"invalid world material range");
  JSValue length_value=JS_GetPropertyStr(ctx,argv[5],"length");uint32_t length=0;
  int length_error=JS_ToUint32(ctx,&length,length_value);JS_FreeValue(ctx,length_value);
  if(length_error)return JS_EXCEPTION;
  if(length!=20)return JS_ThrowRangeError(ctx,"world quad requires exactly 20 vertex values");
  float vertices[20];for(int i=0;i<20;i++){JSValue value=JS_GetPropertyUint32(ctx,argv[5],i);double number;
    int error=JS_ToFloat64(ctx,&number,value);JS_FreeValue(ctx,value);if(error)return JS_EXCEPTION;
    if(!isfinite(number)||fabs(number)>1e7)return JS_ThrowRangeError(ctx,"invalid world vertex");vertices[i]=number;}
  int translucent=argc>8?JS_ToBool(ctx,argv[8]):0,flip=argc>9?JS_ToBool(ctx,argv[9]):0;
  if(translucent<0||flip<0)return JS_EXCEPTION;
  float tint[4]={0};
  if(argc>10&&!JS_IsNull(argv[10])&&!JS_IsUndefined(argv[10])) {
    const char *keys[4]={"r","g","b","amount"};
    for(int i=0;i<4;i++){JSValue value=JS_GetPropertyStr(ctx,argv[10],keys[i]);double n;
      int error=JS_ToFloat64(ctx,&n,value);JS_FreeValue(ctx,value);if(error)return JS_EXCEPTION;
      if(!isfinite(n)||n<0||n>1)return JS_ThrowRangeError(ctx,"invalid world tint");tint[i]=n;}
  }
  double threshold=-1,softness=0;
  if(argc>11&&JS_ToFloat64(ctx,&threshold,argv[11]))return JS_EXCEPTION;
  if(argc>12&&JS_ToFloat64(ctx,&softness,argv[12]))return JS_EXCEPTION;
  if(!isfinite(threshold)||threshold < -1||threshold>1||!isfinite(softness)||softness<0||softness>1)
    return JS_ThrowRangeError(ctx,"invalid world light mask");
  int unlit=argc>13?JS_ToBool(ctx,argv[13]):0;
  int radial=argc>14?JS_ToBool(ctx,argv[14]):0;
  if(unlit<0||radial<0)return JS_EXCEPTION;
  if(radial)unlit=2;
  unsigned first=world_scene.count;
  int result=world_scene_quad(texture,vertices,args[0],args[1],args[2],args[3],args[4],args[5],translucent,flip);
  if(result)for(unsigned i=first;i<world_scene.count;i++) {
    WorldVertex *v=&world_scene.vertices[i];v->tr=tint[0];v->tg=tint[1];v->tb=tint[2];v->ta=tint[3];
    v->threshold=threshold;v->softness=softness;v->unlit=unlit;
  }
  return JS_NewBool(ctx,result);
}
#else
int engine_world_enabled(void){return 0;}
float engine_world_light_extent(void){return 1;}
uint32_t engine_world_light_texture(int *width,int *height){*width=*height=0;return 0;}
void engine_world_projection(float *m){memset(m,0,16*sizeof(float));}
static void world_scene_reset(void){}
static void world_scene_shutdown(void){}
static void world_scene_present(int translucent){(void)translucent;}
static JSValue js_world_camera(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){return JS_FALSE;}
static JSValue js_world_camera_yaw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){return JS_FALSE;}
static JSValue js_world_radial_light(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){return JS_FALSE;}
static JSValue js_world_quad(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){return JS_FALSE;}
#endif
