/* MiniSharp - source span checker (PC test tool). */
#ifndef MS_ASTCHECK_H
#define MS_ASTCHECK_H
/* Prints every AST span of src that does not reparse to the same tree; returns 1 on any failure or syntax error. */
int ms_check_spans(const char* file, const char* src, int* checked);   /* *checked += spans checked */
/* Prints src again with the AST printer and checks that it parses to the same tree and prints the same again. */
int ms_check_print(const char* file, const char* src);
/* *out = src printed by the AST printer (malloc'd); returns nonzero on a syntax error (message in err). */
int ms_print_file(const char* src, char* err, int errlen, char** out);
#endif
