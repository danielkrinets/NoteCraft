/* Text-field editing logic, modelled on Minecraft's TextFieldHelper.
 * Works on a caller-owned code point buffer; every edit is checked by a
 * validator (page limits, title limit...) and silently rejected if it fails. */
#ifndef NC_FIELD_H
#define NC_FIELD_H

#include <stdbool.h>
#include <stdint.h>

typedef bool (*FieldValid)(const uint32_t *s, int n, void *ud);

typedef struct {
    uint32_t *buf;
    int *len;
    int cap;
    int cur, sel;           /* cursor and selection anchor; equal = no selection */
    FieldValid valid;
    void *ud;
} Field;

void field_bind(Field *f, uint32_t *buf, int *len, int cap, FieldValid valid, void *ud);
bool field_has_selection(const Field *f);
void field_sel_range(const Field *f, int *a, int *b);

void field_set_cursor(Field *f, int pos, bool extend);
void field_to_end(Field *f);
void field_select_all(Field *f);
void field_select_range(Field *f, int a, int b);

/* Index reached by moving `dir` words (dir<0 left) from pos; only ' ' separates words. */
int  field_word_pos(const Field *f, int dir, int pos);
void field_move(Field *f, int dir, bool by_word, bool extend);

/* Insert replacing any selection. Rejected as a whole if the result is invalid. */
bool field_insert(Field *f, const uint32_t *s, int n);
/* Like field_insert, but inserts the longest prefix that fits (used for paste). */
int  field_insert_fit(Field *f, const uint32_t *s, int n);

/* Backspace (dir=-1) / Delete (dir=+1); by_word = Ctrl. */
bool field_delete(Field *f, int dir, bool by_word);
bool field_delete_selection(Field *f);

/* Selection as UTF-8 into out (NUL-terminated). Returns bytes written. */
int  field_selection_u8(const Field *f, char *out, int cap);

#endif
