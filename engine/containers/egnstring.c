#include "egnstring.h"
#include "core/memo.h"
#include <stdint.h>
#include <string.h>

// Decode one Unicode scalar, rejecting overlongs, surrogates and > U+10FFFF.
static u32 decode(const unsigned char *p, u64 n, u32 *out) {
  if (!n || !p[0]) return 0;
  u32 cp, count;
  if (p[0] < 0x80) { cp = p[0]; count = 1; }
  else if (p[0] >= 0xc2 && p[0] <= 0xdf) { cp = p[0] & 0x1f; count = 2; }
  else if (p[0] >= 0xe0 && p[0] <= 0xef) { cp = p[0] & 0x0f; count = 3; }
  else if (p[0] >= 0xf0 && p[0] <= 0xf4) { cp = p[0] & 7; count = 4; }
  else return 0;
  if (n < count) return 0;
  for (u32 i = 1; i < count; ++i) {
    if ((p[i] & 0xc0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3f);
  }
  if ((count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) ||
      (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return 0;
  if (out) *out = cp;
  return count;
}

b8 egn_string_utf8_valid(const char *bytes, u64 length) {
  if (!bytes && length) return false;
  for (u64 i = 0; i < length;) {
    u32 n = decode((const unsigned char *)bytes + i, length - i, 0);
    if (!n) return false;
    i += n;
  }
  return true;
}
void egn_string_init(EgnString *s) { if (s) *s = (EgnString){0}; }
void egn_string_destroy(EgnString *s) { if (s) { MEMOFREE(s->data); *s = (EgnString){0}; } }
const char *egn_string_cstr(const EgnString *s) { return s && s->data ? s->data : ""; }
u64 egn_string_len(const EgnString *s) { return s ? s->length : 0; }
b8 egn_string_is_empty(const EgnString *s) { return !s || !s->length; }

static b8 reallocate(EgnString *s, u64 capacity) {
  if (!s || capacity == UINT64_MAX || capacity > SIZE_MAX - 1) return false;
  char *data = MEMOALLOC(capacity + 1, MEMO_STRING);
  if (!data) return false;
  if (s->length) memcpy(data, s->data, (size_t)s->length);
  data[s->length] = 0;
  MEMOFREE(s->data);
  s->data = data;
  s->capacity = capacity;
  return true;
}
b8 egn_string_reserve(EgnString *s, u64 additional) {
  if (!s || additional > UINT64_MAX - s->length) return false;
  u64 need = s->length + additional;
  if (need <= s->capacity) return true;
  u64 cap = s->capacity ? s->capacity : 16;
  while (cap < need) {
    if (cap > (UINT64_MAX - 1) / 2) { cap = need; break; }
    cap *= 2;
  }
  return reallocate(s, cap);
}
b8 egn_string_shrink_to_fit(EgnString *s) {
  if (!s) return false;
  if (!s->length) { egn_string_destroy(s); return true; }
  return s->capacity == s->length || reallocate(s, s->length);
}
b8 egn_string_from_utf8(EgnString *s, const char *bytes, u64 length) {
  if (!s || !egn_string_utf8_valid(bytes, length)) return false;
  EgnString result = {0};
  if (!egn_string_reserve(&result, length)) return false;
  if (length) memcpy(result.data, bytes, (size_t)length);
  result.length = length;
  if (result.data) result.data[length] = 0;
  egn_string_destroy(s);
  *s = result;
  return true;
}
b8 egn_string_from_cstr(EgnString *s, const char *text) { return text && egn_string_from_utf8(s, text, strlen(text)); }
b8 egn_string_clone(EgnString *out, const EgnString *s) { return s && egn_string_from_utf8(out, egn_string_cstr(s), s->length); }
void egn_string_clear(EgnString *s) { if (s) { s->length = 0; if (s->data) s->data[0] = 0; } }
b8 egn_string_is_char_boundary(const EgnString *s, u64 offset) {
  return s && offset <= s->length && (offset == s->length || (((unsigned char)s->data[offset] & 0xc0) != 0x80));
}
b8 egn_string_char_at(const EgnString *s, u64 offset, u32 *cp, u64 *next) {
  if (!s || offset >= s->length || !egn_string_is_char_boundary(s, offset)) return false;
  u32 n = decode((const unsigned char *)s->data + offset, s->length - offset, cp);
  if (!n) return false;
  if (next) *next = offset + n;
  return true;
}
u64 egn_string_char_count(const EgnString *s) {
  if (!s) return 0;
  u64 count = 0;
  for (u64 i = 0; i < s->length; ++i) if (((unsigned char)s->data[i] & 0xc0) != 0x80) ++count;
  return count;
}
b8 egn_string_insert(EgnString *s, u64 offset, const char *text) {
  if (!s || !text || !egn_string_is_char_boundary(s, offset)) return false;
  size_t n = strlen(text);
  if (!egn_string_utf8_valid(text, n)) return false;
  if (!n) return true;
  // The source may point into this string; preserve it before reallocating or shifting.
  char *copy = MEMOALLOC(n + 1, MEMO_STRING);
  if (!copy) return false;
  memcpy(copy, text, n + 1);
  if (!egn_string_reserve(s, n)) { MEMOFREE(copy); return false; }
  memmove(s->data + offset + n, s->data + offset, (size_t)(s->length - offset + 1));
  memcpy(s->data + offset, copy, n);
  s->length += n;
  MEMOFREE(copy);
  return true;
}
b8 egn_string_push_str(EgnString *s, const char *text) { return s && egn_string_insert(s, s->length, text); }
b8 egn_string_push_char(EgnString *s, u32 cp) {
  char b[5] = {0};
  if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  if (cp < 0x80) b[0] = (char)cp;
  else if (cp < 0x800) { b[0] = (char)(0xc0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 63)); }
  else if (cp < 0x10000) { b[0] = (char)(0xe0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 63)); b[2] = (char)(0x80 | (cp & 63)); }
  else { b[0] = (char)(0xf0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 63)); b[2] = (char)(0x80 | ((cp >> 6) & 63)); b[3] = (char)(0x80 | (cp & 63)); }
  return egn_string_push_str(s, b);
}
b8 egn_string_remove_range(EgnString *s, u64 start, u64 end) {
  if (!s || start > end || !egn_string_is_char_boundary(s, start) || !egn_string_is_char_boundary(s, end)) return false;
  if (start != end) { memmove(s->data + start, s->data + end, (size_t)(s->length - end + 1)); s->length -= end - start; }
  return true;
}
b8 egn_string_truncate(EgnString *s, u64 length) {
  if (!s) return false;
  if (length >= s->length) return true;
  return egn_string_remove_range(s, length, s->length);
}
b8 egn_string_pop(EgnString *s, u32 *cp) {
  if (!s || !s->length) return false;
  u64 i = s->length - 1;
  while (!egn_string_is_char_boundary(s, i)) --i;
  return egn_string_char_at(s, i, cp, 0) && egn_string_remove_range(s, i, s->length);
}
b8 egn_string_substring(EgnString *out, const EgnString *s, u64 start, u64 end) {
  return out && s && start <= end && egn_string_is_char_boundary(s, start) && egn_string_is_char_boundary(s, end) && egn_string_from_utf8(out, egn_string_cstr(s) + start, end - start);
}
u64 egn_string_find(const EgnString *s, const char *needle) {
  if (!s || !needle || !egn_string_utf8_valid(needle, strlen(needle))) return EGN_STRING_NPOS;
  const char *p = strstr(egn_string_cstr(s), needle);
  return p ? (u64)(p - egn_string_cstr(s)) : EGN_STRING_NPOS;
}
b8 egn_string_starts_with(const EgnString *s, const char *prefix) { return s && prefix && strncmp(egn_string_cstr(s), prefix, strlen(prefix)) == 0; }
b8 egn_string_ends_with(const EgnString *s, const char *suffix) {
  if (!s || !suffix) return false;
  size_t n = strlen(suffix);
  return n <= s->length && memcmp(egn_string_cstr(s) + s->length - n, suffix, n) == 0;
}
b8 egn_string_equals(const EgnString *a, const EgnString *b) { return a && b && a->length == b->length && memcmp(egn_string_cstr(a), egn_string_cstr(b), (size_t)a->length) == 0; }
