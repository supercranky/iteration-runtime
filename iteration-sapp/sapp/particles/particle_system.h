#ifndef ITERATION_PARTICLE_SYSTEM_H
#define ITERATION_PARTICLE_SYSTEM_H

#include <stdint.h>

#define PARTICLE_CURVE_MAX_KEYS 16
#define PARTICLE_MAX_MODIFIERS 16

typedef enum {
  PARTICLE_VALUE_CONSTANT,
  PARTICLE_VALUE_RANGE,
  PARTICLE_VALUE_CURVE
} ParticleValueType;

typedef struct {
  uint32_t count;
  float time[PARTICLE_CURVE_MAX_KEYS];
  float value[PARTICLE_CURVE_MAX_KEYS];
} ParticleCurve;

typedef struct {
  ParticleValueType type;
  union {
    float constant;
    struct { float min, max; } range;
    ParticleCurve curve;
  };
} ParticleValue;

typedef struct {
  uint32_t capacity;
  uint32_t count;
  float *x, *y;
  float *vx, *vy;
  float *age, *lifetime;
  float *size, *base_size, *rotation;
  float *opacity;
  float *random0, *random1;
  /* Spawn-resolved values needed by update modules. */
  float *angular_velocity;
  float *modifier_strength[PARTICLE_MAX_MODIFIERS];
  float *modifier_frequency[PARTICLE_MAX_MODIFIERS];
} ParticlePool;

typedef enum { PARTICLE_BOUNDARY_NONE, PARTICLE_BOUNDARY_WRAP, PARTICLE_BOUNDARY_BOUNCE,
  PARTICLE_BOUNDARY_KILL, PARTICLE_BOUNDARY_CLAMP } ParticleBoundary;
typedef enum { PARTICLE_SPAWN_POINT, PARTICLE_SPAWN_RECT, PARTICLE_SPAWN_CIRCLE,
  PARTICLE_SPAWN_AREA } ParticleSpawnShape;
typedef enum { PARTICLE_SPACE_WORLD, PARTICLE_SPACE_LOCAL } ParticleSpace;
typedef enum { PARTICLE_MOD_ACCELERATION, PARTICLE_MOD_DRAG, PARTICLE_MOD_OSCILLATION,
  PARTICLE_MOD_NOISE } ParticleModifierType;

typedef struct {
  ParticleModifierType type;
  float x, y;
  ParticleValue strength;
  ParticleValue frequency;
} ParticleModifier;

typedef struct {
  uint32_t max_particles;
  uint32_t seed;
  float area_x, area_y, area_width, area_height;
  ParticleBoundary boundary;
  float emission_rate;
  int prewarm;
  ParticleSpawnShape spawn_shape;
  float spawn_width, spawn_height, spawn_radius;
  ParticleSpace space;
  ParticleValue lifetime, velocity_x, velocity_y, speed, size, size_over_life, opacity;
  ParticleValue rotation, angular_velocity;
  ParticleModifier modifiers[PARTICLE_MAX_MODIFIERS];
  uint32_t modifier_count;
} ParticleConfig;

typedef struct ParticleSystem {
  uint32_t id;
  ParticleConfig config;
  ParticlePool pool;
  uint32_t rng;
  float emission_accumulator;
  float position_x, position_y, rotation, scale;
  int running, paused, destroyed;
  int texture_id, texture_width, texture_height;
  float mask_threshold, mask_softness;
  int mask_direct_light;
  struct ParticleSystem *next;
} ParticleSystem;

/* phase: 0 begins a system batch, 1 emits an instance, 2 ends the batch. */
typedef void (*ParticleDrawFn)(int phase, int texture_id, int texture_width, int texture_height,
  float x, float y, float rotation, float size, float opacity, int mask_direct_light,
  float mask_threshold, float mask_softness);

void particle_config_defaults(ParticleConfig *config);
float particle_value_evaluate(const ParticleValue *value, float random_value, float life);
ParticleSystem *particle_system_create(const ParticleConfig *config);
void particle_system_destroy(ParticleSystem *system);
void particle_system_start(ParticleSystem *system);
void particle_system_stop(ParticleSystem *system);
void particle_system_emit(ParticleSystem *system, uint32_t count);
void particle_system_update(ParticleSystem *system, float dt);
void particle_system_update_all(float dt);
void particle_system_render_all(ParticleDrawFn draw);
void particle_system_render_masked(ParticleDrawFn draw, int masked);
void particle_system_shutdown_all(void);
ParticleSystem *particle_system_find(uint32_t id);
void particle_system_set_texture(uint32_t id, int texture_id, int width, int height);

#endif
