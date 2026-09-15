#pragma once

/* Stable, renderer-independent ABI shared by WebAssembly and native plugins. */
#include <stddef.h>
#include <stdint.h>

#define ITERATION_PLUGIN_ABI_VERSION 1u
#define ITERATION_RENDER_MAGIC 0x42524349u /* "ICRB", little endian */

#if defined(ITERATION_NATIVE_PLUGINS)
typedef uintptr_t iteration_plugin_ptr;
#else
typedef uint32_t iteration_plugin_ptr;
#endif

typedef struct iteration_frame_context
{
  uint64_t frame_index;
  double frame_duration;
  float viewport_left;
  float viewport_right;
  float viewport_top;
  float viewport_bottom;
} iteration_frame_context;

typedef enum iteration_render_opcode
{
  ITER_RENDER_BEGIN_PATH = 1,
  ITER_RENDER_CLOSE_PATH = 2,
  ITER_RENDER_MOVE_TO = 3,
  ITER_RENDER_LINE_TO = 4,
  ITER_RENDER_RECT = 5,
  ITER_RENDER_ROUNDED_RECT = 6,
  ITER_RENDER_FILL_COLOR = 7,
  ITER_RENDER_PATH_SOLID = 8,
  ITER_RENDER_PATH_HOLE = 9,
  ITER_RENDER_RADIAL_GRADIENT = 10,
  ITER_RENDER_FILL = 11,
  ITER_RENDER_STROKE_COLOR = 12,
  ITER_RENDER_STROKE_WIDTH = 13,
  ITER_RENDER_STROKE = 14
} iteration_render_opcode;

typedef struct iteration_render_buffer
{
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t byte_length;
  uint32_t command_count;
} iteration_render_buffer;

typedef struct iteration_render_command
{
  uint16_t opcode;
  uint16_t byte_size;
} iteration_render_command;

typedef struct iteration_graphics_writer
{
  uint8_t *bytes;
  uint32_t capacity;
  uint32_t length;
  uint32_t command_count;
  int32_t error;
} iteration_graphics_writer;

typedef struct iteration_xy_command
{
  iteration_render_command header;
  float x;
  float y;
} iteration_xy_command;

typedef struct iteration_rect_command
{
  iteration_render_command header;
  float x;
  float y;
  float width;
  float height;
  float radius;
} iteration_rect_command;

typedef struct iteration_color_command
{
  iteration_render_command header;
  float red;
  float green;
  float blue;
  float alpha;
} iteration_color_command;

typedef struct iteration_scalar_command
{
  iteration_render_command header;
  float value;
} iteration_scalar_command;

typedef struct iteration_radial_gradient_command
{
  iteration_render_command header;
  float center_x;
  float center_y;
  float inner_radius;
  float outer_radius;
  float inner_red;
  float inner_green;
  float inner_blue;
  float inner_alpha;
  float outer_red;
  float outer_green;
  float outer_blue;
  float outer_alpha;
} iteration_radial_gradient_command;

typedef struct iteration_plugin_result
{
  int32_t status;
  iteration_plugin_ptr value_ptr;
  uint32_t value_len;
  iteration_plugin_ptr render_ptr;
  uint32_t render_len;
} iteration_plugin_result;

static inline void iteration_graphics_init(iteration_graphics_writer *writer,
                                           void *buffer, uint32_t capacity)
{
  writer->bytes = (uint8_t *)buffer;
  writer->capacity = capacity;
  writer->length = sizeof(iteration_render_buffer);
  writer->command_count = 0;
  writer->error = capacity < sizeof(iteration_render_buffer) ? -1 : 0;
}

static inline void *iteration_graphics_push(iteration_graphics_writer *writer,
                                            uint16_t opcode, uint16_t size)
{
  if (writer->error || size < sizeof(iteration_render_command) ||
      size > writer->capacity || writer->length > writer->capacity - size)
  {
    writer->error = -1;
    return NULL;
  }
  iteration_render_command *command =
      (iteration_render_command *)(writer->bytes + writer->length);
  command->opcode = opcode;
  command->byte_size = size;
  writer->length += size;
  writer->command_count++;
  return command;
}

static inline uint32_t iteration_graphics_finish(iteration_graphics_writer *writer)
{
  if (writer->error)
    return 0;
  iteration_render_buffer *buffer = (iteration_render_buffer *)writer->bytes;
  buffer->magic = ITERATION_RENDER_MAGIC;
  buffer->version = ITERATION_PLUGIN_ABI_VERSION;
  buffer->reserved = 0;
  buffer->byte_length = writer->length;
  buffer->command_count = writer->command_count;
  return writer->length;
}

