#include "layout.h"

#include "font.h"

int layout_split(const uint32_t *s, int n, int max_w, Line *out, int cap)
{
    int count = 0, i = 0;
    while (i < n && count < cap) {
        int width = 0, last_space = -1, brk_at = -1, had = 0;
        for (int j = i; j < n; j++) {
            uint32_t c = s[j];
            if (c == '\n') { brk_at = j; break; }
            if (c == ' ') last_space = j;
            int w = font_cp_width(c);
            width += w;
            if (had && width > max_w) {
                brk_at = last_space != -1 ? last_space : j;
                break;
            }
            if (w != 0) had = 1;
        }
        if (brk_at < 0) {
            out[count].start = i; out[count].end = n; out[count].brk = 0;
            count++;
            break;
        }
        int consume = (s[brk_at] == '\n' || s[brk_at] == ' ');
        int l = consume ? brk_at + 1 : brk_at;
        out[count].start = i; out[count].end = l; out[count].brk = consume;
        count++;
        i = l;
    }
    return count;
}

int layout_display(const uint32_t *s, int n, int max_w, Line *out, int cap)
{
    if (n == 0) {
        if (cap > 0) { out[0].start = 0; out[0].end = 0; out[0].brk = 0; }
        return cap > 0 ? 1 : 0;
    }
    int c = layout_split(s, n, max_w, out, cap);
    if (s[n - 1] == '\n' && c < cap) {
        out[c].start = n; out[c].end = n; out[c].brk = 0;
        c++;
    }
    return c;
}

int layout_line_of(const Line *lines, int nlines, int pos)
{
    int r = 0;
    for (int i = 0; i < nlines; i++)
        if (lines[i].start <= pos) r = i;
    return r;
}

int layout_vis_end(const uint32_t *s, const Line *l)
{
    if (l->brk && l->end > l->start && (s[l->end - 1] == ' ' || s[l->end - 1] == '\n'))
        return l->end - 1;
    return l->end;
}
