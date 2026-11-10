#include "text/font.h"
#include "containers/egnstring.h"
#include "core/memo.h"
#include <math.h>
#include <string.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

u8 *egn_font_rasterize(const u8 *font, u32 font_size, const EgnString *text,
                       f32 pixel_height, u32 *width, u32 *height)
{
  if (width) *width = 0;
  if (height) *height = 0;
  if (!font || font_size < 12 || !text || !width || !height || !isfinite(pixel_height) || pixel_height <= 0 || pixel_height > 512) return 0;
  u64 length = egn_string_len(text);
  if (!egn_string_utf8_valid(egn_string_cstr(text), length)) return 0;
  int offset = stbtt_GetFontOffsetForIndex(font, 0);
  stbtt_fontinfo info;
  if (offset < 0 || (u32)offset >= font_size || !stbtt_InitFont(&info, font, offset)) return 0;
  f32 scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
  int ascent, descent, gap;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
  f32 line_height = (ascent - descent + gap) * scale;
  if (line_height <= 0) return 0;
  f32 pen_x = 0, baseline = ascent * scale, min_x = 0, min_y = 0, max_x = 1, max_y = line_height;
  u32 previous = 0;
  for (u64 pos = 0; pos < length;) {
    u32 cp; u64 next;
    if (!egn_string_char_at(text, pos, &cp, &next)) return 0;
    pos = next;
    if (cp == '\n') { if (pen_x > max_x) max_x = pen_x; pen_x = 0; baseline += line_height; max_y = baseline - descent * scale; previous = 0; continue; }
    if (cp == '\r') continue;
    if (previous) pen_x += stbtt_GetCodepointKernAdvance(&info, previous, cp) * scale;
    int x0, y0, x1, y1, advance, bearing;
    stbtt_GetCodepointBitmapBox(&info, cp, scale, scale, &x0, &y0, &x1, &y1);
    stbtt_GetCodepointHMetrics(&info, cp, &advance, &bearing);
    if (pen_x + x0 < min_x) min_x = pen_x + x0;
    if (baseline + y0 < min_y) min_y = baseline + y0;
    if (pen_x + x1 > max_x) max_x = pen_x + x1;
    if (baseline + y1 > max_y) max_y = baseline + y1;
    pen_x += advance * scale; previous = cp;
    if (pen_x > max_x) max_x = pen_x;
    if (max_x - min_x > 4096 || max_y - min_y > 4096) return 0;
  }
  int xshift = (int)floorf(min_x), yshift = (int)floorf(min_y);
  *width = (u32)ceilf(max_x - xshift); *height = (u32)ceilf(max_y - yshift);
  if (!*width || !*height || *width > 4096 || *height > 4096) return 0;
  u64 count = (u64)*width * *height;
  u8 *pixels = MEMOALLOC(count * 4, MEMO_STRING);
  if (!pixels) { *width = *height = 0; return 0; }
  for (u64 i = 0; i < count; ++i) { pixels[i*4] = 255; pixels[i*4+1] = 255; pixels[i*4+2] = 255; pixels[i*4+3] = 0; }
  pen_x = 0; baseline = ascent * scale; previous = 0;
  for (u64 pos = 0; pos < length;) {
    u32 cp; u64 next; egn_string_char_at(text, pos, &cp, &next); pos = next;
    if (cp == '\n') { pen_x = 0; baseline += line_height; previous = 0; continue; }
    if (cp == '\r') continue;
    if (previous) pen_x += stbtt_GetCodepointKernAdvance(&info, previous, cp) * scale;
    int gw, gh, gx, gy, advance, bearing;
    unsigned char *bitmap = stbtt_GetCodepointBitmap(&info, scale, scale, cp, &gw, &gh, &gx, &gy);
    int x = (int)floorf(pen_x) + gx - xshift, y = (int)floorf(baseline) + gy - yshift;
    if (bitmap) {
      for (int row = 0; row < gh; ++row) for (int col = 0; col < gw; ++col) {
        int px = x + col, py = y + row;
        if (px >= 0 && py >= 0 && px < (int)*width && py < (int)*height) {
          u8 *alpha = &pixels[((u64)py * *width + px) * 4 + 3];
          if (bitmap[row * gw + col] > *alpha) *alpha = bitmap[row * gw + col];
        }
      }
      stbtt_FreeBitmap(bitmap, 0);
    }
    stbtt_GetCodepointHMetrics(&info, cp, &advance, &bearing);
    pen_x += advance * scale; previous = cp;
  }
  return pixels;
}
void egn_font_pixels_free(u8 *pixels) { MEMOFREE(pixels); }
