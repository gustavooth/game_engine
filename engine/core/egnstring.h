#pragma once

#include "defines.h"

EGNAPI u64 string_length(const char *str);

EGNAPI char *string_clone(const char *str);

EGNAPI b8 string_equals(const char *str1, const char *str2);

EGNAPI char *string_format(const char *format, ...);

/**
 * @brief Format the string
 *
 * @param dest Pointer to write the formatted string
 * @param dest_len Dest string lenght
 * @param out_required_chars Stores the character count to be written
 * @return Returns true if the string written.
 */
EGNAPI b8 string_format2(char *dest, u64 dest_len, u32 *out_required_chars, const char *format, ...);

#define STRLEN(str) string_length(str)
#define STRCLONE(str) string_clone(str)
#define STREQUALS(str1, str2) string_equals(str1, str2)
#define STRFORMAT(format, ...) string_format(format, __VA_ARGS__)
