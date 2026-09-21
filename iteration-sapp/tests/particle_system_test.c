#include "../sapp/particles/particle_system.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static ParticleValue value(float n){ParticleValue v={0};v.type=PARTICLE_VALUE_CONSTANT;v.constant=n;return v;}
static ParticleValue range(float a,float b){ParticleValue v={0};v.type=PARTICLE_VALUE_RANGE;v.range.min=a;v.range.max=b;return v;}
static ParticleSystem *make(ParticleBoundary boundary){ParticleConfig c;particle_config_defaults(&c);c.max_particles=10000;c.lifetime=value(100);c.area_width=10;c.area_height=10;c.boundary=boundary;return particle_system_create(&c);}
static int near(float a,float b){return fabsf(a-b)<.0001f;}

int main(void){
  ParticleSystem*s=make(PARTICLE_BOUNDARY_NONE);particle_system_emit(s,10000);assert(s->pool.count==10000);
  float *allocation=s->pool.x;for(int i=0;i<100;i++)particle_system_update(s,.016f);assert(s->pool.x==allocation);particle_system_update(s,100);assert(s->pool.count==0);particle_system_destroy(s);

  s=make(PARTICLE_BOUNDARY_WRAP);particle_system_emit(s,1);s->pool.x[0]=5;s->pool.y[0]=5;s->pool.vx[0]=100;particle_system_update(s,1);assert(near(s->pool.x[0],5));particle_system_destroy(s);
  s=make(PARTICLE_BOUNDARY_BOUNCE);particle_system_emit(s,1);s->pool.x[0]=5;s->pool.y[0]=5;s->pool.vx[0]=90;particle_system_update(s,1);assert(near(s->pool.x[0],5));assert(s->pool.vx[0]<0);particle_system_destroy(s);
  s=make(PARTICLE_BOUNDARY_KILL);particle_system_emit(s,1);s->pool.vx[0]=20;particle_system_update(s,1);assert(s->pool.count==0);particle_system_destroy(s);
  s=make(PARTICLE_BOUNDARY_CLAMP);particle_system_emit(s,1);s->pool.vx[0]=20;particle_system_update(s,1);assert(near(s->pool.x[0],10));particle_system_destroy(s);

  ParticleConfig c;particle_config_defaults(&c);c.max_particles=100;c.lifetime=range(10,20);c.size=range(1,3);c.emission_rate=6;c.seed=9;s=particle_system_create(&c);particle_system_start(s);for(int i=0;i<60;i++)particle_system_update(s,1.0f/60);assert(s->pool.count==6);float size=s->pool.size[0];for(int i=0;i<10;i++)particle_system_update(s,.01f);assert(s->pool.size[0]==size);particle_system_destroy(s);

  particle_config_defaults(&c);c.max_particles=1;c.lifetime=value(10);c.size=range(1,3);c.size_over_life.type=PARTICLE_VALUE_CURVE;c.size_over_life.curve.count=2;c.size_over_life.curve.time[0]=0;c.size_over_life.curve.value[0]=.5f;c.size_over_life.curve.time[1]=1;c.size_over_life.curve.value[1]=1.5f;s=particle_system_create(&c);particle_system_emit(s,1);float base_size=s->pool.base_size[0];assert(near(s->pool.size[0],base_size*.5f));particle_system_update(s,5);assert(near(s->pool.size[0],base_size));particle_system_destroy(s);

  particle_config_defaults(&c);c.max_particles=48;c.lifetime=range(12,24);c.emission_rate=1.5f;c.prewarm=1;s=particle_system_create(&c);particle_system_emit(s,12);particle_system_start(s);for(int i=0;i<60*300;i++){particle_system_update(s,1.0f/60);assert(s->pool.count>0);}particle_system_destroy(s);

  particle_config_defaults(&c);c.max_particles=1;c.lifetime=value(100);c.modifier_count=1;c.modifiers[0].type=PARTICLE_MOD_OSCILLATION;c.modifiers[0].x=1;c.modifiers[0].strength=value(10);c.modifiers[0].frequency=value(1);s=particle_system_create(&c);particle_system_emit(s,1);float base=s->pool.x[0];for(int i=0;i<600;i++)particle_system_update(s,1.0f/60);assert(near(s->pool.x[0],base));particle_system_destroy(s);
  puts("particle_system_test: ok");return 0;
}
