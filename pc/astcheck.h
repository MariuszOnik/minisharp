/* MiniSharp - source span checker (PC test tool). */
#ifndef MS_ASTCHECK_H
#define MS_ASTCHECK_H
/* Prints every AST span of src that does not reparse to the same tree; returns 1 on any failure or syntax error. */
int ms_check_spans(const char* file, const char* src, int* checked);   /* *checked += spans checked */
#endif
