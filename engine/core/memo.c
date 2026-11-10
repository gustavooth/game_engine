#include "memo.h"
#include "logger.h"
#include "platform/platform.h"
#include "core/egnstring.h"

static const char *MemoryTagStrings[MEMO_TAG_MAX_TAGS] = {
    "UNKNOWN            ",
    "ENGINE_INSTANCE    ",
    "STRING             ",
    "LINEAR_ALLOCATOR   ",
    "EGNARRAY           ",
    "WINDOW             ",
    "RENDERER           ",
    "FILE               ",
    "SCENE              ",
    "ASSET              "};

typedef struct MemoryData
{
  MemoTag tag;
  u64 length;
} MemoryData;

typedef struct MemoryState
{
  u64 tagged_allocations[MEMO_TAG_MAX_TAGS];
  u64 alloc_count;
} MemoryState;

static MemoryState *state_ptr = 0;

b8 memo_initialize(u64 *out_memory_requirement, void *state)
{
  if (!state)
  {
    *out_memory_requirement = sizeof(MemoryState);
    return false;
  }

  state_ptr = (MemoryState *)state;

  EGNINFO("Core Memo initialized!");

  return true;
}

void memo_shutdown()
{
  state_ptr = 0;
}

void *memo_allocate(u64 size, MemoTag tag)
{
#if LOG_WARN_ENABLED == 1
  if (tag == MEMO_UNKNOWN)
    EGNWARN("memo_allocate called using MEMO_TAG_UNKNOWN.");
#endif

  if (size > (u64)-1 - sizeof(MemoryData) || ((u32)tag >= MEMO_TAG_MAX_TAGS && tag != MEMO_NOT_INITIALIZED))
    return 0;
  MemoryData *block = (MemoryData *)platform_allocate(sizeof(MemoryData) + size, false);
  if (!block) return 0;
  block->tag = tag;
  block->length = size;
  if (state_ptr && tag != MEMO_NOT_INITIALIZED)
  {
    state_ptr->tagged_allocations[tag] += size + sizeof(MemoryData);
    state_ptr->alloc_count++;
  }

  return (void *)(block + 1);
}

void *memo_allocate_zero(u64 size, MemoTag tag)
{
  void *block = memo_allocate(size, tag);
  if (block) platform_set_memory(block, 0, size);
  return block;
}

void memo_free(void *block)
{
  if (!block) return;
  MemoryData *data = ((MemoryData *)block) - 1;

#if LOG_WARN_ENABLED == 1
  if (data->tag == MEMO_UNKNOWN)
    EGNWARN("memo_free called using MEMO_TAG_UNKNOWN.");
#endif

  if (state_ptr && data->tag != MEMO_NOT_INITIALIZED)
  {
    state_ptr->tagged_allocations[data->tag] -= data->length + sizeof(MemoryData);
    state_ptr->alloc_count--;
  }

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

void memo_set_memory_usage_tag(MemoTag tag, u64 size) {
  state_ptr->tagged_allocations[tag] = size;
}

EGNAPI u64 memo_get_memory_usage_tag(MemoTag tag) {
  return state_ptr->tagged_allocations[tag];
}

EGNAPI void memo_set_allocations_count(u64 size) {
  state_ptr->alloc_count = size;
}

EGNAPI u64 memo_get_allocations_count() {
  return state_ptr->alloc_count;
}

b8 memo_write_memory_usage_str(char *str, u64 str_len)
{
  const char *title = "Core memory usage (tagged):\n";
  u64 title_len = string_length(title);
  title_len++; // "/n"
  if (!str || str_len < title_len || !state_ptr) return false;
  MEMOCPY(str, title, title_len);
  u64 offset = title_len - 1;

  const u64 gb = 1024 * 1024 * 1024;
  const u64 mb = 1024 * 1024;
  const u64 kb = 1024;

  for (i32 i = 0; i < MEMO_TAG_MAX_TAGS; i++)
  {
    char unit[3] = "xb";
    f32 amount = 0.0f;

    if (state_ptr->tagged_allocations[i] >= gb)
    {
      unit[0] = 'g';
      amount = state_ptr->tagged_allocations[i] / (f32)gb;
    }
    else if (state_ptr->tagged_allocations[i] >= mb)
    {
      unit[0] = 'm';
      amount = state_ptr->tagged_allocations[i] / (f32)mb;
    }
    else if (state_ptr->tagged_allocations[i] >= kb)
    {
      unit[0] = 'k';
      amount = state_ptr->tagged_allocations[i] / (f32)kb;
    }
    else
    {
      unit[0] = 'b';
      unit[1] = '\0';
      amount = (f32)state_ptr->tagged_allocations[i];
    }

    u32 written = 0;
    if (!string_format2(str + offset, str_len - offset, &written, "%s: %.2f%s\n", MemoryTagStrings[i], amount, unit))
    {
      EGNERROR("memo_get_memory_usage_str: format error! - Probably not enough memory.");
      return false;
    }

    offset += written;
  }

  if (offset > str_len)
  {
    EGNERROR("memo_get_memory_usage_str: insufficient memory!");
    return false;
  }

  u32 written_chars;
  if (!string_format2(str+offset, str_len - offset, &written_chars, "ALLOCATIONS COUNT  : %llu", state_ptr->alloc_count)) return false;
  offset += written_chars;

  return true;
}