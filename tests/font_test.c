#include "text/font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb"); assert(f);
  fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  unsigned char *font = malloc(n); assert(font && fread(font, 1, n, f) == (unsigned long)n); fclose(f);
  unsigned w = 0, h = 0, w2 = 0, h2 = 0;
  EgnString text = {0};
  assert(egn_string_from_cstr(&text, "Olá 世界\nAV"));
  unsigned char *pixels = egn_font_rasterize(font, n, &text, 28, &w, &h);
  assert(pixels && w > 1 && h > 28);
  unsigned filled = 0; for (unsigned i = 0; i < w*h; ++i) filled += pixels[i*4+3] != 0;
  assert(filled > 0 && filled < w*h);
  egn_font_pixels_free(pixels);
  assert(egn_string_from_cstr(&text, "AV"));
  pixels = egn_font_rasterize(font, n, &text, 28, &w2, &h2);
  assert(pixels && h2 < h); egn_font_pixels_free(pixels);
  assert(!egn_string_from_utf8(&text, "\xc0\x80", 2));
  EgnString invalid = {(char *)"\xc0\x80", 2, 2};
  assert(!egn_font_rasterize(font, n, &invalid, 28, &w2, &h2));
  egn_string_destroy(&text);
  free(font); return 0;
}
