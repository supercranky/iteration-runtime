#include "visibility.h"

#include "nanovg.h"
#include "sokol_app.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static struct
{
  NVGcontext *vg;
  visibility_transform_fn convert_x;
  visibility_transform_fn convert_y;
  visibility_transform_fn scale;
  visibility_transform_fn translate_x;
  visibility_transform_fn translate_y;
  double torch_time;
  uint64_t torch_frame;
} visibility_state;

void visibility_init(NVGcontext *vg,
                     visibility_transform_fn convert_x,
                     visibility_transform_fn convert_y,
                     visibility_transform_fn scale,
                     visibility_transform_fn translate_x,
                     visibility_transform_fn translate_y)
{
  visibility_state.vg = vg;
  visibility_state.convert_x = convert_x;
  visibility_state.convert_y = convert_y;
  visibility_state.scale = scale;
  visibility_state.translate_x = translate_x;
  visibility_state.translate_y = translate_y;
}

#define VISIBILITY_MAX_MAPS 256
#define VISIBILITY_TILE_SIZE 32.0
#define VISIBILITY_RAY_EPSILON 0.002
#define VISIBILITY_SHADOW_OPACITY 0.60f
#define VISIBILITY_FALLOFF_OPACITY 0.9f
#define VISIBILITY_FALLOFF_INNER_RADIUS 100.0f
#define VISIBILITY_FALLOFF_OUTER_RADIUS 600.0f
#define VISIBILITY_TORCH_MOTION_X 1.5
#define VISIBILITY_TORCH_MOTION_Y 1.0
#define VISIBILITY_TORCH_NOISE_SPEED_X 5.0
#define VISIBILITY_TORCH_NOISE_SPEED_Y 4.3
#define VISIBILITY_TORCH_STRENGTH_SPEED 6.2
#define VISIBILITY_TORCH_RADIUS_VARIATION 0.05
#define VISIBILITY_TORCH_OPACITY_VARIATION 0.025
#define VISIBILITY_SAMPLE_COUNT 5

typedef struct
{
  int width;
  int height;
  uint8_t *walls;
} visibility_map_t;

typedef struct
{
  double x;
  double y;
} visibility_point_t;

typedef struct
{
  int x;
  int y;
} visibility_tile_t;

static visibility_map_t visibility_maps[VISIBILITY_MAX_MAPS];

static int visibility_is_blocked(const visibility_map_t *map, int x, int y)
{
  if (!map || !map->walls || x < 0 || y < 0 || x >= map->width || y >= map->height)
  {
    return 1;
  }
  return map->walls[y * map->width + x] != 0;
}

static int visibility_is_corner_wall(const visibility_map_t *map, int x, int y)
{
  if (!map || !map->walls || x < 0 || y < 0 || x >= map->width || y >= map->height)
  {
    return 1;
  }
  return map->walls[y * map->width + x] == 1;
}

static int visibility_angle_compare(const void *left, const void *right)
{
  double a = *(const double *)left;
  double b = *(const double *)right;
  return a < b ? 1 : (a > b ? -1 : 0);
}

static double visibility_normalize_angle(double angle)
{
  return atan2(sin(angle), cos(angle));
}

