/* MiniSharp - REPL (terminal): runs one line at a time, keeping variables between lines. */
#ifndef MS_REPL_H
#define MS_REPL_H

typedef struct Repl Repl;

Repl* repl_new(void);
void repl_free(Repl* r);

/* runs one line; output goes to plat_print. 1 = ok, 0 = error (message in err) */
int repl_eval(Repl* r, const char* line, char* err, int errlen);

/* completions for the word that ends at 'cursor': after "Gfx." the members of Gfx, otherwise
 * classes, keywords and the terminal's variables. Returns how many (up to max) and sets *start
 * to where the word begins (the completion replaces line[*start..cursor]). */
#define REPL_WORD 32
int repl_complete(Repl* r, const char* line, int cursor, int* start, char out[][REPL_WORD], int max);
/* the same with the caller's own names ('words', separated by spaces) instead of the terminal's variables */
int ms_complete(const char* line, int cursor, int* start, char out[][REPL_WORD], int max, const char* words);

#endif
