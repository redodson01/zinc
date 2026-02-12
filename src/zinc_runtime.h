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
               ZN_TAG_STRING = 4, ZN_TAG_ARRAY = 5, ZN_TAG_HASH = 6,
               ZN_TAG_REF = 7, ZN_TAG_VAL = 8 } ZnTag;

typedef struct { ZnTag tag; union { int64_t i; double f; bool b; char c; void *ptr; } as; } ZnValue;
typedef void (*ZnElemFn)(void*);
typedef unsigned int (*ZnHashFn)(ZnValue);
typedef bool (*ZnEqFn)(ZnValue, ZnValue);

typedef struct { int32_t _rc; int32_t _len; int32_t _cap; ZnValue *_data;
                 ZnElemFn _elem_retain; ZnElemFn _elem_release;
                 ZnHashFn _elem_hashcode; ZnEqFn _elem_equals; } ZnArray;
typedef struct ZnHashEntry { ZnValue key; ZnValue value; struct ZnHashEntry *next; } ZnHashEntry;
typedef struct { int32_t _rc; int32_t _len; int32_t _cap; ZnHashEntry **_buckets;
                 ZnElemFn _key_retain; ZnElemFn _key_release;
                 ZnHashFn _key_hashcode; ZnEqFn _key_equals;
                 ZnElemFn _val_retain; ZnElemFn _val_release; } ZnHash;

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
static ZnValue __zn_val_hash(ZnHash *v) { ZnValue r; r.tag = ZN_TAG_HASH; r.as.ptr = v; return r; }
static ZnValue __zn_val_ref(void *v) { ZnValue r; r.tag = ZN_TAG_REF; r.as.ptr = v; return r; }
static ZnValue __zn_val_val(void *v) { ZnValue r; r.tag = ZN_TAG_VAL; r.as.ptr = v; return r; }

static int64_t __zn_val_as_int(ZnValue v) { return v.as.i; }
static double __zn_val_as_float(ZnValue v) { return v.as.f; }
static bool __zn_val_as_bool(ZnValue v) { return v.as.b; }
static char __zn_val_as_char(ZnValue v) { return v.as.c; }
static ZnString *__zn_val_as_string(ZnValue v) { return (ZnString*)v.as.ptr; }

/* --- Default hashcode/equals for primitives+strings --- */

static unsigned int __zn_val_hashcode(ZnValue v) {
    switch (v.tag) {
    case ZN_TAG_INT: { uint64_t x = (uint64_t)v.as.i; return (unsigned int)(x ^ (x >> 32)); }
    case ZN_TAG_FLOAT: { union { double d; uint64_t u; } cv; cv.d = v.as.f; return (unsigned int)(cv.u ^ (cv.u >> 32)); }
    case ZN_TAG_BOOL: return v.as.b ? 1 : 0;
    case ZN_TAG_CHAR: return (unsigned int)v.as.c;
    case ZN_TAG_STRING: { ZnString *s = (ZnString*)v.as.ptr; unsigned int h = 5381; for (int i = 0; i < s->_len; i++) h = h * 33 + (unsigned char)s->_data[i]; return h; }
    default: return 0;
    }
}

static bool __zn_val_eq(ZnValue a, ZnValue b) {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
    case ZN_TAG_INT: return a.as.i == b.as.i;
    case ZN_TAG_FLOAT: return a.as.f == b.as.f;
    case ZN_TAG_BOOL: return a.as.b == b.as.b;
    case ZN_TAG_CHAR: return a.as.c == b.as.c;
    case ZN_TAG_STRING: { ZnString *sa = (ZnString*)a.as.ptr, *sb = (ZnString*)b.as.ptr; return sa->_len == sb->_len && memcmp(sa->_data, sb->_data, sa->_len) == 0; }
    default: return a.as.ptr == b.as.ptr;
    }
}

static unsigned int __zn_default_hashcode(ZnValue v) { return __zn_val_hashcode(v); }
static bool __zn_default_equals(ZnValue a, ZnValue b) { return __zn_val_eq(a, b); }

/* --- Array runtime (callback-based ARC) --- */

