#include "memo.h"
#include "logger.h"
#include "platform/platform.h"

static const char *memory_tag_strings[MEMO_TAG_MAX_TAGS] = {
    "UNKNOWN         ",
    "EGNARRAY        ",
    "WINDOW          ",
    "RENDERER        ",
};

typedef struct memory_data
{
  MemoTag tag;
  u64 length;
} memory_data;

typedef struct memoory_state
{
  u64 tagged_allocations[MEMO_TAG_MAX_TAGS];
} memoory_state;

static memoory_state state;

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
  state.tagged_allocations[tag] += size + sizeof(memory_data);

  memory_data *block = platform_allocate(sizeof(memory_data) + size, false);
  block->tag = tag;
  block->length = size;

  return (void *)(((u64)block) + sizeof(memory_data));
}

void memo_free(void *block)
{
  memory_data *data = (memory_data *)(((u64)block) - sizeof(memory_data));

#if LOG_WARN_ENABLED == 1
  if (data->tag == MEMO_UNKNOWN)
    EGNWARN("memo_allocate called using memo_TAG_UNKNOWN.");
#endif
  state.tagged_allocations[data->tag] -= data->length + sizeof(memory_data);

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
