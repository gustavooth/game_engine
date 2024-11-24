#include "linear_allocator.h"
#include "core/logger.h"
#include "core/memo.h"

EGNAPI void linear_allocator_create(u64 size, LinearAllocator *out_allocator) {
  if (!out_allocator){
    EGNERROR("linear_allocator_create: out_allocator is null.")
    return;
  }

  out_allocator->memory = MEMOALLOC(size, MEMO_LINEAR_ALLOCATOR);
  out_allocator->total_size = size;
  out_allocator->allocated = 0;
}

EGNAPI void linear_allocator_destroy(LinearAllocator *allocator) {
  if (!allocator){
    EGNERROR("linear_allocator_destroy: allocator is null.");
    return;
  }

  MEMOFREE(allocator->memory);
  MEMOZERO(allocator, sizeof(LinearAllocator));
}

EGNAPI void* linear_allocator_allocate(LinearAllocator *allocator, u64 size) {
  if (!allocator){
    EGNERROR("linear_allocator_allocate: allocator is null.");
    return 0;
  }

  u64 remaining = allocator->total_size - allocator->allocated;

  if (remaining < size)
  {
    EGNERROR("linear_allocator_allocate: insufficient memory! Remaning: %llu Requested: %llu", remaining, size);
    return 0;
  }

  void* block = allocator->memory + allocator->allocated;

  allocator->allocated += size;

  return block;
}

EGNAPI void linear_allocator_free_all(LinearAllocator *allocator) {
  if (!allocator){
    EGNERROR("linear_allocator_free_all: allocator is null.");
    return;
  }

  allocator->allocated = 0;
}
