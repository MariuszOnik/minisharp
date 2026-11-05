/* MiniSharp - what the editor knows about the project's code without compiling it: the classes,
 * their fields and methods, and the type of a name from its declaration. Used for suggestions
 * after "ship." and for the signature hint of the user's own methods. */
#ifndef MS_CODEINDEX_H
#define MS_CODEINDEX_H

typedef struct {
    char cls[32], name[32];
    char type[48];                      /* field type or method return type, e.g. "List<Bullet>" */
    char sig[100];                      /* "void Hit(int dmg)" or "float x" */
    int isMethod;
} CIMember;

typedef struct {
    CIMember* m; int n, cap;
    char classes[64][32]; int nclasses;
    char bases[64][64];                 /* "Enemy IHealable": what each class derives from, space separated */
    const char** texts; int ntexts;     /* the files the index was built from (not copied) */
} CodeIndex;

void ci_build(CodeIndex* ci, const char** texts, int ntexts);
void ci_free(CodeIndex* ci);

/* type of the expression that ends just before texts[t][dot] (the '.'), e.g. "ship", "Game.ship",
 * "bullets[i]", "this". 1 = found, the type in 'type' ("Ship", "List<Bullet>", "string", "int[]") */
int ci_type_before(const CodeIndex* ci, int t, int dot, char* type, int cap);

/* the member names of a type, separated by spaces: user classes, List, Dictionary, string, arrays */
void ci_member_names(const CodeIndex* ci, const char* type, char* out, int cap);

/* members after "expr." ending at 'cursor' in text t, filtered by what is typed after the dot
 * (an empty prefix lists them all). -1: the cursor is not after a dot, or the type is unknown */
#ifndef REPL_WORD
#define REPL_WORD 32
#endif
int ci_complete(const CodeIndex* ci, int t, int cursor, int* start, char out[][REPL_WORD], int max);

/* "void Hit(int dmg)" for type.name, or NULL */
const char* ci_signature(const CodeIndex* ci, const char* type, const char* name);

/* name of the class the position is in, or NULL */
const char* ci_class_at(const CodeIndex* ci, int t, int pos);

#endif
