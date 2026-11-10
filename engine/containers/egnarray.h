#pragma once

#include "defines.h"

/**
 * Memory layout
 * u64 capacity = number elements that can be held
 * u64 length = number of elements currently contained
 * u64 stride = size of each element in bytes
 * data = array of elements
 */
typedef void *egnarray;

enum
{
  EGNARRAY_CAPACITY,
  EGNARRAY_LENGTH,
  EGNARRAY_STRIDE,

  EGNARRAY_PADDING,
  EGNARRAY_DATA,
  EGNARRAY_FIELD_LENGTH = 4,
};

EGNAPI egnarray _egnarray_create(u32 capacity, u32 stride);
EGNAPI void egnarray_destroy(egnarray arr);

EGNAPI u32 _egnarray_field_get(egnarray arr, u32 field);
EGNAPI void _egnarray_field_set(egnarray arr, u32 field, u32 value);

EGNAPI egnarray _egnarray_extend(egnarray arr, u32 new_capacity);

EGNAPI egnarray _egnarray_push(egnarray arr, void *value_ptr);

/**
 * Remove the last item from the array.
 * @param egnarray Dynamic array.
 */
EGNAPI void egnarray_pop(egnarray arr);

/**
 * Remove the item at the chosen index from the array.
 * @param egnarray Dynamic array.
 * @param index Item index.
 */
EGNAPI void egnarray_pop_at(egnarray arr, u32 index);

#define EGNARRAY_DEFAULT_CAPACITY 3

/**
 * @brief Create a dynamic array
 * @param type Variable type.
 * @returns Dynamic array (egnarray).
 * @note Returns a direct pointer to the data.
 */
#define EGNARRAY(type) _egnarray_create(EGNARRAY_DEFAULT_CAPACITY, sizeof(type))

/**
 * Create a dynamic array
 * @param type Variable type.
 * @returns Dynamic array (egnarray).
 * @note Returns a direct pointer to the data.
 */
#define egnarray_create(type) _egnarray_create(EGNARRAY_DEFAULT_CAPACITY, sizeof(type))

/**
 * Create a dynamic array with the chosen capacity.
 * @param type Variable type.
 * @param size Array Capacity.
 * @returns Dynamic array (egnarray).
 * @note Returns a direct pointer to the data.
 */
#define egnarray_reserve(type, size) _egnarray_create(size, sizeof(type))

/**
 * Adds a copy of the value to the end of the array.
 * @param egnarray Dynamic array.
 * @param value_ptr Value pointer.
 */
#define egnarray_push(egnarray, value_ptr)          \
  {                                                 \
    egnarray = _egnarray_push(egnarray, value_ptr); \
  }

/**
 * This function returns the number of items in the array.
 * @param egnarray Dynamic array.
 * @returns Number of items in the array.
 */
#define egnarray_len(egnarray) _egnarray_field_get(egnarray, EGNARRAY_LENGTH)

/**
 * This function returns the size of each item in the array.
 * @param egnarray Dynamic array.
 * @returns Size of each item in the array.
 */
#define egnarray_stride(egnarray) _egnarray_field_get(egnarray, EGNARRAY_STRIDE)

/**
 * This function returns the capacity of the array.
 * @param egnarray Dynamic array.
 * @returns Array Capacity.
 */
#define egnarray_capacity(egnarray) _egnarray_field_get(egnarray, EGNARRAY_CAPACITY)
