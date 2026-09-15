#include "iteration_plugin.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(ITERATION_NATIVE_PLUGINS)
#define iteration_abi_version iteration_visibility_abi_version
#define iteration_manifest_ptr iteration_visibility_manifest_ptr
#define iteration_manifest_len iteration_visibility_manifest_len
#define iteration_alloc iteration_visibility_alloc
#define iteration_free iteration_visibility_free
#define iteration_call iteration_visibility_call
#endif

#define MAX_MAPS 256
#define TILE_SIZE 32.0
#define RAY_EPSILON 0.002
#define SHADOW_OPACITY 0.60f
#define FALLOFF_OPACITY 0.9f
#define FALLOFF_INNER_RADIUS 100.0f
#define FALLOFF_OUTER_RADIUS 600.0f
#define TORCH_MOTION_X 1.5
#define TORCH_MOTION_Y 1.0
#define TORCH_NOISE_SPEED_X 5.0
#define TORCH_NOISE_SPEED_Y 4.3
#define TORCH_STRENGTH_SPEED 6.2
#define TORCH_RADIUS_VARIATION 0.05
#define TORCH_OPACITY_VARIATION 0.025
#define SAMPLE_COUNT 5
#define RENDER_CAPACITY (2u * 1024u * 1024u)

typedef struct { int width, height; uint8_t *walls; double *revealed; } map_t;
typedef struct { double x, y; } point_t;
typedef struct { int x, y; } tile_t;

static const char manifest[] =
    "{\"abi\":\"iteration.plugin/1\",\"name\":\"visibility\",\"exports\":{"
    "\"setMap\":{\"id\":0},\"draw\":{\"id\":1}}}";
static map_t maps[MAX_MAPS];
static double torch_time;
static uint64_t torch_frame = UINT64_MAX;
static iteration_plugin_result result;
static uint8_t *render_bytes;
static uint8_t *value_bytes;
static uint32_t value_capacity;

static uint32_t read_u32(const uint8_t *bytes) { uint32_t v; memcpy(&v, bytes, 4); return v; }
static int argument(const uint8_t *input, uint32_t input_len, uint32_t wanted,
                    uint32_t *tag, const uint8_t **bytes, uint32_t *length)
{
  if (input_len < 4 || read_u32(input) <= wanted) return 0;
  uint32_t offset = 4;
  for (uint32_t i = 0; i <= wanted; i++) {
    if (offset > input_len - 8) return 0;
    uint32_t current_tag = read_u32(input + offset);
    uint32_t current_len = read_u32(input + offset + 4);
    offset += 8;
    if (current_len > input_len - offset) return 0;
    if (i == wanted) {
      *tag = current_tag; *bytes = input + offset; *length = current_len; return 1;
    }
    offset += current_len;
  }
  return 0;
}
static int number_arg(const uint8_t *input, uint32_t len, uint32_t index, double *out)
{
  uint32_t tag, size; const uint8_t *bytes;
  if (!argument(input, len, index, &tag, &bytes, &size) || tag != 3 || size != 8) return 0;
  memcpy(out, bytes, 8); return isfinite(*out);
}
static int array_arg(const uint8_t *input, uint32_t len, uint32_t index,
                     const uint8_t **bytes, uint32_t *count)
{
  uint32_t tag, size;
  if (!argument(input, len, index, &tag, bytes, &size) || tag != 6 || size % 8) return 0;
  *count = size / 8; return 1;
}
static double array_number(const uint8_t *bytes, uint32_t index)
{
  double value; memcpy(&value, bytes + index * 8, 8); return value;
}

