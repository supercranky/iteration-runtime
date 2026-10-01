#include "particle_system.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PARTICLE_PI 3.14159265358979323846f
#define PFIELDS 13

static ParticleSystem *systems;
static uint32_t next_id = 1;
static struct { float left,top,right,bottom; int enabled; } render_viewport;
void particle_system_set_viewport(float left,float top,float right,float bottom) {
  render_viewport.left=fminf(left,right);render_viewport.right=fmaxf(left,right);
  render_viewport.top=fminf(top,bottom);render_viewport.bottom=fmaxf(top,bottom);
  render_viewport.enabled=isfinite(left)&&isfinite(top)&&isfinite(right)&&isfinite(bottom);
}

static float rng01(ParticleSystem *s) {
  uint32_t x = s->rng ? s->rng : 0x6d2b79f5u;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5; s->rng = x;
  return (float)(x >> 8) * (1.0f / 16777216.0f);
}

static ParticleValue constant(float value) {
  ParticleValue result; memset(&result, 0, sizeof(result));
  result.type = PARTICLE_VALUE_CONSTANT; result.constant = value; return result;
}

void particle_config_defaults(ParticleConfig *c) {
  memset(c, 0, sizeof(*c)); c->max_particles = 100; c->seed = 0x12345678u;
  c->area_width = 1000; c->area_height = 1000; c->boundary = PARTICLE_BOUNDARY_NONE;
  c->spawn_shape = PARTICLE_SPAWN_POINT; c->space = PARTICLE_SPACE_WORLD;
  c->lifetime = constant(1); c->velocity_x = constant(0); c->velocity_y = constant(0);
  c->speed = constant(1); c->size = constant(1); c->size_over_life = constant(1); c->opacity = constant(1);
  c->rotation = constant(0); c->angular_velocity = constant(0);
}

float particle_value_evaluate(const ParticleValue *v, float r, float life) {
  if (v->type == PARTICLE_VALUE_RANGE) return v->range.min + (v->range.max-v->range.min)*r;
  if (v->type != PARTICLE_VALUE_CURVE || !v->curve.count) return v->type == PARTICLE_VALUE_CONSTANT ? v->constant : 0;
  if (life <= v->curve.time[0]) return v->curve.value[0];
  for (uint32_t i=1;i<v->curve.count;i++) if (life <= v->curve.time[i]) {
    float d=v->curve.time[i]-v->curve.time[i-1];
    float t=d>0?(life-v->curve.time[i-1])/d:1;
    return v->curve.value[i-1]+(v->curve.value[i]-v->curve.value[i-1])*t;
  }
  return v->curve.value[v->curve.count-1];
}

static int pool_init(ParticlePool *p, uint32_t capacity, uint32_t modifiers) {
  memset(p,0,sizeof(*p)); p->capacity=capacity;
  size_t floats=(size_t)capacity*PFIELDS;
  float *block=(float*)calloc(floats,sizeof(float)); if(!block)return 0;
  p->x=block; p->y=p->x+capacity; p->vx=p->y+capacity; p->vy=p->vx+capacity;
  p->age=p->vy+capacity; p->lifetime=p->age+capacity; p->size=p->lifetime+capacity;
  p->base_size=p->size+capacity; p->rotation=p->base_size+capacity; p->opacity=p->rotation+capacity;
  p->random0=p->opacity+capacity; p->random1=p->random0+capacity;
  p->angular_velocity=p->random1+capacity;
  for(uint32_t m=0;m<modifiers;m++) {
    p->modifier_strength[m]=(float*)calloc((size_t)capacity*2,sizeof(float));
    if(!p->modifier_strength[m]) return 0;
    p->modifier_frequency[m]=p->modifier_strength[m]+capacity;
  }
  return 1;
}

static void pool_free(ParticlePool *p) {
  free(p->x);
  for(uint32_t m=0;m<PARTICLE_MAX_MODIFIERS;m++) free(p->modifier_strength[m]);
  memset(p,0,sizeof(*p));
}

