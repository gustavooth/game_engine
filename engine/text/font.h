#pragma once
#include "defines.h"
#include "containers/egnstring.h"
// Rasterize UTF-8 into white RGBA8 pixels with alpha, newline and kerning.
// The returned buffer must be released with egn_font_pixels_free. The font must
// be a trusted valid TTF/OTF file; stb_truetype does not sandbox font parsing.
EGNAPI u8 *egn_font_rasterize(const u8 *font, u32 font_size, const EgnString *text,
                               f32 pixel_height, u32 *width, u32 *height);
EGNAPI void egn_font_pixels_free(u8 *pixels);
