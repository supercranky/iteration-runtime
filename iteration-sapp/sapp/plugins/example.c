#include "iteration_plugin.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(ITERATION_NATIVE_PLUGINS)
#define iteration_abi_version iteration_example_abi_version
#define iteration_manifest_ptr iteration_example_manifest_ptr
#define iteration_manifest_len iteration_example_manifest_len
#define iteration_alloc iteration_example_alloc
#define iteration_free iteration_example_free
#define iteration_call iteration_example_call
#endif

static const char manifest[] =
    "{\"abi\":\"iteration.plugin/1\",\"name\":\"example\",\"exports\":{"
    "\"drawRect\":{\"id\":0},\"add\":{\"id\":1}}}";
static uint8_t render_bytes[1024];
static uint8_t value_bytes[16];
static iteration_plugin_result result;

static uint32_t read_u32(const uint8_t *bytes)
{
  uint32_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

static int read_number(const uint8_t *input, uint32_t input_len,
                       uint32_t wanted, double *output)
{
  if (input_len < 4 || read_u32(input) <= wanted)
    return 0;
  uint32_t offset = 4;
  for (uint32_t index = 0; index <= wanted; index++)
  {
    if (offset > input_len - 8)
      return 0;
    uint32_t tag = read_u32(input + offset);
    uint32_t length = read_u32(input + offset + 4);
    offset += 8;
    if (length > input_len - offset)
      return 0;
    if (index == wanted)
    {
      if (tag != 3 || length != 8)
        return 0;
      memcpy(output, input + offset, 8);
      return 1;
    }
    offset += length;
  }
  return 0;
}

uint32_t iteration_abi_version(void)
{
  return ITERATION_PLUGIN_ABI_VERSION;
}

iteration_plugin_ptr iteration_manifest_ptr(void)
{
  return (iteration_plugin_ptr)(uintptr_t)manifest;
}

uint32_t iteration_manifest_len(void)
{
  return (uint32_t)(sizeof(manifest) - 1);
}

iteration_plugin_ptr iteration_alloc(uint32_t size, uint32_t alignment)
{
  (void)alignment;
  return (iteration_plugin_ptr)(uintptr_t)malloc(size);
}

void iteration_free(iteration_plugin_ptr ptr, uint32_t size, uint32_t alignment)
{
  (void)size;
  (void)alignment;
  free((void *)(uintptr_t)ptr);
}

iteration_plugin_ptr iteration_call(uint32_t method, iteration_plugin_ptr input_ptr, uint32_t input_len)
{
  const uint8_t *input = (const uint8_t *)(uintptr_t)input_ptr;
  memset(&result, 0, sizeof(result));
  value_bytes[0] = 0;
  memset(value_bytes + 1, 0, 7);
  result.value_ptr = (iteration_plugin_ptr)(uintptr_t)value_bytes;
  result.value_len = 8;

  if (method == 0)
  {
    double values[8] = {0};
    for (uint32_t i = 0; i < 8; i++)
    {
      if (!read_number(input, input_len, i, &values[i]))
      {
        result.status = -1;
        return (iteration_plugin_ptr)(uintptr_t)&result;
      }
    }
    iteration_graphics_writer graphics;
    iteration_graphics_init(&graphics, render_bytes, sizeof(render_bytes));
    graphicsBeginPath(&graphics);
    graphicsRect(&graphics, (float)values[0], (float)values[1],
                 (float)values[2], (float)values[3]);
    graphicsFillColor(&graphics, (float)values[4], (float)values[5],
                      (float)values[6], (float)values[7]);
    graphicsFill(&graphics);
    result.render_len = iteration_graphics_finish(&graphics);
    result.render_ptr = (iteration_plugin_ptr)(uintptr_t)render_bytes;
    result.status = result.render_len ? 0 : -2;
  }
  else if (method == 1)
  {
    double left, right, sum;
    if (!read_number(input, input_len, 0, &left) ||
        !read_number(input, input_len, 1, &right))
    {
      result.status = -1;
      return (iteration_plugin_ptr)(uintptr_t)&result;
    }
    sum = left + right;
    uint32_t tag = 3, length = 8;
    memcpy(value_bytes, &tag, 4);
    memcpy(value_bytes + 4, &length, 4);
    memcpy(value_bytes + 8, &sum, 8);
    result.value_len = 16;
  }
  else
  {
    result.status = -3;
  }
  return (iteration_plugin_ptr)(uintptr_t)&result;
}
