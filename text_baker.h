#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct BakedGlyph
{
    float x0;
    float y0;
    float x1;
    float y1;
    float xoff;
    float yoff;
    float xadvance;
} BakedGlyph;

bool text_bake_font_rgba(const char* font_path,
                         float       pixel_height,
                         uint32_t    atlas_width,
                         uint32_t    atlas_height,
                         BakedGlyph  glyphs[96],
                         uint8_t**   out_rgba_pixels,
                         size_t*     out_rgba_size);