static int blocked(const map_t *map, int x, int y)
{
  return !map || !map->walls || x < 0 || y < 0 || x >= map->width || y >= map->height ||
         map->walls[y * map->width + x] != 0;
}
static int corner_wall(const map_t *map, int x, int y)
{
  return !map || !map->walls || x < 0 || y < 0 || x >= map->width || y >= map->height ||
         map->walls[y * map->width + x] == 1;
}
static int angle_compare(const void *left, const void *right)
{
  double a = *(const double *)left, b = *(const double *)right;
  return a < b ? 1 : (a > b ? -1 : 0);
}
static double normalize_angle(double angle) { return atan2(sin(angle), cos(angle)); }
static uint32_t noise_hash(uint32_t value)
{
  value ^= value >> 16; value *= 0x7feb352dU; value ^= value >> 15;
  value *= 0x846ca68bU; value ^= value >> 16; return value;
}
static double noise_fade(double v) { return v * v * v * (v * (v * 6.0 - 15.0) + 10.0); }
static double perlin_noise(double position, uint32_t seed)
{
  int lattice = (int)floor(position); double fraction = position - lattice;
  double left = (noise_hash((uint32_t)lattice + seed) & 1U) ? fraction : -fraction;
  double distance = fraction - 1.0;
  double right = (noise_hash((uint32_t)(lattice + 1) + seed) & 1U) ? distance : -distance;
  double fade = noise_fade(fraction); return 2.0 * (left + (right - left) * fade);
}
static int ray_cast(const map_t *map, double start_x, double start_y, double angle, point_t *hit)
{
  double sx = start_x / TILE_SIZE + 0.5, sy = start_y / TILE_SIZE + 0.5;
  int mx = (int)floor(sx), my = (int)floor(sy);
  double dx = cos(angle), dy = sin(angle);
  double ux = fabs(dx) < 1e-9 ? INFINITY : sqrt(1.0 + (dy / dx) * (dy / dx));
  double uy = fabs(dy) < 1e-9 ? INFINITY : sqrt(1.0 + (dx / dy) * (dx / dy));
  int stepx = dx < 0 ? -1 : 1, stepy = dy < 0 ? -1 : 1;
  double lx = dx < 0 ? (sx - mx) * ux : (mx + 1 - sx) * ux;
  double ly = dy < 0 ? (sy - my) * uy : (my + 1 - sy) * uy;
  double distance = 0;
  while (distance < 30.0) {
    if (lx < ly) { mx += stepx; distance = lx; lx += ux; }
    else { my += stepy; distance = ly; ly += uy; }
    if (blocked(map, mx, my)) { hit->x = sx + dx * distance; hit->y = sy + dy * distance; return 1; }
  }
  return 0;
}
static void polygon_path(iteration_graphics_writer *graphics, const point_t *polygon, int count)
{
  if (count < 1) return;
  graphicsMoveTo(graphics, (float)polygon[0].x, (float)polygon[0].y);
  for (int i = 1; i < count; i++) graphicsLineTo(graphics, (float)polygon[i].x, (float)polygon[i].y);
  graphicsClosePath(graphics);
}
static void draw_shadow(iteration_graphics_writer *graphics, const point_t *polygon, int count,
                        float left, float right, float top, float bottom)
{
  if (count < 3) return;
  float alpha = 1.0f - powf(1.0f - SHADOW_OPACITY, 1.0f / SAMPLE_COUNT);
  graphicsBeginPath(graphics); graphicsFillColor(graphics, 0, 0, 0, alpha);
  graphicsMoveTo(graphics, left, top); graphicsLineTo(graphics, left, bottom);
  graphicsLineTo(graphics, right, bottom); graphicsLineTo(graphics, right, top);
  graphicsClosePath(graphics); graphicsSolid(graphics); polygon_path(graphics, polygon, count);
  graphicsHole(graphics); graphicsFill(graphics);
}
static void draw_falloff(iteration_graphics_writer *graphics, double x, double y, double noise,
                         float left, float right, float top, float bottom)
{
  float radius_scale = (float)(1.0 + noise * TORCH_RADIUS_VARIATION);
  float inner = (float)(TORCH_OPACITY_VARIATION * (1.0 - noise));
  float outer = (float)(FALLOFF_OPACITY - noise * TORCH_OPACITY_VARIATION);
  inner = fmaxf(0, fminf(1, inner)); outer = fmaxf(0, fminf(1, outer));
  graphicsBeginPath(graphics); graphicsMoveTo(graphics, left, top);
  graphicsLineTo(graphics, left, bottom); graphicsLineTo(graphics, right, bottom);
  graphicsLineTo(graphics, right, top); graphicsClosePath(graphics); graphicsSolid(graphics);
  graphicsRadialGradient(graphics, (float)x, (float)y,
      FALLOFF_INNER_RADIUS * radius_scale, FALLOFF_OUTER_RADIUS * radius_scale,
      0, 0, 0, inner, 0, 0, 0, outer); graphicsFill(graphics);
}

