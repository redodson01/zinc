#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ast.h"
#include "scanner_extra.h"
#include "parser.h"
#include "scanner.h"
#include "semantic.h"
#include "codegen.h"

extern int yyparse(yyscan_t scanner, ASTNode **result, int *nerrs);

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input.zn>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --ast        Print AST only (no code generation)\n");
    fprintf(stderr, "  --check      Type check only (no code generation)\n");
    fprintf(stderr, "  -c, --compile  Compile generated C to executable\n");
    fprintf(stderr, "  -o <file>    Output base name (default: derived from input)\n");
    fprintf(stderr, "  -h, --help   Show this help\n");
}

static char *get_compiler_dir(const char *argv0) {
    static char dir[PATH_MAX];
    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        strncpy(resolved, argv0, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }
    strncpy(dir, resolved, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else strcpy(dir, ".");
    return dir;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "r");
    if (!in) return -1;
    FILE *out = fopen(dst, "w");
    if (!out) { fclose(in); return -1; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

static const char *output_dir_of(const char *output_base) {
    static char dir[256];
    strncpy(dir, output_base, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; return dir; }
    return ".";
}

static char *derive_output_base(const char *input_file) {
    static char buf[256];
    const char *base = strrchr(input_file, '/');
    base = base ? base + 1 : input_file;
    strncpy(buf, base, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *dot = strrchr(buf, '.');
    if (dot && strcmp(dot, ".zn") == 0) *dot = '\0';
    return buf;
}

int main(int argc, char **argv) {
    const char *input_file = NULL;
    const char *output_base = NULL;
    int ast_only = 0;
    int check_only = 0;
    int do_compile = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0) {
            ast_only = 1;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--compile") == 0) {
            do_compile = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_base = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!output_base) {
        output_base = input_file ? derive_output_base(input_file) : "output";
    }

    FILE *input = stdin;
    if (input_file) {
        input = fopen(input_file, "r");
        if (!input) { perror(input_file); return 1; }
    }

    yyscan_t scanner;
    yylex_init(&scanner);

    ScannerExtra *extra = calloc(1, sizeof(ScannerExtra));
    extra->column = 1;
    yyset_extra(extra, scanner);
    yyset_in(input, scanner);

    ASTNode *root = NULL;
    int nerrs = 0;
    int parse_result = yyparse(scanner, &root, &nerrs);

    yylex_destroy(scanner);
    free(extra->str_buf);
    free(extra);
    if (input != stdin) fclose(input);

    if (nerrs > 0) {
        fprintf(stderr, "\n%d parse error(s) encountered.\n", nerrs);
        free_ast(root);
        return 1;
    }
    if (parse_result != 0) { free_ast(root); return 1; }

    if (ast_only) {
        printf("=== Abstract Syntax Tree ===\n\n");
        print_ast(root, 0);
        free_ast(root);
        return 0;
    }

    SemanticContext *sem_ctx = semantic_init();
    int sem_errors = analyze(sem_ctx, root);

    if (sem_errors > 0) {
        fprintf(stderr, "\n%d semantic error(s) encountered.\n", sem_errors);
        semantic_free(sem_ctx);
        free_ast(root);
        return 1;
    }

    if (check_only) {
        printf("Type checking passed.\n");
        semantic_free(sem_ctx);
        free_ast(root);
        return 0;
    }

    char c_filename[256], h_filename[256];
    snprintf(c_filename, sizeof(c_filename), "%s.c", output_base);
    snprintf(h_filename, sizeof(h_filename), "%s.h", output_base);

    FILE *c_file = fopen(c_filename, "w");
    FILE *h_file = fopen(h_filename, "w");
    if (!c_file || !h_file) {
        perror("Could not open output files");
        if (c_file) fclose(c_file);
        if (h_file) fclose(h_file);
        semantic_free(sem_ctx);
        free_ast(root);
        return 1;
    }

    CodegenContext *cg_ctx = codegen_init(c_file, h_file, sem_ctx, output_base, input_file ? input_file : "<stdin>");
    generate(cg_ctx, root);
    codegen_free(cg_ctx);

    fclose(c_file);
    fclose(h_file);

    /* Copy zinc_runtime.h to output directory */
    char runtime_src[PATH_MAX], runtime_dst[256];
    snprintf(runtime_src, sizeof(runtime_src), "%s/zinc_runtime.h", get_compiler_dir(argv[0]));
    snprintf(runtime_dst, sizeof(runtime_dst), "%s/zinc_runtime.h", output_dir_of(output_base));
    if (copy_file(runtime_src, runtime_dst) != 0) {
        fprintf(stderr, "Warning: Could not copy zinc_runtime.h from %s\n", runtime_src);
    }
    printf("Generated %s, %s, and %s\n", c_filename, h_filename, runtime_dst);

    semantic_free(sem_ctx);
    free_ast(root);

    if (do_compile) {
        char compile_cmd[1024];
        snprintf(compile_cmd, sizeof(compile_cmd),
                 "gcc -Wall -o \"%s\" \"%s\"", output_base, c_filename);
        printf("Compiling: %s\n", compile_cmd);
        int result = system(compile_cmd);
        if (result != 0) { fprintf(stderr, "Compilation failed\n"); return 1; }
        printf("Created executable: %s\n", output_base);
    }

    return 0;
}
