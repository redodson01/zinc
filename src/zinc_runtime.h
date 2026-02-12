#ifndef ZINC_RUNTIME_H
#define ZINC_RUNTIME_H

/*
 * Zinc runtime library — included by generated code via #include.
 *
 * This file is self-contained: it defines all runtime types and provides
 * static function implementations. The compiler copies it to the output
 * directory alongside generated .c/.h files.
 *
 * Uses int32_t for _rc and _len fields to keep struct sizes tight (#18).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* --- Type definitions --- */

typedef struct { int32_t _rc; int32_t _len; char _data[]; } ZnString;

/* --- String runtime --- */

static inline void __zn_str_retain(ZnString *s) {
    if (s && s->_rc >= 0) s->_rc++;
}

static inline void __zn_str_release(ZnString *s) {
    if (s && s->_rc >= 0 && --(s->_rc) == 0) free(s);
}

static ZnString *__zn_str_alloc(const char *data, int32_t len) {
    ZnString *s = malloc(sizeof(ZnString) + len + 1);
    s->_rc = 1;
    s->_len = len;
    memcpy(s->_data, data, len);
    s->_data[len] = '\0';
    return s;
}

static ZnString *__zn_str_concat(ZnString *a, ZnString *b) {
    int32_t len = a->_len + b->_len;
    ZnString *s = malloc(sizeof(ZnString) + len + 1);
    s->_rc = 1;
    s->_len = len;
    memcpy(s->_data, a->_data, a->_len);
    memcpy(s->_data + a->_len, b->_data, b->_len);
    s->_data[len] = '\0';
    return s;
}

/* Coercion functions for string interpolation and concatenation */

static ZnString *__zn_str_from_int(int64_t v) {
    char buf[32]; int len = snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return __zn_str_alloc(buf, len);
}

static ZnString *__zn_str_from_float(double v) {
    char buf[64]; int len = snprintf(buf, sizeof(buf), "%g", v);
    return __zn_str_alloc(buf, len);
}

static ZnString *__zn_str_from_bool(bool v) {
    return v ? __zn_str_alloc("true", 4) : __zn_str_alloc("false", 5);
}

static ZnString *__zn_str_from_char(char c) {
    return __zn_str_alloc(&c, 1);
}

#endif