static int set_map(const uint8_t *input, uint32_t input_len)
{
  double numbers[5];
  for (uint32_t i = 0; i < 3; i++) if (!number_arg(input, input_len, i, &numbers[i])) return -1;
  if (!number_arg(input, input_len, 4, &numbers[3]) || !number_arg(input, input_len, 5, &numbers[4])) return -1;
  int id = (int)numbers[0], width = (int)numbers[1], height = (int)numbers[2];
  if (id < 0 || id >= MAX_MAPS || width <= 0 || height <= 0 ||
      (size_t)width > SIZE_MAX / (size_t)height) return -1;
  size_t count = (size_t)width * (size_t)height;
  const uint8_t *tiles; uint32_t tile_count;
  if (!array_arg(input, input_len, 3, &tiles, &tile_count) || tile_count < count) return -1;
  uint8_t *walls = malloc(count); double *revealed = calloc(count, sizeof(double));
  if (!walls || !revealed) { free(walls); free(revealed); return -2; }
  int floor_tile = (int)numbers[3], wall_tile = (int)numbers[4];
  for (size_t i = 0; i < count; i++) {
    int tile = (int)array_number(tiles, (uint32_t)i);
    walls[i] = tile == floor_tile ? 0 : (tile == wall_tile ? 1 : 2);
  }
  free(maps[id].walls); free(maps[id].revealed);
  maps[id] = (map_t){width, height, walls, revealed}; return 0;
}

