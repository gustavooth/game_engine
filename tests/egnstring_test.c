#include "containers/egnstring.h"
#include <assert.h>
#include <string.h>

int main(void) {
  EgnString s = {0}, sub = {0};
  assert(egn_string_from_cstr(&s, "Olá 🌍"));
  assert(egn_string_len(&s) == 9 && egn_string_char_count(&s) == 5);
  assert(!egn_string_is_char_boundary(&s, 3));
  assert(!egn_string_remove_range(&s, 2, 3));
  assert(egn_string_find(&s, "🌍") == 5);
  assert(egn_string_substring(&sub, &s, 0, 4));
  assert(strcmp(egn_string_cstr(&sub), "Olá") == 0);
  egn_string_destroy(&sub);
  assert(egn_string_insert(&s, 0, egn_string_cstr(&s)));
  assert(egn_string_char_count(&s) == 10);
  u32 cp = 0;
  assert(egn_string_pop(&s, &cp) && cp == 0x1f30d);
  assert(egn_string_push_char(&s, cp));
  assert(egn_string_ends_with(&s, "🌍"));
  assert(!egn_string_push_char(&s, 0xd800));
  assert(!egn_string_utf8_valid("\xc0\x80", 2));
  assert(!egn_string_utf8_valid("\xed\xa0\x80", 3));
  assert(!egn_string_utf8_valid("\xf4\x90\x80\x80", 4));
  assert(!egn_string_utf8_valid("a\0b", 3));
  assert(egn_string_shrink_to_fit(&s));
  assert(egn_string_clone(&sub, &s));
  assert(egn_string_equals(&s, &sub));
  assert(egn_string_substring(&sub, &sub, 0, 4));
  assert(strcmp(egn_string_cstr(&sub), "Olá") == 0);
  egn_string_destroy(&sub);
  egn_string_clear(&s);
  assert(egn_string_is_empty(&s));
  egn_string_destroy(&s);
  return 0;
}
