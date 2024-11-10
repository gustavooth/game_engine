#include "memo.h"
#include "logger.h"
#include "platform/platform.h"

typedef struct MemoryData
{
  MemoTag tag;
  u64 length;
} MemoryData;

typedef struct MemoryState
{
  u64 tagged_allocations[MEMO_TAG_MAX_TAGS];
} MemoryState;

static MemoryState state;

b8 memo_initialize()
{
  EGNINFO("Memo system initialized!");
  return true;
}

void memo_shutdown()
{
}

void *memo_allocate(u64 size, MemoTag tag)
{
#if LOG_WARN_ENABLED == 1
  if (tag == MEMO_UNKNOWN)
    EGNWARN("memo_allocate called using memo_TAG_UNKNOWN.");
#endif
  state.tagged_allocations[tag] += size + sizeof(MemoryData);

  MemoryData *block = (MemoryData*)platform_allocate(sizeof(MemoryData) + size, false);
  block->tag = tag;
  block->length = size;

  return (void *)(block + 1);
}

void memo_free(void *block)
{
  MemoryData *data = ((MemoryData*)block) - 1;

#if LOG_WARN_ENABLED == 1
  if (data->tag == MEMO_UNKNOWN)
    EGNWARN("memo_allocate called using memo_TAG_UNKNOWN.");
#endif

  state.tagged_allocations[data->tag] -= data->length + sizeof(MemoryData);

  platform_free(data, false);
}

void *memo_copy(void *dest, const void *source, u64 size)
{
  return platform_copy_memory(dest, source, size);
}

void *memo_set(void *dest, i32 value, u64 size)
{
  return platform_set_memory(dest, value, size);
}
