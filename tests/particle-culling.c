#include "../iteration-sapp/sapp/particles/particle_system.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int phases[3];
static void draw(int phase,int texture,int width,int height,float x,float y,float rotation,
  float size,float opacity,int mask,float threshold,float softness){
  (void)texture;(void)width;(void)height;(void)x;(void)y;(void)rotation;
  (void)size;(void)opacity;(void)mask;(void)threshold;(void)softness;phases[phase]++;
}
static void expect(int count){phases[0]=phases[1]=phases[2]=0;particle_system_render_all(draw);
  assert(phases[0]==!!count&&phases[1]==count&&phases[2]==!!count);}
int main(void){
  ParticleConfig c;particle_config_defaults(&c);c.space=PARTICLE_SPACE_LOCAL;
  c.lifetime.constant=100;c.max_particles=2;
  c.modifier_count=1;c.modifiers[0].type=PARTICLE_MOD_OSCILLATION;
  ParticleSystem*s=particle_system_create(&c);s->config.modifier_count=0;particle_system_set_texture(s->id,0,32,32);
  particle_system_emit(s,1);particle_system_set_viewport(-10,-10,10,10);expect(1);
  s->scale=0;expect(0);float age=s->pool.age[0];particle_system_update(s,.1f);assert(s->pool.age[0]>age);
  s->scale=1;s->position_x=100;expect(0);s->position_x=10;expect(1); // edge intersection
  s->position_x=0;s->pool.opacity[0]=0;expect(0);s->pool.opacity[0]=1;
  s->scale=-2;expect(1);s->scale=1;
  s->pool.x[0]=20;s->rotation=1.57079632679f;s->position_y=-20;expect(1);
  s->rotation=0;s->position_y=0;expect(0);
  s->config.modifier_count=1;s->config.modifiers[0].type=PARTICLE_MOD_OSCILLATION;
  s->config.modifiers[0].x=1;s->pool.random0[0]=.75f;
  s->pool.modifier_strength[0][0]=20;s->pool.modifier_frequency[0][0]=0;expect(1);
  s->config.modifier_count=0;s->texture_width=1;s->texture_height=100;expect(1);
  s->texture_width=s->texture_height=32;s->pool.x[0]=0;
  s->config.space=PARTICLE_SPACE_WORLD;s->scale=0;s->position_x=100;expect(1);
  s->pool.x[0]=100;expect(0);particle_system_set_viewport(NAN,NAN,NAN,NAN);expect(1);
  particle_system_shutdown_all();puts("PASS: hidden/offscreen systems, edge/rotation/aspect/oscillation, mirrored and world transforms; simulation retained");
}
