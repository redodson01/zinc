#ifndef SCANNER_EXTRA_H
#define SCANNER_EXTRA_H

typedef struct {
    int column;
    int brace_depth;
    int had_interpolation;
    char *str_buf;
    int str_buf_len;
    int str_buf_cap;
} ScannerExtra;

#endif
