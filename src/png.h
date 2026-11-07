/* PNG encode/decode (8-bit, non-interlaced) via zlib. Used for screenshots
 * and the optional resource-pack textures. */
#ifndef NC_PNG_H
#define NC_PNG_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

/* Write the framebuffer scaled by `scale` (nearest). Returns 0 on success. */
int png_write_fb(const char *path, const Fb *f, int scale);
/* Encode RGBA bytes to a malloc'd PNG buffer. */
int png_encode(const uint32_t *argb, int w, int h, uint8_t **out, size_t *outn);
/* Decode a PNG file into a new Fb (0xAARRGGBB). Returns NULL on failure. */
Fb *png_read_fb(const char *path);

#endif
