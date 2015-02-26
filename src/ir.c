#include "ir.h"
#include "symbol.h"
#include "error.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>


static const char *
mklabel()
{
    static int n;

    char *name = malloc(sizeof(char) * 16);
    snprintf(name, 12, ".L%d", n++);

    return name;
}

decl_t *
cfg_create(const symbol_t *symbol)
{
    decl_t *decl = calloc(1, sizeof(decl_t));
    assert(decl);
    return decl;
}

block_t *
block_init(decl_t *decl)
{
    block_t *block;
    if (!decl) {
        error("Internal error, cannot create cfg node without function.");
    }
    block = calloc(1, sizeof(block_t));
    block->label = mklabel();
    block->expr = var_void();

    if (decl->size == decl->capacity) {
        decl->capacity += 16;
        decl->nodes = realloc(decl->nodes, decl->capacity * sizeof(block_t*));
    }
    decl->nodes[decl->size++] = block;

    return block;
}

void
cfg_finalize(decl_t *decl)
{
    if (!decl) return;
    if (decl->capacity) {
        int i;
        for (i = 0; i < decl->size; ++i) {
            block_t *block = decl->nodes[i];
            if (block->n) free(block->code);
            if (block->label) free((void *) block->label);
            free(block);
        }
        free(decl->nodes);
    }
    free(decl);
}

void
ir_append(block_t *block, op_t op)
{
    if (block) {
        block->n += 1;
        block->code = realloc(block->code, sizeof(op_t) * block->n);
        block->code[block->n - 1] = op;
    }
}
