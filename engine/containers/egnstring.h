#pragma once

#include "defines.h"

// Owned, valid UTF-8 string. Indices and ranges are byte offsets, as in Rust;
// all mutating ranges must lie on UTF-8 code point boundaries. Embedded NULs
// are rejected so egn_string_cstr() is always usable with C APIs.
// Initialize every destination with egn_string_init() or {0} before use.
// Constructors and substring replace an initialized destination atomically.
typedef struct EgnString {
  char *data;
  u64 length;
  u64 capacity;
} EgnString;

#define EGN_STRING_NPOS ((u64)-1)

EGNAPI void egn_string_init(EgnString *s);
EGNAPI b8 egn_string_from_utf8(EgnString *s, const char *bytes, u64 length);
EGNAPI b8 egn_string_from_cstr(EgnString *s, const char *text);
EGNAPI b8 egn_string_clone(EgnString *out, const EgnString *s);
EGNAPI void egn_string_destroy(EgnString *s);
EGNAPI const char *egn_string_cstr(const EgnString *s);
EGNAPI u64 egn_string_len(const EgnString *s);
EGNAPI u64 egn_string_char_count(const EgnString *s);
EGNAPI b8 egn_string_is_empty(const EgnString *s);
EGNAPI b8 egn_string_is_char_boundary(const EgnString *s, u64 offset);
EGNAPI b8 egn_string_utf8_valid(const char *bytes, u64 length);
EGNAPI b8 egn_string_reserve(EgnString *s, u64 additional);
EGNAPI b8 egn_string_shrink_to_fit(EgnString *s);
EGNAPI void egn_string_clear(EgnString *s);
EGNAPI b8 egn_string_push_str(EgnString *s, const char *text);
EGNAPI b8 egn_string_push_char(EgnString *s, u32 codepoint);
EGNAPI b8 egn_string_insert(EgnString *s, u64 offset, const char *text);
EGNAPI b8 egn_string_remove_range(EgnString *s, u64 start, u64 end);
EGNAPI b8 egn_string_truncate(EgnString *s, u64 length);
EGNAPI b8 egn_string_pop(EgnString *s, u32 *codepoint);
EGNAPI b8 egn_string_char_at(const EgnString *s, u64 offset, u32 *codepoint, u64 *next_offset);
EGNAPI b8 egn_string_substring(EgnString *out, const EgnString *s, u64 start, u64 end);
EGNAPI u64 egn_string_find(const EgnString *s, const char *needle);
EGNAPI b8 egn_string_starts_with(const EgnString *s, const char *prefix);
EGNAPI b8 egn_string_ends_with(const EgnString *s, const char *suffix);
EGNAPI b8 egn_string_equals(const EgnString *a, const EgnString *b);
