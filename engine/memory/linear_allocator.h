#pragma once

#include "defines.h"

typedef struct LinearAllocator {
  u64 total_size;
  u64 allocated;
  void *memory;
} LinearAllocator;

EGNAPI void linear_allocator_create(u64 size, LinearAllocator *out_allocator);
EGNAPI void linear_allocator_destroy(LinearAllocator *allocator);

EGNAPI void* linear_allocator_allocate(LinearAllocator *allocator, u64 size);
EGNAPI void linear_allocator_free_all(LinearAllocator *allocator);

#define LIALLOC(allocator, size) linear_allocator_allocate(allocator, size);
