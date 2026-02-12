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

typedef enum { ZN_TAG_INT = 0, ZN_TAG_FLOAT = 1, ZN_TAG_BOOL = 2, ZN_TAG_CHAR = 3,
               ZN_TAG_STRING = 4, ZN_TAG_ARRAY = 5,
               ZN_TAG_REF = 7, ZN_TAG_VAL = 8 } ZnTag;

typedef struct { ZnTag tag; union { int64_t i; double f; bool b; char c; void *ptr; } as; } ZnValue;
typedef void (*ZnElemFn)(void*);

typedef struct { int32_t _rc; int32_t _len; int32_t _cap; ZnValue *_data;
                 ZnElemFn _elem_retain; ZnElemFn _elem_release; } ZnArray;

typedef struct { bool _has; int64_t _val; } ZnOpt_int;
typedef struct { bool _has; double _val; } ZnOpt_float;
typedef struct { bool _has; bool _val; } ZnOpt_bool;
typedef struct { bool _has; char _val; } ZnOpt_char;

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

/* --- ZnValue boxing/unboxing --- */

static ZnValue __zn_val_int(int64_t v) { ZnValue r; r.tag = ZN_TAG_INT; r.as.i = v; return r; }
static ZnValue __zn_val_float(double v) { ZnValue r; r.tag = ZN_TAG_FLOAT; r.as.f = v; return r; }
static ZnValue __zn_val_bool(bool v) { ZnValue r; r.tag = ZN_TAG_BOOL; r.as.b = v; return r; }
static ZnValue __zn_val_char(char v) { ZnValue r; r.tag = ZN_TAG_CHAR; r.as.c = v; return r; }
static ZnValue __zn_val_string(ZnString *v) { ZnValue r; r.tag = ZN_TAG_STRING; r.as.ptr = v; return r; }
static ZnValue __zn_val_array(ZnArray *v) { ZnValue r; r.tag = ZN_TAG_ARRAY; r.as.ptr = v; return r; }
static ZnValue __zn_val_ref(void *v) { ZnValue r; r.tag = ZN_TAG_REF; r.as.ptr = v; return r; }
static ZnValue __zn_val_val(void *v) { ZnValue r; r.tag = ZN_TAG_VAL; r.as.ptr = v; return r; }

static int64_t __zn_val_as_int(ZnValue v) { return v.as.i; }
static double __zn_val_as_float(ZnValue v) { return v.as.f; }
static bool __zn_val_as_bool(ZnValue v) { return v.as.b; }
static char __zn_val_as_char(ZnValue v) { return v.as.c; }
static ZnString *__zn_val_as_string(ZnValue v) { return (ZnString*)v.as.ptr; }

/* --- Array runtime (callback-based ARC) --- */

static ZnArray *__zn_arr_alloc(int cap, ZnElemFn retain, ZnElemFn release) {
    ZnArray *a = malloc(sizeof(ZnArray));
    a->_rc = 1; a->_len = 0; a->_cap = cap;
    a->_data = cap > 0 ? calloc(cap, sizeof(ZnValue)) : NULL;
    a->_elem_retain = retain;
    a->_elem_release = release;
    return a;
}

static void __zn_arr_retain(ZnArray *a) { if (a) a->_rc++; }

static void __zn_arr_release(ZnArray *a) {
    if (!a) return;
    if (--(a->_rc) == 0) {
        if (a->_elem_release) {
            for (int i = 0; i < a->_len; i++) {
                if (a->_data[i].as.ptr) a->_elem_release(a->_data[i].as.ptr);
            }
        }
        free(a->_data);
        free(a);
    }
}

static void __zn_arr_push(ZnArray *a, ZnValue v) {
    if (a->_len >= a->_cap) {
        a->_cap = a->_cap > 0 ? a->_cap * 2 : 4;
        a->_data = realloc(a->_data, a->_cap * sizeof(ZnValue));
    }
    if (a->_elem_retain && v.as.ptr) a->_elem_retain(v.as.ptr);
    a->_data[a->_len++] = v;
}

static ZnValue __zn_arr_get(ZnArray *a, int64_t idx) {
    if (idx < 0 || idx >= a->_len) { fprintf(stderr, "Array index out of bounds: %lld (length %d)\n", (long long)idx, a->_len); exit(1); }
    return a->_data[idx];
}

static void __zn_arr_set(ZnArray *a, int64_t idx, ZnValue v) {
    if (idx < 0 || idx >= a->_len) { fprintf(stderr, "Array index out of bounds: %lld (length %d)\n", (long long)idx, a->_len); exit(1); }
    ZnValue old = a->_data[idx];
    if (a->_elem_release && old.as.ptr) a->_elem_release(old.as.ptr);
    if (a->_elem_retain && v.as.ptr) a->_elem_retain(v.as.ptr);
    a->_data[idx] = v;
}

/* Wrapper to cast __zn_str_retain/release for use as ZnElemFn */
static void __zn_str_retain_v(void *p) { __zn_str_retain((ZnString*)p); }
static void __zn_str_release_v(void *p) { __zn_str_release((ZnString*)p); }
static void __zn_arr_retain_v(void *p) { __zn_arr_retain((ZnArray*)p); }
static void __zn_arr_release_v(void *p) { __zn_arr_release((ZnArray*)p); }

#endif
