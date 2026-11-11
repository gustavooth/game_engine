#include "platform/platform.h"
#include "defines.h"
#include "core/logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#ifdef EGNPLATFORM_WAYLAND

typedef struct PlatformLinuxState
{
  struct timespec start_time;
} PlatformLinuxState;

static PlatformLinuxState *platform_ptr;

b8 platform_initialize(u64 *out_memory_requirement, void *state)
{
  if (!state)
  {
    *out_memory_requirement = sizeof(PlatformLinuxState);
    return false;
  }
  platform_ptr = (PlatformLinuxState *)state;

  // Monotonic clock: unlike the wall clock it never jumps when the system
  // time is adjusted (NTP, suspend, manual change), which would otherwise
  // produce negative or huge frame deltas.
  if (clock_gettime(CLOCK_MONOTONIC, &platform_ptr->start_time) != 0)
  {
    EGNERROR("platform_initialize: failed to initialize clock");
    return false;
  }

  EGNINFO("Core Linux Platform initialized!");
  return true;
}

void platform_shutdown()
{
}

void *platform_allocate(u64 size, b8 aligned)
{
  return malloc(size);
}

void platform_free(void *block, b8 aligned)
{
  free(block);
}

void *platform_zero_memory(void *block, u64 size)
{
  return memset(block, 0, size);
}

void *platform_copy_memory(void *dest, const void *source, u64 size)
{
  return memcpy(dest, source, size);
}

void *platform_set_memory(void *dest, i32 value, u64 size)
{
  return memset(dest, value, size);
}

void platform_console_write(const char *message, u8 colour)
{
  // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
  const char *colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
  printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}

void platform_console_write_error(const char *message, u8 colour)
{
  // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
  const char *colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
  printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}

// Seconds elapsed since platform_initialize().
f64 platform_get_absolute_time()
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (f64)(now.tv_sec - platform_ptr->start_time.tv_sec) +
         (f64)(now.tv_nsec - platform_ptr->start_time.tv_nsec) * 1e-9;
}
#endif
void platform_sleep(u64 ms)
{
  platform_sleep_us(ms * 1000);
}

void platform_sleep_us(u64 us)
{
  struct timespec ts;
  ts.tv_sec = (time_t)(us / 1000000);
  ts.tv_nsec = (long)((us % 1000000) * 1000);
  nanosleep(&ts, NULL);
}