static int draw(const uint8_t *input, uint32_t input_len)
{
  double a[15]; for (uint32_t i = 0; i < 15; i++) if (!number_arg(input, input_len, i, &a[i])) return -1;
  int id = (int)a[0], max_length = (int)a[8];
  if (id < 0 || id >= MAX_MAPS || !maps[id].walls || max_length <= 0) return -1;
  map_t *map = &maps[id]; size_t tile_count = (size_t)map->width * (size_t)map->height;
  double dt = a[9]; uint64_t frame = (uint64_t)a[10];
  if (frame != torch_frame) {
    torch_time += dt > 0 && dt < 0.1 ? dt : 1.0 / 60.0;
    torch_frame = frame;
  }
  double ox = perlin_noise(torch_time * TORCH_NOISE_SPEED_X, 0x9e3779b9U) * TORCH_MOTION_X;
  double oy = perlin_noise(torch_time * TORCH_NOISE_SPEED_Y, 0x85ebca6bU) * TORCH_MOTION_Y;
  double origin_x = a[1] + ox, origin_y = a[2] + oy, parent_x = a[3], parent_y = a[4], scale = a[5];
  double light_x = a[6] + ox * scale, light_y = a[7] + oy * scale;
  double strength = perlin_noise(torch_time * TORCH_STRENGTH_SPEED, 0xc2b2ae35U);
  uint8_t *found = calloc(tile_count, 1); tile_t *visible = malloc(tile_count * sizeof(*visible));
  if (!found || !visible) { free(found); free(visible); return -2; }
  int tx = (int)round(origin_x / TILE_SIZE), ty = (int)round(origin_y / TILE_SIZE), visible_count = 0;
  for (int octant = 0; octant < 8; octant++) {
    int next_valid = 0; double next_start = 0, next_end = 0;
    for (int row = 0; row < max_length; row++) {
      int shadow_valid = next_valid; double shadow_start = next_start, shadow_end = next_end;
      if (shadow_valid && shadow_start == 0 && shadow_end == 1) break;
      for (int col = 0; col <= row; col++) {
        int dx = 0, dy = 0;
        switch (octant) {
          case 0: dx=col;dy=-row;break; case 1: dx=row;dy=-col;break;
          case 2: dx=row;dy=col;break; case 3: dx=col;dy=row;break;
          case 4: dx=-col;dy=row;break; case 5: dx=-row;dy=col;break;
          case 6: dx=-row;dy=-col;break; default: dx=-col;dy=-row;break;
        }
        int x = tx + dx, y = ty + dy; double start = (double)col/(row+2), end=(double)(col+1)/(row+1);
        int hidden = shadow_valid && start >= shadow_start && end <= shadow_end;
        if (!hidden && x >= 0 && y >= 0 && x < map->width && y < map->height) {
          int index = y * map->width + x; if (!found[index]) { found[index]=1; visible[visible_count++]=(tile_t){x,y}; }
        }
        if (blocked(map,x,y)) {
          if (!next_valid) { next_valid=1;next_start=start;next_end=end; }
          else if (!(start >= next_start && end <= next_end)) {
            if (start >= next_start && end >= next_end) next_end=end;
            else if (start <= next_start && end <= next_end) next_start=start;
            else { next_start=start;next_end=end; }
          }
        }
      }
    }
  }
  size_t value_size = 8 + (size_t)visible_count * 16;
  if (value_size > value_capacity) {
    uint8_t *next = realloc(value_bytes, value_size); if (!next) { free(found);free(visible);return -2; }
    value_bytes=next;value_capacity=(uint32_t)value_size;
  }
  uint32_t tag=6, payload=(uint32_t)(value_size-8); memcpy(value_bytes,&tag,4);memcpy(value_bytes+4,&payload,4);
  for (int i=0;i<visible_count;i++) {
    uint32_t index=(uint32_t)(visible[i].y*map->width+visible[i].x);
    double old=map->revealed[index], updated=old>0?fmin(1.0,old+0.04):0.0001; map->revealed[index]=updated;
    double index_number=(double)index; memcpy(value_bytes+8+i*16,&index_number,8);memcpy(value_bytes+16+i*16,&updated,8);
  }
  int corner_capacity=visible_count>0?visible_count*4:1, corner_count=0;
  point_t *corners=malloc((size_t)corner_capacity*sizeof(*corners));
  if (!corners) { free(found);free(visible);return -2; }
  for(int i=0;i<visible_count;i++) {
    int x=visible[i].x,y=visible[i].y;if(!corner_wall(map,x,y))continue;
    int left=!corner_wall(map,x-1,y),down=!corner_wall(map,x,y+1),right=!corner_wall(map,x+1,y),up=!corner_wall(map,x,y-1);
    int br=!corner_wall(map,x+1,y+1),bl=!corner_wall(map,x-1,y+1),tr=!corner_wall(map,x+1,y-1),tl=!corner_wall(map,x-1,y-1);
    if((left&&up)||(left&&!tl))corners[corner_count++]=(point_t){x-.5,y-.5};
    if((up&&right)||(right&&!tr))corners[corner_count++]=(point_t){x+.5,y-.5};
    if((right&&down)||(right&&!br)||(down&&!br))corners[corner_count++]=(point_t){x+.5,y+.5};
    if((down&&left)||(left&&!bl)||(down&&!bl))corners[corner_count++]=(point_t){x-.5,y+.5};
  }
  int capacity=corner_count>0?corner_count*3:1; double *angles=malloc((size_t)capacity*sizeof(*angles));
  point_t *polygon=malloc((size_t)capacity*sizeof(*polygon));
  if(!angles||!polygon){free(found);free(visible);free(corners);free(angles);free(polygon);return -2;}
  if(!render_bytes)render_bytes=malloc(RENDER_CAPACITY); if(!render_bytes){free(found);free(visible);free(corners);free(angles);free(polygon);return -2;}
  iteration_graphics_writer graphics; iteration_graphics_init(&graphics,render_bytes,RENDER_CAPACITY);
  static const double sx[SAMPLE_COUNT]={0,-8,8,0,0},sy[SAMPLE_COUNT]={0,0,0,-8,8};
  float left=(float)a[11]-100,right=(float)a[12]+100,top=(float)a[13]-100,bottom=(float)a[14]+100;
  for(int sample=0;sample<SAMPLE_COUNT;sample++) {
    double sox=origin_x+sx[sample],soy=origin_y+sy[sample],tox=sox/TILE_SIZE,toy=soy/TILE_SIZE;int angle_count=0;
    for(int i=0;i<corner_count;i++){double angle=atan2(corners[i].y-toy,corners[i].x-tox);angles[angle_count++]=normalize_angle(angle-RAY_EPSILON);angles[angle_count++]=normalize_angle(angle);angles[angle_count++]=normalize_angle(angle+RAY_EPSILON);}
    qsort(angles,(size_t)angle_count,sizeof(*angles),angle_compare);int polygon_count=0;double previous=0;int has_previous=0;
    for(int i=0;i<angle_count;i++){if(has_previous&&fabs(angles[i]-previous)<=1e-6)continue;previous=angles[i];has_previous=1;point_t hit;if(!ray_cast(map,sox,soy,angles[i],&hit))continue;point_t screen={(hit.x*TILE_SIZE-16)*scale+parent_x,(hit.y*TILE_SIZE-16)*scale+parent_y};if(!isfinite(screen.x)||!isfinite(screen.y))continue;if(!polygon_count||fabs(polygon[polygon_count-1].x-screen.x)>.001||fabs(polygon[polygon_count-1].y-screen.y)>.001)polygon[polygon_count++]=screen;}
    draw_shadow(&graphics,polygon,polygon_count,left,right,top,bottom);
  }
  draw_falloff(&graphics,light_x,light_y,strength,left,right,top,bottom);
  free(found);free(visible);free(corners);free(angles);free(polygon);
  if(graphics.error)return -3; result.value_ptr=(iteration_plugin_ptr)(uintptr_t)value_bytes;result.value_len=(uint32_t)value_size;
  result.render_ptr=(iteration_plugin_ptr)(uintptr_t)render_bytes;result.render_len=iteration_graphics_finish(&graphics);return 0;
}

