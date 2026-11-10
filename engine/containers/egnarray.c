#include "egnarray.h"
#include "core/logger.h"
#include "core/memo.h"
#include <string.h>

egnarray _egnarray_create(u32 capacity, u32 stride)
{
  u64 total_size = (EGNARRAY_FIELD_LENGTH * sizeof(u32)) + (u64)capacity * stride;
  u32 *header = MEMOALLOC(total_size, MEMO_EGNARRAY);
  if (!header) return 0;
  header[EGNARRAY_CAPACITY] = capacity;
  header[EGNARRAY_LENGTH] = 0;
  header[EGNARRAY_STRIDE] = stride;
  return header + EGNARRAY_DATA;
}

void egnarray_destroy(egnarray arr)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  MEMOFREE(header);
}

u32 _egnarray_field_get(egnarray arr, u32 field)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  return header[field];
}

void _egnarray_field_set(egnarray arr, u32 field, u32 value)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  header[field] = value;
}

egnarray _egnarray_extend(egnarray arr, u32 new_capacity)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;

  egnarray new_arr = _egnarray_create(new_capacity, header[EGNARRAY_STRIDE]);
  if (!new_arr) return arr;
  u32 *new_header = (u32 *)new_arr - EGNARRAY_FIELD_LENGTH;

  MEMOCPY(new_arr, arr, header[EGNARRAY_LENGTH] * header[EGNARRAY_STRIDE]);
  new_header[EGNARRAY_LENGTH] = header[EGNARRAY_LENGTH];

  egnarray_destroy(arr);

  return new_arr;
}

egnarray _egnarray_push(egnarray arr, void *value_ptr)
{
  if (!arr || !value_ptr) return arr;
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;

  if (header[EGNARRAY_CAPACITY] == header[EGNARRAY_LENGTH])
  {
    if (header[EGNARRAY_CAPACITY] > (u32)-1 - EGNARRAY_DEFAULT_CAPACITY) return arr;
    arr = _egnarray_extend(arr, header[EGNARRAY_CAPACITY] + EGNARRAY_DEFAULT_CAPACITY);
    header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
    if (header[EGNARRAY_CAPACITY] == header[EGNARRAY_LENGTH]) return arr;
  }

  MEMOCPY((void *)((u64)arr + header[EGNARRAY_STRIDE] * header[EGNARRAY_LENGTH]), value_ptr, header[EGNARRAY_STRIDE]);
  header[EGNARRAY_LENGTH] += 1;

  return arr;
}

void egnarray_pop(egnarray arr)
{
  if (!arr) return;
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  if (!header[EGNARRAY_LENGTH]) return;
  header[EGNARRAY_LENGTH]--;
  MEMOSET((char *)arr + (u64)header[EGNARRAY_STRIDE] * header[EGNARRAY_LENGTH], 0, header[EGNARRAY_STRIDE]);
}

void egnarray_pop_at(egnarray arr, u32 index)
{
  if (!arr) return;
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  u32 length = header[EGNARRAY_LENGTH];
  if (index >= length) return;
  u32 stride = header[EGNARRAY_STRIDE];
  char *destination = (char *)arr + (u64)index * stride;
  memmove(destination, destination + stride, (u64)(length - index - 1) * stride);
  header[EGNARRAY_LENGTH]--;
  MEMOSET((char *)arr + (u64)header[EGNARRAY_LENGTH] * stride, 0, stride);
}