static ZnArray *__zn_arr_alloc(int cap, ZnElemFn retain, ZnElemFn release, ZnHashFn hashcode, ZnEqFn equals) {
    ZnArray *a = malloc(sizeof(ZnArray));
    a->_rc = 1; a->_len = 0; a->_cap = cap;
    a->_data = cap > 0 ? calloc(cap, sizeof(ZnValue)) : NULL;
    a->_elem_retain = retain;
    a->_elem_release = release;
    a->_elem_hashcode = hashcode;
    a->_elem_equals = equals;
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

/* --- Hash runtime (callback-based) --- */

static ZnHash *__zn_hash_alloc(int cap, ZnElemFn key_retain, ZnElemFn key_release,
                                ZnHashFn key_hashcode, ZnEqFn key_equals,
                                ZnElemFn val_retain, ZnElemFn val_release) {
    ZnHash *h = malloc(sizeof(ZnHash));
    h->_rc = 1; h->_len = 0; h->_cap = cap > 0 ? cap : 8;
    h->_buckets = calloc(h->_cap, sizeof(ZnHashEntry*));
    h->_key_retain = key_retain;
    h->_key_release = key_release;
    h->_key_hashcode = key_hashcode;
    h->_key_equals = key_equals;
    h->_val_retain = val_retain;
    h->_val_release = val_release;
    return h;
}

static void __zn_hash_retain(ZnHash *h) { if (h) h->_rc++; }

static void __zn_hash_release(ZnHash *h) {
    if (!h) return;
    if (--(h->_rc) == 0) {
        for (int i = 0; i < h->_cap; i++) {
            ZnHashEntry *e = h->_buckets[i];
            while (e) {
                ZnHashEntry *next = e->next;
                if (h->_key_release && e->key.as.ptr) h->_key_release(e->key.as.ptr);
                if (h->_val_release && e->value.as.ptr) h->_val_release(e->value.as.ptr);
                free(e);
                e = next;
            }
        }
        free(h->_buckets); free(h);
    }
}

static void __zn_hash_resize(ZnHash *h, int new_cap) {
    ZnHashEntry **old_buckets = h->_buckets;
    int old_cap = h->_cap;
    h->_buckets = calloc(new_cap, sizeof(ZnHashEntry*));
    h->_cap = new_cap;
    for (int i = 0; i < old_cap; i++) {
        ZnHashEntry *e = old_buckets[i];
        while (e) {
            ZnHashEntry *next = e->next;
            unsigned int idx = h->_key_hashcode(e->key) % new_cap;
            e->next = h->_buckets[idx];
            h->_buckets[idx] = e;
            e = next;
        }
    }
    free(old_buckets);
}

static ZnValue __zn_hash_get(ZnHash *h, ZnValue key) {
    unsigned int idx = h->_key_hashcode(key) % h->_cap;
    for (ZnHashEntry *e = h->_buckets[idx]; e; e = e->next) {
        if (h->_key_equals(e->key, key)) return e->value;
    }
    ZnValue nil; nil.tag = ZN_TAG_INT; nil.as.i = 0; return nil;
}

static void __zn_hash_set(ZnHash *h, ZnValue key, ZnValue value) {
    unsigned int idx = h->_key_hashcode(key) % h->_cap;
    for (ZnHashEntry *e = h->_buckets[idx]; e; e = e->next) {
        if (h->_key_equals(e->key, key)) {
            if (h->_val_release && e->value.as.ptr) h->_val_release(e->value.as.ptr);
            if (h->_val_retain && value.as.ptr) h->_val_retain(value.as.ptr);
            e->value = value;
            return;
        }
    }
    ZnHashEntry *ne = malloc(sizeof(ZnHashEntry));
    if (h->_key_retain && key.as.ptr) h->_key_retain(key.as.ptr);
    if (h->_val_retain && value.as.ptr) h->_val_retain(value.as.ptr);
    ne->key = key; ne->value = value;
    ne->next = h->_buckets[idx];
    h->_buckets[idx] = ne;
    h->_len++;
    if (h->_len * 4 > h->_cap * 3) {
        __zn_hash_resize(h, h->_cap * 2);
    }
}

/* Wrapper to cast __zn_str_retain/release for use as ZnElemFn */
static void __zn_str_retain_v(void *p) { __zn_str_retain((ZnString*)p); }
static void __zn_str_release_v(void *p) { __zn_str_release((ZnString*)p); }
static void __zn_arr_retain_v(void *p) { __zn_arr_retain((ZnArray*)p); }
static void __zn_arr_release_v(void *p) { __zn_arr_release((ZnArray*)p); }
static void __zn_hash_retain_v(void *p) { __zn_hash_retain((ZnHash*)p); }
static void __zn_hash_release_v(void *p) { __zn_hash_release((ZnHash*)p); }

#endif
