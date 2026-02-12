#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "codegen.h"

/* Generate struct typedef to header */
void gen_struct_def(CodegenContext *ctx, ASTNode *node) {
    const char *name = node->data.type_def.name;
    StructDef *sd = lookup_struct(ctx->sem_ctx, name);
    if (!sd) return;

    fprintf(ctx->h_file, "typedef struct {\n");
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        if (fd->type->kind == TK_STRUCT && fd->type->name) {
            fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
        } else {
            fprintf(ctx->h_file, "    %s %s;\n", type_to_c(fd->type->kind), fd->name);
        }
    }
    fprintf(ctx->h_file, "} %s;\n\n", name);
}
