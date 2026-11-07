/* UTF-8 / code point helpers. Pages are stored as arrays of Unicode code
 * points; Minecraft counts limits in UTF-16 code units, see cp_units(). */
#ifndef NC_UTF_H
#define NC_UTF_H

#include <stddef.h>
#include <stdint.h>

/* Decode one code point from s (n bytes available). Returns bytes consumed
 * (always >= 1). Invalid sequences yield U+FFFD and consume one byte. */
int u8_decode(const char *s, size_t n, uint32_t *cp);

/* Encode cp into out (up to 4 bytes). Returns bytes written. */
int u8_encode(uint32_t cp, char out[4]);

/* Decode a whole UTF-8 string into code points. Returns the count written. */
size_t u8_to_cps(const char *s, size_t n, uint32_t *out, size_t cap);

/* Encode code points to a NUL-terminated UTF-8 string. Returns bytes written
 * (without the NUL). Output is truncated on a code point boundary. */
size_t cps_to_u8(const uint32_t *cp, int n, char *out, size_t cap);

/* Number of UTF-16 code units a code point takes (Java String.length()). */
static inline int cp_units(uint32_t cp) { return cp > 0xFFFF ? 2 : 1; }
int cps_units(const uint32_t *cp, int n);

#endif
