/* MiniSharp - AST printer: turns syntax trees back into C# text in one fixed style.
   The graph editor uses it only for NEW code (inserted nodes); existing code is never reprinted,
   it is patched by source spans (see docs/graph-model.md). */
#ifndef MS_PRINTER_H
#define MS_PRINTER_H

#include "ast.h"

typedef struct { char* p; int n, cap; int oom; } MsBuf;    /* growable text; p is always 0-terminated (or NULL) */

void ms_buf_free(MsBuf* b);
void ms_print_type(MsBuf* b, TypeNode* tn);
void ms_print_expr(MsBuf* b, Expr* e);
void ms_print_stmt(MsBuf* b, Stmt* s, int indent);        /* indent = level (4 spaces each); no trailing newline */
void ms_print_program(MsBuf* b, ProgramAST* prog);

#endif
