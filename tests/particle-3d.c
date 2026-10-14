#include "../iteration-sapp/sapp/particles/particle_system.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int draws;
static float last_z;
static void draw(int texture,int width,int height,float x,float y,float z,float rotation,float size,float opacity,
 int mask,float threshold,float softness) {
 (void)texture;(void)width;(void)height;(void)x;(void)y;(void)rotation;(void)size;(void)opacity;(void)mask;(void)threshold;(void)softness;
 assert(isfinite(z));draws++;last_z=z;
}
int main(void) {
 ParticleConfig config;particle_config_defaults(&config);
 config.max_particles=16;config.world3d=1;config.space=PARTICLE_SPACE_LOCAL;
 config.elevation.constant=10;config.velocity_z.constant=8;config.lifetime.constant=10;
 config.modifier_count=1;config.modifiers[0].type=PARTICLE_MOD_ACCELERATION;
 config.modifiers[0].z=-4;config.modifiers[0].strength.constant=1;
 ParticleSystem *system=particle_system_create(&config);assert(system);
 particle_system_set_texture(system->id,1,32,32);
 system->position_z=3;system->scale=2;
 particle_system_emit(system,2);particle_system_update(system,.5f);
 assert(fabsf(system->pool.vz[0]-6)<.0001f);
 assert(fabsf(system->pool.z[0]-13)<.0001f);
 particle_system_render_world(draw);assert(draws==2&&fabsf(last_z-29)<.0001f);
 system->pool.lifetime[0]=.1f;particle_system_update(system,.1f);
 assert(system->pool.count==1&&system->pool.z[0]>13);
 particle_system_destroy(system);
 particle_config_defaults(&config);config.world3d=1;config.spawn_shape=PARTICLE_SPAWN_SPHERE;
 config.spawn_radius=5;config.max_particles=1000;
 system=particle_system_create(&config);particle_system_emit(system,1000);
 for(unsigned i=0;i<system->pool.count;i++)assert(system->pool.x[i]*system->pool.x[i]+
  system->pool.y[i]*system->pool.y[i]+system->pool.z[i]*system->pool.z[i]<=25.0001f);
 particle_system_shutdown_all();puts("PASS: 3D particle elevation, gravity, transforms, compaction and sphere volumes");
}
