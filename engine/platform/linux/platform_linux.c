#include "defines.h"
#include "core/logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef EGNPLATFORM_WAYLAND

// Clock
static struct timeval start_time;

b8 platform_initialize()
{
  // Clock setup usando gettimeofday
  if (gettimeofday(&start_time, NULL) != 0)
  {
    EGNERROR("Failed to initialize clock");
    return false;
  }

  EGNINFO("Linux Platform System initialized!");
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

// Função que calcula o tempo de execução em segundos (f64)
f64 platform_get_absolute_time()
{
  struct timeval current_time;
  gettimeofday(&current_time, NULL);

  // Calcula a diferença em segundos e microssegundos
  i64 seconds = current_time.tv_sec - start_time.tv_sec;
  i64 microseconds = current_time.tv_usec - start_time.tv_usec;

  // Retorna o tempo total em segundos como um f64
  return (f64)seconds + (f64)microseconds / 1e6;
}

#endif