ParticleSystem *particle_system_create(const ParticleConfig *config) {
  if(!config || !config->max_particles) return NULL;
  ParticleSystem *s=(ParticleSystem*)calloc(1,sizeof(*s)); if(!s)return NULL;
  s->config=*config; s->rng=config->seed; s->scale=1; s->texture_id=-1; s->id=next_id++;
  if(!pool_init(&s->pool,config->max_particles,config->modifier_count)){pool_free(&s->pool);free(s);return NULL;}
  s->next=systems; systems=s;
  if(config->prewarm && config->emission_rate>0) {
    s->running=1;
    float duration=particle_value_evaluate(&config->lifetime,.5f,0);
    if(config->lifetime.type==PARTICLE_VALUE_RANGE) duration=config->lifetime.range.max;
    if(duration>120)duration=120;
    for(float t=0;t<duration;t+=1.0f/60.0f) particle_system_update(s,1.0f/60.0f);
    s->running=0;
  }
  return s;
}

void particle_system_destroy(ParticleSystem *s) {
  if(!s||s->destroyed)return; s->destroyed=1;
  ParticleSystem **at=&systems; while(*at&&*at!=s)at=&(*at)->next; if(*at)*at=s->next;
  pool_free(&s->pool); free(s);
}

ParticleSystem *particle_system_find(uint32_t id){for(ParticleSystem*s=systems;s;s=s->next)if(s->id==id)return s;return NULL;}
void particle_system_set_texture(uint32_t id,int texture_id,int width,int height){ParticleSystem*s=particle_system_find(id);if(s){s->texture_id=texture_id;s->texture_width=width;s->texture_height=height;}}
void particle_system_start(ParticleSystem*s){if(s)s->running=1;}
void particle_system_stop(ParticleSystem*s){if(s){s->running=0;s->emission_accumulator=0;}}

static void spawn_one(ParticleSystem *s) {
  ParticlePool*p=&s->pool;if(p->count>=p->capacity)return;uint32_t i=p->count++;
  float x=0,y=0,r0=rng01(s),r1=rng01(s);
  switch(s->config.spawn_shape){
    case PARTICLE_SPAWN_RECT:x=(r0-.5f)*s->config.spawn_width;y=(r1-.5f)*s->config.spawn_height;break;
    case PARTICLE_SPAWN_CIRCLE:{float a=2*PARTICLE_PI*r0,r=sqrtf(r1)*s->config.spawn_radius;x=cosf(a)*r;y=sinf(a)*r;break;}
    case PARTICLE_SPAWN_AREA:x=s->config.area_x+r0*s->config.area_width;y=s->config.area_y+r1*s->config.area_height;break;
    default:break;
  }
  if(s->config.space==PARTICLE_SPACE_WORLD){float cs=cosf(s->rotation),sn=sinf(s->rotation);float tx=x*s->scale,ty=y*s->scale;x=s->position_x+tx*cs-ty*sn;y=s->position_y+tx*sn+ty*cs;}
  p->x[i]=x;p->y[i]=y;p->age[i]=0;p->random0[i]=r0;p->random1[i]=r1;
  p->lifetime[i]=fmaxf(.0001f,particle_value_evaluate(&s->config.lifetime,rng01(s),0));
  p->vx[i]=particle_value_evaluate(&s->config.velocity_x,rng01(s),0);
  p->vy[i]=particle_value_evaluate(&s->config.velocity_y,rng01(s),0);
  float speed=particle_value_evaluate(&s->config.speed,rng01(s),0);p->vx[i]*=speed;p->vy[i]*=speed;
  p->base_size[i]=particle_value_evaluate(&s->config.size,rng01(s),0);
  p->size[i]=p->base_size[i]*particle_value_evaluate(&s->config.size_over_life,p->random0[i],0);
  p->rotation[i]=particle_value_evaluate(&s->config.rotation,rng01(s),0);
  p->angular_velocity[i]=particle_value_evaluate(&s->config.angular_velocity,rng01(s),0);
  p->opacity[i]=particle_value_evaluate(&s->config.opacity,rng01(s),0);
  for(uint32_t m=0;m<s->config.modifier_count;m++){
    p->modifier_strength[m][i]=particle_value_evaluate(&s->config.modifiers[m].strength,rng01(s),0);
    p->modifier_frequency[m][i]=particle_value_evaluate(&s->config.modifiers[m].frequency,rng01(s),0);
  }
}
void particle_system_emit(ParticleSystem*s,uint32_t n){if(!s)return;while(n--&&s->pool.count<s->pool.capacity)spawn_one(s);}

