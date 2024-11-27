#include "egnstring.h"

#include "core/memo.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

u64 string_length(const char *str)
{
  return strlen(str);
}

char *string_clone(const char *str)
{
  u64 len = string_length(str);

  char *new_str = MEMOALLOC(len + 1, MEMO_STRING);

  MEMOCPY(new_str, str, len);
  new_str[len] = '\0';

  return new_str;
}

b8 string_equals(const char *str1, const char *str2)
{
  return strcmp(str1, str2) == 0;
}

char *string_format(const char *format, ...)
{
  enum
  {
    NEW_STR_LEN = 1024
  };

  char *new_str = MEMOALLOC(NEW_STR_LEN, MEMO_STRING);

  va_list arg_ptr;
  va_start(arg_ptr, format);
  vsnprintf(new_str, NEW_STR_LEN, format, arg_ptr);
  va_end(arg_ptr);

  return new_str;
}

b8 string_format2(char *dest, u64 dest_len, u32 *out_required_chars, const char *format, ...)
{
  va_list arg_ptr;
  va_start(arg_ptr, format);
  i32 count = vsnprintf(dest, dest_len, format, arg_ptr);
  va_end(arg_ptr);

  if (count < 0)
    return false;

  if (count >= (i32)dest_len)
  {
    if (out_required_chars)
      *out_required_chars = (u32)count + 1; // +1 for including the null character

    return false;
  }

  if (out_required_chars)
    *out_required_chars = (u32)count;

  return true;
}