static uint32_t visibility_noise_hash(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

static double visibility_noise_fade(double value)
{
  return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

static double visibility_perlin_noise(double position, uint32_t seed)
{
  int lattice = (int)floor(position);
  double fraction = position - lattice;
  double left_gradient = (visibility_noise_hash((uint32_t)lattice + seed) & 1U)
                             ? fraction
                             : -fraction;
  double right_distance = fraction - 1.0;
  double right_gradient = (visibility_noise_hash((uint32_t)(lattice + 1) + seed) & 1U)
                              ? right_distance
                              : -right_distance;
  double fade = visibility_noise_fade(fraction);
  return 2.0 * (left_gradient + (right_gradient - left_gradient) * fade);
}

static int visibility_ray_cast(const visibility_map_t *map, double start_x, double start_y,
                               double angle, visibility_point_t *intersection)
{
  double ray_start_x = start_x / VISIBILITY_TILE_SIZE + 0.5;
  double ray_start_y = start_y / VISIBILITY_TILE_SIZE + 0.5;
  int map_x = (int)floor(ray_start_x);
  int map_y = (int)floor(ray_start_y);
  double dir_x = cos(angle);
  double dir_y = sin(angle);
  double unit_x = fabs(dir_x) < 0.000000001 ? INFINITY : sqrt(1.0 + (dir_y / dir_x) * (dir_y / dir_x));
  double unit_y = fabs(dir_y) < 0.000000001 ? INFINITY : sqrt(1.0 + (dir_x / dir_y) * (dir_x / dir_y));
  int step_x = dir_x < 0.0 ? -1 : 1;
  int step_y = dir_y < 0.0 ? -1 : 1;
  double length_x = dir_x < 0.0 ? (ray_start_x - map_x) * unit_x : (map_x + 1 - ray_start_x) * unit_x;
  double length_y = dir_y < 0.0 ? (ray_start_y - map_y) * unit_y : (map_y + 1 - ray_start_y) * unit_y;
  double distance = 0.0;

  while (distance < 30.0)
  {
    if (length_x < length_y)
    {
      map_x += step_x;
      distance = length_x;
      length_x += unit_x;
    }
    else
    {
      map_y += step_y;
      distance = length_y;
      length_y += unit_y;
    }

    if (visibility_is_blocked(map, map_x, map_y))
    {
      intersection->x = ray_start_x + dir_x * distance;
      intersection->y = ray_start_y + dir_y * distance;
      return 1;
    }
  }
  return 0;
}

static void visibility_polygon_path(const visibility_point_t *polygon, int count)
{
  if (count < 1)
  {
    return;
  }
  nvgMoveTo(visibility_state.vg, visibility_state.convert_x((float)polygon[0].x),
            visibility_state.convert_y((float)polygon[0].y));
  for (int i = 1; i < count; i++)
  {
    nvgLineTo(visibility_state.vg, visibility_state.convert_x((float)polygon[i].x),
              visibility_state.convert_y((float)polygon[i].y));
  }
  nvgClosePath(visibility_state.vg);
}

static void visibility_draw_shadow(const visibility_point_t *polygon, int count)
{
  if (count < 3)
  {
    return;
  }

  const float margin = 100.0f;
  const float layer_opacity = 1.0f - powf(1.0f - VISIBILITY_SHADOW_OPACITY,
                                         1.0f / VISIBILITY_SAMPLE_COUNT);
  float left = visibility_state.translate_x(-500.0f) - margin;
  float right = visibility_state.translate_x(500.0f) + margin;
  float top = visibility_state.translate_y(-500.0f) - margin;
  float bottom = visibility_state.translate_y(500.0f) + margin;

  nvgBeginPath(visibility_state.vg);
  nvgFillColor(visibility_state.vg, nvgRGBAf(0.0f, 0.0f, 0.0f, layer_opacity));
  nvgMoveTo(visibility_state.vg, visibility_state.convert_x(left), visibility_state.convert_y(top));
  nvgLineTo(visibility_state.vg, visibility_state.convert_x(left), visibility_state.convert_y(bottom));
  nvgLineTo(visibility_state.vg, visibility_state.convert_x(right), visibility_state.convert_y(bottom));
  nvgLineTo(visibility_state.vg, visibility_state.convert_x(right), visibility_state.convert_y(top));
  nvgClosePath(visibility_state.vg);
  nvgPathWinding(visibility_state.vg, NVG_SOLID);
  visibility_polygon_path(polygon, count);
  nvgPathWinding(visibility_state.vg, NVG_HOLE);
  nvgFill(visibility_state.vg);
}

static void visibility_draw_falloff(double light_x, double light_y, double strength_noise)
{
  const float margin = 100.0f;
  float radius_scale = (float)(1.0 + strength_noise * VISIBILITY_TORCH_RADIUS_VARIATION);
  float inner_opacity = (float)(VISIBILITY_TORCH_OPACITY_VARIATION * (1.0 - strength_noise));
  float outer_opacity = (float)(VISIBILITY_FALLOFF_OPACITY -
                                strength_noise * VISIBILITY_TORCH_OPACITY_VARIATION);
  inner_opacity = fmaxf(0.0f, fminf(1.0f, inner_opacity));
  outer_opacity = fmaxf(0.0f, fminf(1.0f, outer_opacity));
  float left = visibility_state.translate_x(-500.0f) - margin;
  float right = visibility_state.translate_x(500.0f) + margin;
  float top = visibility_state.translate_y(-500.0f) - margin;
  float bottom = visibility_state.translate_y(500.0f) + margin;

  nvgBeginPath(visibility_state.vg);
  nvgMoveTo(visibility_state.vg, visibility_state.convert_x(left), visibility_state.convert_y(top));
  nvgLineTo(visibility_state.vg, visibility_state.convert_x(left), visibility_state.convert_y(bottom));
  nvgLineTo(visibility_state.vg, visibility_state.convert_x(right), visibility_state.convert_y(bottom));
  nvgLineTo(visibility_state.vg, visibility_state.convert_x(right), visibility_state.convert_y(top));
  nvgClosePath(visibility_state.vg);
  nvgPathWinding(visibility_state.vg, NVG_SOLID);

  NVGpaint gradient = nvgRadialGradient(
      visibility_state.vg,
      visibility_state.convert_x((float)light_x),
      visibility_state.convert_y((float)light_y),
      visibility_state.scale(VISIBILITY_FALLOFF_INNER_RADIUS * radius_scale),
      visibility_state.scale(VISIBILITY_FALLOFF_OUTER_RADIUS * radius_scale),
      nvgRGBAf(0.0f, 0.0f, 0.0f, inner_opacity),
      nvgRGBAf(0.0f, 0.0f, 0.0f, outer_opacity));
  nvgFillPaint(visibility_state.vg, gradient);
  nvgFill(visibility_state.vg);
}

JSValue js_engine_visibility_set_map(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
  int map_id, width, height, floor_tile, wall_tile;
  if (argc < 6 || JS_ToInt32(ctx, &map_id, argv[0]) ||
      JS_ToInt32(ctx, &width, argv[1]) || JS_ToInt32(ctx, &height, argv[2]) ||
      JS_ToInt32(ctx, &floor_tile, argv[4]) || JS_ToInt32(ctx, &wall_tile, argv[5]) || map_id < 0 ||
      map_id >= VISIBILITY_MAX_MAPS || width <= 0 || height <= 0)
  {
    return JS_ThrowRangeError(ctx, "invalid visibility map");
  }

  size_t tile_count = (size_t)width * (size_t)height;
  uint8_t *walls = malloc(tile_count);
  if (!walls)
  {
    return JS_ThrowOutOfMemory(ctx);
  }

  for (size_t i = 0; i < tile_count; i++)
  {
    JSValue value = JS_GetPropertyUint32(ctx, argv[3], (uint32_t)i);
    int tile = 0;
    int valid = !JS_IsUndefined(value) && !JS_IsNull(value) && JS_ToInt32(ctx, &tile, value) == 0;
    JS_FreeValue(ctx, value);
    walls[i] = valid && tile == floor_tile ? 0 : (valid && tile == wall_tile ? 1 : 2);
  }

  free(visibility_maps[map_id].walls);
  visibility_maps[map_id] = (visibility_map_t){width, height, walls};
  return JS_UNDEFINED;
}

JSValue js_engine_visibility_draw(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
  int map_id, max_visible_length;
  double origin_x, origin_y, parent_x, parent_y, parent_scale, light_x, light_y;
  if (argc < 10 || JS_ToInt32(ctx, &map_id, argv[0]) || map_id < 0 ||
      map_id >= VISIBILITY_MAX_MAPS || !visibility_maps[map_id].walls ||
      JS_ToFloat64(ctx, &origin_x, argv[2]) || JS_ToFloat64(ctx, &origin_y, argv[3]) ||
      JS_ToFloat64(ctx, &parent_x, argv[4]) || JS_ToFloat64(ctx, &parent_y, argv[5]) ||
      JS_ToFloat64(ctx, &parent_scale, argv[6]) || JS_ToFloat64(ctx, &light_x, argv[7]) ||
      JS_ToFloat64(ctx, &light_y, argv[8]) || JS_ToInt32(ctx, &max_visible_length, argv[9]) ||
      max_visible_length <= 0)
  {
    return JS_ThrowRangeError(ctx, "invalid visibility draw arguments");
  }

  uint64_t frame = sapp_frame_count();
  if (frame != visibility_state.torch_frame)
  {
    double frame_duration = sapp_frame_duration();
    visibility_state.torch_time += frame_duration > 0.0 && frame_duration < 0.1
                                       ? frame_duration
                                       : 1.0 / 60.0;
    visibility_state.torch_frame = frame;
  }

  double torch_offset_x = visibility_perlin_noise(
                              visibility_state.torch_time * VISIBILITY_TORCH_NOISE_SPEED_X,
                              0x9e3779b9U) *
                          VISIBILITY_TORCH_MOTION_X;
  double torch_offset_y = visibility_perlin_noise(
                              visibility_state.torch_time * VISIBILITY_TORCH_NOISE_SPEED_Y,
                              0x85ebca6bU) *
                          VISIBILITY_TORCH_MOTION_Y;
  origin_x += torch_offset_x;
  origin_y += torch_offset_y;
  light_x += torch_offset_x * parent_scale;
  light_y += torch_offset_y * parent_scale;
  double torch_strength_noise = visibility_perlin_noise(
      visibility_state.torch_time * VISIBILITY_TORCH_STRENGTH_SPEED,
      0xc2b2ae35U);

  visibility_map_t *map = &visibility_maps[map_id];
  size_t tile_count = (size_t)map->width * (size_t)map->height;
  uint8_t *found = calloc(tile_count, 1);
  visibility_tile_t *visible = malloc(tile_count * sizeof(*visible));
  if (!found || !visible)
  {
    free(found);
    free(visible);
    return JS_ThrowOutOfMemory(ctx);
  }

  int origin_tile_x = (int)round(origin_x / VISIBILITY_TILE_SIZE);
  int origin_tile_y = (int)round(origin_y / VISIBILITY_TILE_SIZE);
  int visible_count = 0;
  for (int octant = 0; octant < 8; octant++)
  {
    int next_shadow_valid = 0;
    double next_shadow_start = 0.0;
    double next_shadow_end = 0.0;
    for (int row = 0; row < max_visible_length; row++)
    {
      int shadow_valid = next_shadow_valid;
      double shadow_start = next_shadow_start;
      double shadow_end = next_shadow_end;
      if (shadow_valid && shadow_start == 0.0 && shadow_end == 1.0)
      {
        break;
      }

      for (int col = 0; col <= row; col++)
      {
        int offset_x = 0, offset_y = 0;
        switch (octant)
        {
        case 0: offset_x = col; offset_y = -row; break;
        case 1: offset_x = row; offset_y = -col; break;
        case 2: offset_x = row; offset_y = col; break;
        case 3: offset_x = col; offset_y = row; break;
        case 4: offset_x = -col; offset_y = row; break;
        case 5: offset_x = -row; offset_y = col; break;
        case 6: offset_x = -row; offset_y = -col; break;
        default: offset_x = -col; offset_y = -row; break;
        }

        int x = origin_tile_x + offset_x;
        int y = origin_tile_y + offset_y;
        double top_left = (double)col / (double)(row + 2);
        double bottom_right = (double)(col + 1) / (double)(row + 1);
        int hidden = shadow_valid && top_left >= shadow_start && bottom_right <= shadow_end;
        if (!hidden && x >= 0 && y >= 0 && x < map->width && y < map->height)
        {
          int index = y * map->width + x;
          if (!found[index])
          {
            found[index] = 1;
            visible[visible_count++] = (visibility_tile_t){x, y};
          }
        }

        if (visibility_is_blocked(map, x, y))
        {
          if (!next_shadow_valid)
          {
            next_shadow_valid = 1;
            next_shadow_start = top_left;
            next_shadow_end = bottom_right;
          }
          else if (top_left >= next_shadow_start && bottom_right <= next_shadow_end)
          {
          }
          else if (top_left >= next_shadow_start && bottom_right >= next_shadow_end)
          {
            next_shadow_end = bottom_right;
          }
          else if (top_left <= next_shadow_start && bottom_right <= next_shadow_end)
          {
            next_shadow_start = top_left;
          }
          else
          {
            next_shadow_start = top_left;
            next_shadow_end = bottom_right;
          }
        }
      }
    }
  }

  for (int i = 0; i < visible_count; i++)
  {
    uint32_t index = (uint32_t)(visible[i].y * map->width + visible[i].x);
    JSValue old_value = JS_GetPropertyUint32(ctx, argv[1], index);
    double revealed = 0.0;
    int has_value = !JS_IsUndefined(old_value) && !JS_IsNull(old_value) &&
                    JS_ToFloat64(ctx, &revealed, old_value) == 0 && revealed > 0.0;
    JS_FreeValue(ctx, old_value);
    double new_value = has_value ? fmin(1.0, revealed + 0.04) : 0.0001;
    JS_SetPropertyUint32(ctx, argv[1], index, JS_NewFloat64(ctx, new_value));
  }

  int corner_capacity = visible_count > 0 ? visible_count * 4 : 1;
  visibility_point_t *corners = malloc((size_t)corner_capacity * sizeof(*corners));
  if (!corners)
  {
    free(found);
    free(visible);
    return JS_ThrowOutOfMemory(ctx);
  }

  int corner_count = 0;
  for (int i = 0; i < visible_count; i++)
  {
    int x = visible[i].x;
    int y = visible[i].y;
    if (!visibility_is_corner_wall(map, x, y))
    {
      continue;
    }

    int left = !visibility_is_corner_wall(map, x - 1, y);
    int down = !visibility_is_corner_wall(map, x, y + 1);
    int right = !visibility_is_corner_wall(map, x + 1, y);
    int up = !visibility_is_corner_wall(map, x, y - 1);
    int bottom_right_free = !visibility_is_corner_wall(map, x + 1, y + 1);
    int bottom_left_free = !visibility_is_corner_wall(map, x - 1, y + 1);
    int top_right_free = !visibility_is_corner_wall(map, x + 1, y - 1);
    int top_left_free = !visibility_is_corner_wall(map, x - 1, y - 1);
    int top_left = (left && up) || (left && !top_left_free);
    int top_right = (up && right) || (right && !top_right_free);
    int bottom_right = (right && down) || (right && !bottom_right_free) || (down && !bottom_right_free);
    int bottom_left = (down && left) || (left && !bottom_left_free) || (down && !bottom_left_free);

    if (top_left) corners[corner_count++] = (visibility_point_t){x - 0.5, y - 0.5};
    if (top_right) corners[corner_count++] = (visibility_point_t){x + 0.5, y - 0.5};
    if (bottom_right) corners[corner_count++] = (visibility_point_t){x + 0.5, y + 0.5};
    if (bottom_left) corners[corner_count++] = (visibility_point_t){x - 0.5, y + 0.5};
  }

  int angle_capacity = corner_count > 0 ? corner_count * 3 : 1;
  double *angles = malloc((size_t)angle_capacity * sizeof(*angles));
  visibility_point_t *polygon = malloc((size_t)angle_capacity * sizeof(*polygon));
  if (!angles || !polygon)
  {
    free(found);
    free(visible);
    free(corners);
    free(angles);
    free(polygon);
    return JS_ThrowOutOfMemory(ctx);
  }

  static const double sample_x[VISIBILITY_SAMPLE_COUNT] = {0.0, -8.0, 8.0, 0.0, 0.0};
  static const double sample_y[VISIBILITY_SAMPLE_COUNT] = {0.0, 0.0, 0.0, -8.0, 8.0};

  for (int sample = 0; sample < VISIBILITY_SAMPLE_COUNT; sample++)
  {
    double sample_origin_x = origin_x + sample_x[sample];
    double sample_origin_y = origin_y + sample_y[sample];
    double tile_origin_x = sample_origin_x / VISIBILITY_TILE_SIZE;
    double tile_origin_y = sample_origin_y / VISIBILITY_TILE_SIZE;
    int angle_count = 0;

    for (int i = 0; i < corner_count; i++)
    {
      double angle = atan2(corners[i].y - tile_origin_y, corners[i].x - tile_origin_x);
      angles[angle_count++] = visibility_normalize_angle(angle - VISIBILITY_RAY_EPSILON);
      angles[angle_count++] = visibility_normalize_angle(angle);
      angles[angle_count++] = visibility_normalize_angle(angle + VISIBILITY_RAY_EPSILON);
    }
    qsort(angles, (size_t)angle_count, sizeof(*angles), visibility_angle_compare);

    int polygon_count = 0;
    double previous_angle = 0.0;
    int has_previous_angle = 0;
    for (int i = 0; i < angle_count; i++)
    {
      if (has_previous_angle && fabs(angles[i] - previous_angle) <= 0.000001)
      {
        continue;
      }
      previous_angle = angles[i];
      has_previous_angle = 1;

      visibility_point_t hit;
      if (!visibility_ray_cast(map, sample_origin_x, sample_origin_y, angles[i], &hit))
      {
        continue;
      }
      visibility_point_t screen_point = {
          (hit.x * VISIBILITY_TILE_SIZE - 16.0) * parent_scale + parent_x,
          (hit.y * VISIBILITY_TILE_SIZE - 16.0) * parent_scale + parent_y};
      if (!isfinite(screen_point.x) || !isfinite(screen_point.y))
      {
        continue;
      }
      if (polygon_count == 0 || fabs(polygon[polygon_count - 1].x - screen_point.x) > 0.001 ||
          fabs(polygon[polygon_count - 1].y - screen_point.y) > 0.001)
      {
        polygon[polygon_count++] = screen_point;
      }
    }

    visibility_draw_shadow(polygon, polygon_count);
  }

  visibility_draw_falloff(light_x, light_y, torch_strength_noise);
  free(found);
  free(visible);
  free(corners);
  free(angles);
  free(polygon);
  return JS_UNDEFINED;
}

