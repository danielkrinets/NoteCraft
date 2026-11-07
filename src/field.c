#include "field.h"

#include <string.h>

#include "utf.h"

void field_bind(Field *f, uint32_t *buf, int *len, int cap, FieldValid valid, void *ud)
{
    f->buf = buf; f->len = len; f->cap = cap; f->valid = valid; f->ud = ud;
    f->cur = f->sel = *len;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool field_has_selection(const Field *f) { return f->cur != f->sel; }

void field_sel_range(const Field *f, int *a, int *b)
{
    *a = f->cur < f->sel ? f->cur : f->sel;
    *b = f->cur < f->sel ? f->sel : f->cur;
}

void field_set_cursor(Field *f, int pos, bool extend)
{
    f->cur = clampi(pos, 0, *f->len);
    if (!extend) f->sel = f->cur;
}

void field_to_end(Field *f) { f->cur = f->sel = *f->len; }

void field_select_all(Field *f) { f->sel = 0; f->cur = *f->len; }

void field_select_range(Field *f, int a, int b)
{
    f->sel = clampi(a, 0, *f->len);
    f->cur = clampi(b, 0, *f->len);
}

int field_word_pos(const Field *f, int dir, int pos)
{
    int i = pos, n = *f->len;
    const uint32_t *s = f->buf;
    if (dir < 0) {
        while (i > 0 && s[i - 1] == ' ') i--;
        while (i > 0 && s[i - 1] != ' ') i--;
    } else {
        while (i < n && s[i] != ' ') i++;
        while (i < n && s[i] == ' ') i++;
    }
    return i;
}

void field_move(Field *f, int dir, bool by_word, bool extend)
{
    int target;
    if (!extend && field_has_selection(f)) {
        /* Collapse towards the direction of travel, like vanilla. */
        int a, b; field_sel_range(f, &a, &b);
        f->cur = f->sel = dir < 0 ? a : b;
        if (!by_word) return;
    }
    target = by_word ? field_word_pos(f, dir, f->cur) : f->cur + dir;
    field_set_cursor(f, target, extend);
}

static bool commit(Field *f, const uint32_t *tmp, int tn)
{
    if (f->valid && !f->valid(tmp, tn, f->ud)) return false;
    if (tn >= f->cap) return false;
    memcpy(f->buf, tmp, (size_t)tn * sizeof(uint32_t));
    *f->len = tn;
    return true;
}

static int build(const Field *f, const uint32_t *s, int n, uint32_t *tmp, int tmpcap)
{
    int a, b; field_sel_range(f, &a, &b);
    int len = *f->len;
    int tn = a + n + (len - b);
    if (tn > tmpcap) return -1;
    memcpy(tmp, f->buf, (size_t)a * sizeof(uint32_t));
    if (n > 0) memcpy(tmp + a, s, (size_t)n * sizeof(uint32_t));
    memcpy(tmp + a + n, f->buf + b, (size_t)(len - b) * sizeof(uint32_t));
    return tn;
}

bool field_insert(Field *f, const uint32_t *s, int n)
{
    uint32_t tmp[4096];
    int tn = build(f, s, n, tmp, 4096);
    if (tn < 0) return false;
    int a, b; field_sel_range(f, &a, &b);
    if (!commit(f, tmp, tn)) return false;
    f->cur = f->sel = a + n;
    return true;
}

int field_insert_fit(Field *f, const uint32_t *s, int n)
{
    if (n > 2048) n = 2048;
    for (int k = n; k > 0; k--)
        if (field_insert(f, s, k)) return k;
    return 0;
}

bool field_delete_selection(Field *f)
{
    if (!field_has_selection(f)) return false;
    uint32_t tmp[4096];
    int a, b; field_sel_range(f, &a, &b);
    int tn = build(f, NULL, 0, tmp, 4096);
    if (tn < 0 || !commit(f, tmp, tn)) return false;
    f->cur = f->sel = a;
    return true;
}

bool field_delete(Field *f, int dir, bool by_word)
{
    if (field_has_selection(f)) return field_delete_selection(f);
    int target = by_word ? field_word_pos(f, dir, f->cur) : f->cur + dir;
    target = clampi(target, 0, *f->len);
    if (target == f->cur) return false;
    f->sel = target;
    return field_delete_selection(f);
}

int field_selection_u8(const Field *f, char *out, int cap)
{
    int a, b; field_sel_range(f, &a, &b);
    return (int)cps_to_u8(f->buf + a, b - a, out, (size_t)cap);
}