static void remove_particle(ParticleSystem*s,uint32_t i){ParticlePool*p=&s->pool;uint32_t q=--p->count;if(i==q)return;
#define CP(field) p->field[i]=p->field[q]
  CP(x);CP(y);CP(vx);CP(vy);CP(age);CP(lifetime);CP(size);CP(base_size);CP(rotation);CP(opacity);CP(random0);CP(random1);CP(angular_velocity);
  for(uint32_t m=0;m<s->config.modifier_count;m++){p->modifier_strength[m][i]=p->modifier_strength[m][q];p->modifier_frequency[m][i]=p->modifier_frequency[m][q];}
#undef CP
}

static float wrap(float v,float lo,float span){if(span<=0)return lo;v=fmodf(v-lo,span);if(v<0)v+=span;return lo+v;}
static float bounce(float v,float lo,float span,float*velocity){if(span<=0){*velocity=0;return lo;}float q=fmodf(v-lo,2*span);if(q<0)q+=2*span;if(q>span){q=2*span-q;*velocity=-fabsf(*velocity);}else *velocity=fabsf(*velocity);return lo+q;}

void particle_system_update(ParticleSystem*s,float dt){if(!s||s->paused||dt<=0)return;
  if(s->running&&s->config.emission_rate>0){s->emission_accumulator+=dt*s->config.emission_rate;uint32_t n=(uint32_t)floorf(s->emission_accumulator);s->emission_accumulator-=n;particle_system_emit(s,n);}
  ParticlePool*p=&s->pool;for(uint32_t i=0;i<p->count;){p->age[i]+=dt;if(p->age[i]>=p->lifetime[i]){remove_particle(s,i);continue;}
    float life=p->age[i]/p->lifetime[i];
    if(s->config.size_over_life.type==PARTICLE_VALUE_CURVE)p->size[i]=p->base_size[i]*particle_value_evaluate(&s->config.size_over_life,p->random0[i],life);
    else if(s->config.size.type==PARTICLE_VALUE_CURVE)p->size[i]=particle_value_evaluate(&s->config.size,p->random0[i],life);
    for(uint32_t m=0;m<s->config.modifier_count;m++){ParticleModifier*mod=&s->config.modifiers[m];float strength=mod->strength.type==PARTICLE_VALUE_CURVE?particle_value_evaluate(&mod->strength,p->random0[i],life):p->modifier_strength[m][i],freq=mod->frequency.type==PARTICLE_VALUE_CURVE?particle_value_evaluate(&mod->frequency,p->random1[i],life):p->modifier_frequency[m][i];
      if(mod->type==PARTICLE_MOD_ACCELERATION){p->vx[i]+=mod->x*strength*dt;p->vy[i]+=mod->y*strength*dt;}
      else if(mod->type==PARTICLE_MOD_DRAG){float factor=expf(-fmaxf(0,strength)*dt);p->vx[i]*=factor;p->vy[i]*=factor;}
      else if(mod->type==PARTICLE_MOD_NOISE){float phase=p->random0[i]*37.0f+p->age[i]*freq*2*PARTICLE_PI;p->vx[i]+=cosf(phase)*strength*dt;p->vy[i]+=sinf(phase*1.317f)*strength*dt;}
    }
    p->x[i]+=p->vx[i]*dt;p->y[i]+=p->vy[i]*dt;p->rotation[i]+=p->angular_velocity[i]*dt;
    if(s->config.opacity.type==PARTICLE_VALUE_CURVE)p->opacity[i]=particle_value_evaluate(&s->config.opacity,p->random1[i],life);
    float l=s->config.area_x,r=l+s->config.area_width,t=s->config.area_y,b=t+s->config.area_height;
    int outside=p->x[i]<l||p->x[i]>r||p->y[i]<t||p->y[i]>b;
    if(outside)switch(s->config.boundary){
      case PARTICLE_BOUNDARY_WRAP:p->x[i]=wrap(p->x[i],l,s->config.area_width);p->y[i]=wrap(p->y[i],t,s->config.area_height);break;
      case PARTICLE_BOUNDARY_BOUNCE:p->x[i]=bounce(p->x[i],l,s->config.area_width,&p->vx[i]);p->y[i]=bounce(p->y[i],t,s->config.area_height,&p->vy[i]);break;
      case PARTICLE_BOUNDARY_KILL:remove_particle(s,i);continue;
      case PARTICLE_BOUNDARY_CLAMP:p->x[i]=fmaxf(l,fminf(r,p->x[i]));p->y[i]=fmaxf(t,fminf(b,p->y[i]));break;
      default:break;
    }i++;}
}
void particle_system_update_all(float dt){for(ParticleSystem*s=systems;s;s=s->next)particle_system_update(s,dt);}

