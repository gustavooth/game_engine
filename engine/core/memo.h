#pragma once

#include "defines.h"

typedef enum MemoTag
{
  MEMO_UNKNOWN,
  MEMO_ENGINE_INSTANCE,
  MEMO_STRING,
  MEMO_LINEAR_ALLOCATOR,
  MEMO_EGNARRAY,
  MEMO_WINDOW,
  MEMO_RENDERER,

  MEMO_TAG_MAX_TAGS,
  MEMO_NOT_INITIALIZED
} MemoTag;

EGNAPI b8 memo_initialize(u64 *out_memory_requirement, void *state);
EGNAPI void memo_shutdown();

EGNAPI void *memo_allocate(u64 size, MemoTag tag);

EGNAPI void memo_free(void *block);

EGNAPI void *memo_copy(void *dest, const void *source, u64 size);

EGNAPI void *memo_set(void *dest, i32 value, u64 size);

EGNAPI void memo_set_memory_usage_tag(MemoTag tag, u64 size);
EGNAPI u64 memo_get_memory_usage_tag(MemoTag tag);
EGNAPI void memo_set_allocations_count(u64 size);
EGNAPI u64 memo_get_allocations_count();

EGNAPI b8 memo_write_memory_usage_str(char *str, u64 str_len);

#define MEMOALLOC(size, tag) memo_allocate(size, tag)

#define MEMOZALLOC(size, tag) memo_set(memo_allocate(size, tag), 0, size)

#define MEMOFREE(block) memo_free(block)
#define MEMOZERO(block, size) memo_set(block, 0, size)
#define MEMOCPY(dest, source, size) memo_copy(dest, source, size)
#define MEMOSET(dest, value, size) memo_set(dest, value, size)