uint32_t iteration_abi_version(void){return ITERATION_PLUGIN_ABI_VERSION;}
iteration_plugin_ptr iteration_manifest_ptr(void){return (iteration_plugin_ptr)(uintptr_t)manifest;}
uint32_t iteration_manifest_len(void){return (uint32_t)(sizeof(manifest)-1);}
iteration_plugin_ptr iteration_alloc(uint32_t size,uint32_t alignment){(void)alignment;return(iteration_plugin_ptr)(uintptr_t)malloc(size);}
void iteration_free(iteration_plugin_ptr ptr,uint32_t size,uint32_t alignment){(void)size;(void)alignment;free((void *)(uintptr_t)ptr);}
iteration_plugin_ptr iteration_call(uint32_t method,iteration_plugin_ptr input_ptr,uint32_t input_len)
{
  memset(&result,0,sizeof(result));static uint8_t undefined_value[8];result.value_ptr=(iteration_plugin_ptr)(uintptr_t)undefined_value;result.value_len=8;
  int status=method==0?set_map((const uint8_t *)(uintptr_t)input_ptr,input_len):method==1?draw((const uint8_t *)(uintptr_t)input_ptr,input_len):-1;
  result.status=status;return(iteration_plugin_ptr)(uintptr_t)&result;
}