static void render_systems(ParticleDrawFn draw,int filter){if(!draw)return;for(ParticleSystem*s=systems;s;s=s->next){if(filter>=0&&!!s->mask_direct_light!=filter)continue;ParticlePool*p=&s->pool;
  /* Scale-zero local systems are hidden by the application. Keep simulating
     their ages/RNG, but never start a render batch (or traverse their particles). */
  if(s->texture_id<0||!p->count||(s->config.space==PARTICLE_SPACE_LOCAL&&s->scale==0))continue;
  int started=0;float aspect=s->texture_width>0?(float)s->texture_height/s->texture_width:1;
  float extent=.5f*sqrtf(1+aspect*aspect);
  for(uint32_t i=0;i<p->count;i++){
  if(!(p->opacity[i]>0))continue;
  float x=p->x[i],y=p->y[i],rot=p->rotation[i],size=p->size[i];
  for(uint32_t m=0;m<s->config.modifier_count;m++)if(s->config.modifiers[m].type==PARTICLE_MOD_OSCILLATION){float phase=p->random0[i]*2*PARTICLE_PI+p->age[i]*p->modifier_frequency[m][i]*2*PARTICLE_PI;float amount=sinf(phase)*p->modifier_strength[m][i];x+=s->config.modifiers[m].x*amount;y+=s->config.modifiers[m].y*amount;}
  if(s->config.space==PARTICLE_SPACE_LOCAL){float cs=cosf(s->rotation),sn=sinf(s->rotation),tx=x*s->scale,ty=y*s->scale;x=s->position_x+tx*cs-ty*sn;y=s->position_y+tx*sn+ty*cs;rot+=s->rotation;size*=s->scale;}
  if(size==0)continue;
  float radius=fabsf(size)*extent;
  if(render_viewport.enabled&&(x+radius<render_viewport.left||x-radius>render_viewport.right||
    y+radius<render_viewport.top||y-radius>render_viewport.bottom))continue;
  /* Lazy begin also rejects entire offscreen systems without any GPU work.
     A circumscribed rectangle radius includes rotation and non-square textures. */
  if(!started){draw(0,s->texture_id,s->texture_width,s->texture_height,0,0,0,0,0,s->mask_direct_light,s->mask_threshold,s->mask_softness);started=1;}
  draw(1,s->texture_id,s->texture_width,s->texture_height,x,y,rot,size,p->opacity[i],s->mask_direct_light,s->mask_threshold,s->mask_softness);
}if(started)draw(2,s->texture_id,s->texture_width,s->texture_height,0,0,0,0,0,s->mask_direct_light,s->mask_threshold,s->mask_softness);}}
void particle_system_render_all(ParticleDrawFn draw){render_systems(draw,-1);}
void particle_system_render_masked(ParticleDrawFn draw,int masked){render_systems(draw,!!masked);}
void particle_system_shutdown_all(void){while(systems)particle_system_destroy(systems);render_viewport.enabled=0;}
