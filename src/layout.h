/* Word wrapping that mirrors Minecraft's StringSplitter.splitLines():
 *  - a '\n' always ends a line (and is consumed by it)
 *  - the line is broken at the LAST space once the width exceeds the limit
 *    (the space is consumed by the line and may overhang the right edge)
 *  - a word with no space in front of it is broken between characters
 *  - every line holds at least one character. */
#ifndef NC_LAYOUT_H
#define NC_LAYOUT_H

#include <stdint.h>

typedef struct {
    int start, end;   /* [start,end) in code points, includes a consumed ' '/'\n' */
    int brk;          /* 1 if the line ends with such a consumed break character */
} Line;

/* Splits text; writes at most `cap` lines, returns how many were produced.
 * Empty text yields 0 lines (like vanilla). */
int layout_split(const uint32_t *s, int n, int max_w, Line *out, int cap);

/* Same, but as the book *display* sees it: empty text and text ending in a
 * newline get one extra empty line so the cursor has somewhere to sit. */
int layout_display(const uint32_t *s, int n, int max_w, Line *out, int cap);

/* Index of the line holding cursor position `pos` (start <= pos < next start,
 * the last line also owns pos == n). */
int layout_line_of(const Line *lines, int nlines, int pos);

/* Visible end of a line (without its consumed break character). */
int layout_vis_end(const uint32_t *s, const Line *l);

#endif
