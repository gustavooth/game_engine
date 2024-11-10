#include "egnarray.h"
#include "core/logger.h"
#include <stdlib.h>
#include <string.h>

egnarray _egnarray_create(u32 capacity, u32 stride)
{
  u32 total_size = (EGNARRAY_FIELD_LENGTH * sizeof(u32)) + (capacity * stride);
  u32 *header = malloc(total_size);
  header[EGNARRAY_CAPACITY] = capacity;
  header[EGNARRAY_LENGTH] = 0;
  header[EGNARRAY_STRIDE] = stride;
  return header + EGNARRAY_DATA;
}

void egnarray_destroy(egnarray arr)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  free(header);
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
  u32 *new_header = (u32 *)new_arr - EGNARRAY_FIELD_LENGTH;

  memcpy(new_arr, arr, header[EGNARRAY_LENGTH] * header[EGNARRAY_STRIDE]);
  new_header[EGNARRAY_LENGTH] = header[EGNARRAY_LENGTH];

  egnarray_destroy(arr);

  return new_arr;
}

egnarray _egnarray_push(egnarray arr, void *value_ptr)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;

  if (header[EGNARRAY_CAPACITY] == header[EGNARRAY_LENGTH])
  {
    arr = _egnarray_extend(arr, header[EGNARRAY_CAPACITY] + EGNARRAY_DEFAULT_CAPACITY);
    header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  }

  memcpy((void *)((u64)arr + header[EGNARRAY_STRIDE] * header[EGNARRAY_LENGTH]), value_ptr, header[EGNARRAY_STRIDE]);
  header[EGNARRAY_LENGTH] += 1;

  return arr;
}

void egnarray_pop(egnarray arr)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;
  memset((void *)((u64)arr + header[EGNARRAY_STRIDE] * (header[EGNARRAY_LENGTH] - 1)), 0, header[EGNARRAY_STRIDE]);
  header[EGNARRAY_LENGTH] -= 1;
}

void egnarray_pop_at(egnarray arr, u32 index)
{
  u32 *header = (u32 *)arr - EGNARRAY_FIELD_LENGTH;

  u64 addr = (u64)arr + (header[EGNARRAY_STRIDE] * (index + 1));
  u32 size = (header[EGNARRAY_LENGTH] - (index + 1)) * header[EGNARRAY_STRIDE];

  void *buffer = malloc(size);
  memcpy(buffer, (void *)addr, size);
  memcpy((void *)(addr - header[EGNARRAY_STRIDE]), buffer, size);
  free(buffer);

  header[EGNARRAY_LENGTH] -= 1;
}