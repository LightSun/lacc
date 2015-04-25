#ifndef MACRO_H
#define MACRO_H

#include "preprocess.h"

#include <stdlib.h>


typedef struct {
    token_t name;
    enum { OBJECT_LIKE, FUNCTION_LIKE } type;

    size_t params;
    size_t size;

    /* A substitution is either a token or a parameter. */
    struct macro_subst_t {
        token_t token;
        int param;
    } *replacement;

} macro_t;

typedef struct {
    token_t *elem;
    size_t length;
    size_t cap;
} toklist_t;

toklist_t *toklist_init();
void toklist_push_back(toklist_t *, token_t);
void toklist_destroy(toklist_t *);
token_t toklist_to_string(toklist_t *);

char *pastetok(char *, token_t);

void define_macro(macro_t *);

void define(token_t, token_t);

void undef(token_t name);

macro_t *definition(token_t);

toklist_t *expand_macro(macro_t *, toklist_t **);

void register_builtin_definitions();


#endif