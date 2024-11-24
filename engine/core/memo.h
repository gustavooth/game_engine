#pragma once

#include "defines.h"

typedef enum MemoTag
{
  MEMO_UNKNOWN,
  MEMO_LINEAR_ALLOCATOR,
  MEMO_EGNARRAY,
  MEMO_WINDOW,
  MEMO_RENDERER,

  MEMO_TAG_MAX_TAGS
} MemoTag;

EGNAPI b8 memo_initialize();
EGNAPI void memo_shutdown();

EGNAPI void *memo_allocate(u64 size, MemoTag tag);

EGNAPI void memo_free(void *block);

EGNAPI void *memo_copy(void *dest, const void *source, u64 size);

EGNAPI void *memo_set(void *dest, i32 value, u64 size);

#define MEMOALLOC(size, tag) memo_allocate(size, tag)

#define MEMOZALLOC(size, tag) memo_set(memo_allocate(size, tag), 0, size)

#define MEMOFREE(block) memo_free(block)
#define MEMOZERO(block, size) memo_set(block, 0, size)
#define MEMOCPY(dest, source, size) memo_copy(dest, source, size)
#define MEMOSET(dest, value, size) memo_set(dest, value, size)