static inline void graphicsCommand(iteration_graphics_writer *writer, uint16_t opcode)
{
  iteration_graphics_push(writer, opcode, sizeof(iteration_render_command));
}

static inline void graphicsBeginPath(iteration_graphics_writer *writer)
{
  graphicsCommand(writer, ITER_RENDER_BEGIN_PATH);
}

static inline void graphicsClosePath(iteration_graphics_writer *writer)
{
  graphicsCommand(writer, ITER_RENDER_CLOSE_PATH);
}

static inline void graphicsMoveTo(iteration_graphics_writer *writer, float x, float y)
{
  iteration_xy_command *command = (iteration_xy_command *)iteration_graphics_push(
      writer, ITER_RENDER_MOVE_TO, sizeof(iteration_xy_command));
  if (command) { command->x = x; command->y = y; }
}

static inline void graphicsLineTo(iteration_graphics_writer *writer, float x, float y)
{
  iteration_xy_command *command = (iteration_xy_command *)iteration_graphics_push(
      writer, ITER_RENDER_LINE_TO, sizeof(iteration_xy_command));
  if (command) { command->x = x; command->y = y; }
}

static inline void graphicsRect(iteration_graphics_writer *writer, float x, float y,
                                float width, float height)
{
  iteration_rect_command *command = (iteration_rect_command *)iteration_graphics_push(
      writer, ITER_RENDER_RECT, sizeof(iteration_rect_command));
  if (command) {
    command->x = x; command->y = y; command->width = width;
    command->height = height; command->radius = 0.0f;
  }
}

static inline void graphicsRoundedRect(iteration_graphics_writer *writer, float x, float y,
                                       float width, float height, float radius)
{
  iteration_rect_command *command = (iteration_rect_command *)iteration_graphics_push(
      writer, ITER_RENDER_ROUNDED_RECT, sizeof(iteration_rect_command));
  if (command) {
    command->x = x; command->y = y; command->width = width;
    command->height = height; command->radius = radius;
  }
}

static inline void graphicsFillColor(iteration_graphics_writer *writer,
                                     float red, float green, float blue, float alpha)
{
  iteration_color_command *command = (iteration_color_command *)iteration_graphics_push(
      writer, ITER_RENDER_FILL_COLOR, sizeof(iteration_color_command));
  if (command) {
    command->red = red; command->green = green;
    command->blue = blue; command->alpha = alpha;
  }
}

static inline void graphicsSolid(iteration_graphics_writer *writer)
{
  graphicsCommand(writer, ITER_RENDER_PATH_SOLID);
}

static inline void graphicsHole(iteration_graphics_writer *writer)
{
  graphicsCommand(writer, ITER_RENDER_PATH_HOLE);
}

static inline void graphicsFill(iteration_graphics_writer *writer)
{
  graphicsCommand(writer, ITER_RENDER_FILL);
}

static inline void graphicsStroke(iteration_graphics_writer *writer)
{
  graphicsCommand(writer, ITER_RENDER_STROKE);
}

static inline void graphicsStrokeWidth(iteration_graphics_writer *writer, float width)
{
  iteration_scalar_command *command = (iteration_scalar_command *)iteration_graphics_push(
      writer, ITER_RENDER_STROKE_WIDTH, sizeof(iteration_scalar_command));
  if (command) command->value = width;
}

static inline void graphicsStrokeColor(iteration_graphics_writer *writer,
                                       float red, float green, float blue, float alpha)
{
  iteration_color_command *command = (iteration_color_command *)iteration_graphics_push(
      writer, ITER_RENDER_STROKE_COLOR, sizeof(iteration_color_command));
  if (command) {
    command->red = red; command->green = green;
    command->blue = blue; command->alpha = alpha;
  }
}

static inline void graphicsRadialGradient(iteration_graphics_writer *writer,
                                          float center_x, float center_y,
                                          float inner_radius, float outer_radius,
                                          float ir, float ig, float ib, float ia,
                                          float or_, float og, float ob, float oa)
{
  iteration_radial_gradient_command *command =
      (iteration_radial_gradient_command *)iteration_graphics_push(
          writer, ITER_RENDER_RADIAL_GRADIENT,
          sizeof(iteration_radial_gradient_command));
  if (command) {
    command->center_x = center_x; command->center_y = center_y;
    command->inner_radius = inner_radius; command->outer_radius = outer_radius;
    command->inner_red = ir; command->inner_green = ig;
    command->inner_blue = ib; command->inner_alpha = ia;
    command->outer_red = or_; command->outer_green = og;
    command->outer_blue = ob; command->outer_alpha = oa;
  }
}
