#include "defines.h"

#ifdef EGNPLATFORM_WEB
#include "platform/platform.h"
#include "core/logger.h"

#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

b8 platform_initialize(u64 *out_memory_requirement, void *state)
{
  if (!state)
  {
    *out_memory_requirement = 1;
    return false;
  }

  EGNINFO("Web platform initialized");
  return true;
}

void platform_shutdown() {}

void *platform_allocate(u64 size, b8 aligned)
{
  (void)aligned;
  return malloc((size_t)size);
}

void platform_free(void *block, b8 aligned)
{
  (void)aligned;
  free(block);
}

void *platform_zero_memory(void *block, u64 size)
{
  return memset(block, 0, (size_t)size);
}

void *platform_copy_memory(void *dest, const void *source, u64 size)
{
  return memcpy(dest, source, (size_t)size);
}

void *platform_set_memory(void *dest, i32 value, u64 size)
{
  return memset(dest, value, (size_t)size);
}

void platform_console_write(const char *message, u8 colour)
{
  (void)colour;
  fputs(message, stdout);
  fflush(stdout);
}

void platform_console_write_error(const char *message, u8 colour)
{
  (void)colour;
  fputs(message, stderr);
  fflush(stderr);
}

f64 platform_get_absolute_time()
{
  return emscripten_get_now() / 1000.0;
}

void platform_sleep(u64 ms)
{
  (void)ms;
}

void platform_sleep_us(u64 us)
{
  (void)us;
}

#endif
