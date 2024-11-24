#pragma once

#include "defines.h"

EGNAPI u64 string_length(const char* str);

EGNAPI char* string_clone(const char* str);

EGNAPI b8 string_equals(const char* str1, const char* str2);

EGNAPI char* string_format(const char* format, ...);

#define STRLEN(str) string_length(str)
#define STRCLONE(str) string_clone(str)
#define STREQUALS(str1, str2) string_equals(str1, str2)
#define STRFORMAT(format, ...) string_format(format, __VA_ARGS__)
