#pragma once

#include "defines.h"
#include "containers/egnarray.h"

b8 platform_initialize(u64* out_memory_requirement, void* state);
void platform_shutdown();

void *platform_allocate(u64 size, b8 aligned);
void platform_free(void *block, b8 aligned);
void *platform_zero_memory(void *block, u64 size);
void *platform_copy_memory(void *dest, const void *source, u64 size);
void *platform_set_memory(void *dest, i32 value, u64 size);

void platform_console_write(const char *message, u8 colour);
void platform_console_write_error(const char *message, u8 colour);

f64 platform_get_absolute_time();

void platform_sleep(u64 ms);
void platform_sleep_us(u64 us);
